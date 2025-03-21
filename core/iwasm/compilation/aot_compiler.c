/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "aot_compiler.h"
#include "aot_emit_compare.h"
#include "aot_emit_conversion.h"
#include "aot_emit_memory.h"
#include "aot_emit_variable.h"
#include "aot_emit_const.h"
#include "aot_emit_exception.h"
#include "aot_emit_numberic.h"
#include "aot_emit_control.h"
#include "aot_emit_function.h"
#include "aot_emit_parametric.h"
#include "aot_emit_table.h"
#include "simd/simd_access_lanes.h"
#include "simd/simd_bitmask_extracts.h"
#include "simd/simd_bit_shifts.h"
#include "simd/simd_bitwise_ops.h"
#include "simd/simd_bool_reductions.h"
#include "simd/simd_comparisons.h"
#include "simd/simd_conversions.h"
#include "simd/simd_construct_values.h"
#include "simd/simd_conversions.h"
#include "simd/simd_floating_point.h"
#include "simd/simd_int_arith.h"
#include "simd/simd_load_store.h"
#include "simd/simd_sat_int_arith.h"
#include "../aot/aot_runtime.h"
#include "../interpreter/wasm_opcode.h"
#include <errno.h>

#if WASM_ENABLE_DEBUG_AOT != 0
#include "debug/dwarf_extractor.h"
#endif

#define CHECK_BUF(buf, buf_end, length)                             \
    do {                                                            \
        if (buf + length > buf_end) {                               \
            aot_set_last_error("read leb failed: unexpected end."); \
            return false;                                           \
        }                                                           \
    } while (0)

static bool
read_leb(const uint8 *buf, const uint8 *buf_end, uint32 *p_offset,
         uint32 maxbits, bool sign, uint64 *p_result)
{
    uint64 result = 0;
    uint32 shift = 0;
    uint32 bcnt = 0;
    uint64 byte;

    while (true) {
        CHECK_BUF(buf, buf_end, 1);
        byte = buf[*p_offset];
        *p_offset += 1;
        result |= ((byte & 0x7f) << shift);
        shift += 7;
        if ((byte & 0x80) == 0) {
            break;
        }
        bcnt += 1;
    }
    if (bcnt > (maxbits + 6) / 7) {
        aot_set_last_error("read leb failed: "
                           "integer representation too long");
        return false;
    }
    if (sign && (shift < maxbits) && (byte & 0x40)) {
        /* Sign extend */
        result |= (~((uint64)0)) << shift;
    }
    *p_result = result;
    return true;
}

#define read_leb_uint32(p, p_end, res)                    \
    do {                                                  \
        uint32 off = 0;                                   \
        uint64 res64;                                     \
        if (!read_leb(p, p_end, &off, 32, false, &res64)) \
            return false;                                 \
        p += off;                                         \
        res = (uint32)res64;                              \
    } while (0)

#define read_leb_int32(p, p_end, res)                    \
    do {                                                 \
        uint32 off = 0;                                  \
        uint64 res64;                                    \
        if (!read_leb(p, p_end, &off, 32, true, &res64)) \
            return false;                                \
        p += off;                                        \
        res = (int32)res64;                              \
    } while (0)

#define read_leb_int64(p, p_end, res)                    \
    do {                                                 \
        uint32 off = 0;                                  \
        uint64 res64;                                    \
        if (!read_leb(p, p_end, &off, 64, true, &res64)) \
            return false;                                \
        p += off;                                        \
        res = (int64)res64;                              \
    } while (0)

/**
 * Since Wamrc uses a full feature Wasm loader,
 * add a post-validator here to run checks according
 * to options, like enable_tail_call, enable_ref_types,
 * and so on.
 */
static bool
aot_validate_wasm(AOTCompContext *comp_ctx)
{
    if (!comp_ctx->enable_ref_types) {
        /* Doesn't support multiple tables unless enabling reference type */
        if (comp_ctx->comp_data->import_table_count
                + comp_ctx->comp_data->table_count
            > 1) {
            aot_set_last_error("multiple tables");
            return false;
        }
    }

    return true;
}

#define COMPILE_ATOMIC_RMW(OP, NAME)                      \
    case WASM_OP_ATOMIC_RMW_I32_##NAME:                   \
        bytes = 4;                                        \
        op_type = VALUE_TYPE_I32;                         \
        goto OP_ATOMIC_##OP;                              \
    case WASM_OP_ATOMIC_RMW_I64_##NAME:                   \
        bytes = 8;                                        \
        op_type = VALUE_TYPE_I64;                         \
        goto OP_ATOMIC_##OP;                              \
    case WASM_OP_ATOMIC_RMW_I32_##NAME##8_U:              \
        bytes = 1;                                        \
        op_type = VALUE_TYPE_I32;                         \
        goto OP_ATOMIC_##OP;                              \
    case WASM_OP_ATOMIC_RMW_I32_##NAME##16_U:             \
        bytes = 2;                                        \
        op_type = VALUE_TYPE_I32;                         \
        goto OP_ATOMIC_##OP;                              \
    case WASM_OP_ATOMIC_RMW_I64_##NAME##8_U:              \
        bytes = 1;                                        \
        op_type = VALUE_TYPE_I64;                         \
        goto OP_ATOMIC_##OP;                              \
    case WASM_OP_ATOMIC_RMW_I64_##NAME##16_U:             \
        bytes = 2;                                        \
        op_type = VALUE_TYPE_I64;                         \
        goto OP_ATOMIC_##OP;                              \
    case WASM_OP_ATOMIC_RMW_I64_##NAME##32_U:             \
        bytes = 4;                                        \
        op_type = VALUE_TYPE_I64;                         \
        OP_ATOMIC_##OP : bin_op = LLVMAtomicRMWBinOp##OP; \
        goto build_atomic_rmw;

static bool
store_value(AOTCompContext *comp_ctx, LLVMValueRef value, uint8 value_type,
            LLVMValueRef cur_frame, uint32 offset)
{
    LLVMValueRef value_offset, value_addr, value_ptr = NULL, res;
    LLVMTypeRef value_ptr_type;

    if (!(value_offset = I32_CONST(offset))) {
        aot_set_last_error("llvm build const failed");
        return false;
    }

    if (!(value_addr =
              LLVMBuildInBoundsGEP2(comp_ctx->builder, INT8_TYPE, cur_frame,
                                    &value_offset, 1, "value_addr"))) {
        aot_set_last_error("llvm build in bounds gep failed");
        return false;
    }

    switch (value_type) {
        case VALUE_TYPE_I32:
            value_ptr_type = INT32_PTR_TYPE;
            break;
        case VALUE_TYPE_I64:
            value_ptr_type = INT64_PTR_TYPE;
            break;
        case VALUE_TYPE_F32:
            value_ptr_type = F32_PTR_TYPE;
            break;
        case VALUE_TYPE_F64:
            value_ptr_type = F64_PTR_TYPE;
            break;
        case VALUE_TYPE_V128:
            value_ptr_type = V128_PTR_TYPE;
            break;
        default:
            bh_assert(0);
            break;
    }

    if (!(value_ptr = LLVMBuildBitCast(comp_ctx->builder, value_addr,
                                       value_ptr_type, "value_ptr"))) {
        aot_set_last_error("llvm build bit cast failed");
        return false;
    }

    if (!(res = LLVMBuildStore(comp_ctx->builder, value, value_ptr))) {
        aot_set_last_error("llvm build store failed");
        return false;
    }

    LLVMSetAlignment(res, 1);

    return true;
}

bool
aot_gen_commit_value(AOTCompFrame *frame, bool reset_dirty_bit, AOTValueSlot **p, uint32 local_idx) {
    AOTCompContext *comp_ctx = frame->comp_ctx;
    AOTFuncContext *func_ctx = frame->func_ctx;
    LLVMValueRef value;
    LLVMTypeRef llvm_value_type;
    uint32 n;

    if (!(*p)->dirty) {
        switch ((*p)->type) {
            case VALUE_TYPE_I32:
            case VALUE_TYPE_FUNCREF:
            case VALUE_TYPE_EXTERNREF:
            case VALUE_TYPE_F32:
            case VALUE_TYPE_I1:
                break;
            case VALUE_TYPE_I64:
            case VALUE_TYPE_F64:
                (*p)++;
                break;
            case VALUE_TYPE_V128:
                (*p) += 3;
                break;
            default:
                bh_assert(0);
                break;
        }
        return true;
    }

    if (reset_dirty_bit)
        (*p)->dirty = 0;
    n = (*p) - frame->lp;

    llvm_value_type = TO_LLVM_TYPE((*p)->type);
    if (llvm_value_type == NULL) {
        fprintf(stderr, "gen value %d type error\n", (*p)->type);
        fprintf(stderr, "local_idx %d n %d\n", local_idx, n);
        return false;
    }
    if (local_idx < func_ctx->aot_func->func_type->param_count
                        + func_ctx->aot_func->local_count) {
        // fprintf(stderr, "LLVMBuildLoad2 %d < %d\n", local_idx,
        //         func_ctx->aot_func->local_count);
        value = LLVMBuildLoad2(comp_ctx->builder, llvm_value_type,
                                func_ctx->locals[local_idx],
                                "commit_stack_load");
        // fprintf(stderr, "DONE LLVMBuildLoad2 %d\n", local_idx);
    }
    else {
        if (!(*p)->value) {
            fprintf(stderr, "value is null, %d %d\n", local_idx, n);
            exit(-1);
        }
        value = LLVMBuildLoad2(comp_ctx->builder, llvm_value_type, (*p)->value,
                                "commit_stack_load");
    }
    if (value == NULL) {
        fprintf(stderr, "gen value load error\n");
        return false;
    }

    switch ((*p)->type) {
        case VALUE_TYPE_I32:
        case VALUE_TYPE_FUNCREF:
        case VALUE_TYPE_EXTERNREF:
            if (!store_value(comp_ctx, value, VALUE_TYPE_I32,
                                func_ctx->cur_frame,
                                offset_of_local(comp_ctx, n)))
                return false;
            break;
        case VALUE_TYPE_I64:
            if (reset_dirty_bit)
                ((*p)+1)->dirty = 0;
            (*p) += 1;
            if (!store_value(comp_ctx, value, VALUE_TYPE_I64,
                                func_ctx->cur_frame,
                                offset_of_local(comp_ctx, n)))
                return false;
            break;
        case VALUE_TYPE_F32:
            if (!store_value(comp_ctx, value, VALUE_TYPE_F32,
                                func_ctx->cur_frame,
                                offset_of_local(comp_ctx, n)))
                return false;
            break;
        case VALUE_TYPE_F64:
            if (reset_dirty_bit)
                ((*p)+1)->dirty = 0;
            (*p) += 1;
            if (!store_value(comp_ctx, value, VALUE_TYPE_F64,
                                func_ctx->cur_frame,
                                offset_of_local(comp_ctx, n)))
                return false;
            break;
        case VALUE_TYPE_V128:
            if (reset_dirty_bit) {
                ((*p)+1)->dirty = 0;
                ((*p)+2)->dirty = 0;
                ((*p)+3)->dirty = 0;
            }
            (*p) += 3;
            if (!store_value(comp_ctx, value, VALUE_TYPE_V128,
                                func_ctx->cur_frame,
                                offset_of_local(comp_ctx, n)))
                return false;
            break;
        case VALUE_TYPE_I1:
            if (!(value = LLVMBuildZExt(comp_ctx->builder, value, I32_TYPE,
                                        "i32_val"))) {
                aot_set_last_error("llvm build bit cast failed");
                return false;
            }
            if (!store_value(comp_ctx, value, VALUE_TYPE_I32,
                                func_ctx->cur_frame,
                                offset_of_local(comp_ctx, n)))
                return false;
            break;
        default:
            bh_assert(0);
            break;
    }
    return true;
}

bool
fake_aot_gen_commit_value(AOTCompFrame *frame, bool reset_dirty_bit, AOTValueSlot **p, uint32 local_idx) {
    if (!(*p)->dirty) {
        switch ((*p)->type) {
            case VALUE_TYPE_I32:
            case VALUE_TYPE_FUNCREF:
            case VALUE_TYPE_EXTERNREF:
            case VALUE_TYPE_F32:
            case VALUE_TYPE_I1:
                break;
            case VALUE_TYPE_I64:
            case VALUE_TYPE_F64:
                (*p)++;
                break;
            case VALUE_TYPE_V128:
                (*p) += 3;
                break;
            default:
                bh_assert(0);
                break;
        }
        return true;
    }

    if (reset_dirty_bit)
        (*p)->dirty = 0;

    switch ((*p)->type) {
        case VALUE_TYPE_I32:
        case VALUE_TYPE_FUNCREF:
        case VALUE_TYPE_EXTERNREF:
            break;
        case VALUE_TYPE_I64:
            if (reset_dirty_bit)
                ((*p)+1)->dirty = 0;
            (*p) += 1;
            break;
        case VALUE_TYPE_F32:
            break;
        case VALUE_TYPE_F64:
            if (reset_dirty_bit)
                ((*p)+1)->dirty = 0;
            (*p) += 1;
            break;
        case VALUE_TYPE_V128:
            if (reset_dirty_bit) {
                ((*p)+1)->dirty = 0;
                ((*p)+2)->dirty = 0;
                ((*p)+3)->dirty = 0;
            }
            (*p) += 3;
            break;
        case VALUE_TYPE_I1:
            break;
        default:
            bh_assert(0);
            break;
    }
    return true;
}

bool
fake_aot_gen_commit_values(AOTCompFrame *frame, bool reset_dirty_bit)
{
    AOTCompContext *comp_ctx = frame->comp_ctx;
    AOTValueSlot *p;
    uint32 local_idx;

    reset_dirty_bit |= comp_ctx->enable_aux_stack_dirty_bit;

    for (p = frame->lp, local_idx = 0; p < frame->sp; p++, local_idx++) {
        if (!fake_aot_gen_commit_value(frame, reset_dirty_bit, &p, local_idx))
            return false;
    }

    return true;
}


bool
aot_gen_commit_values(AOTCompFrame *frame, bool reset_dirty_bit)
{
    AOTCompContext *comp_ctx = frame->comp_ctx;
    AOTValueSlot *p;
    uint32 local_idx;

    reset_dirty_bit |= comp_ctx->enable_aux_stack_dirty_bit;

    for (p = frame->lp, local_idx = 0; p < frame->sp; p++, local_idx++) {
        if (!aot_gen_commit_value(frame, reset_dirty_bit, &p, local_idx))
            return false;
    }

    return true;
}


bool
aot_gen_commit_all_locals(AOTCompFrame *frame)
{
    AOTCompContext *comp_ctx = frame->comp_ctx;
    AOTFuncContext *func_ctx = frame->func_ctx;
    AOTValueSlot *p;
    uint32 local_idx;
    LLVMValueRef value;
    LLVMTypeRef llvm_value_type;
    uint32 n;
    uint32 total_locals = func_ctx->aot_func->func_type->param_count + func_ctx->aot_func->local_count;

    for (
        p = frame->lp, local_idx = 0;
        local_idx < total_locals;
        p++, local_idx++) {

        n = (p) - frame->lp;
        llvm_value_type = TO_LLVM_TYPE(p->type);
        if (llvm_value_type == NULL) {
            fprintf(stderr, "gen value %d type error\n", (p)->type);
            fprintf(stderr, "local_idx %d n %d\n", local_idx, n);
            return false;
        }
        value = LLVMBuildLoad2(comp_ctx->builder, llvm_value_type,
                        func_ctx->locals[local_idx],
                        "commit_stack_load");
        if (value == NULL) {
            fprintf(stderr, "gen value load error\n");
            return false;
        }
        switch ((p)->type) {
            case VALUE_TYPE_I32:
            case VALUE_TYPE_FUNCREF:
            case VALUE_TYPE_EXTERNREF:
                if (!store_value(comp_ctx, value, VALUE_TYPE_I32,
                                    func_ctx->cur_frame,
                                    offset_of_local(comp_ctx, n)))
                    return false;
                break;
            case VALUE_TYPE_I64:
                if (!store_value(comp_ctx, value, VALUE_TYPE_I64,
                                    func_ctx->cur_frame,
                                    offset_of_local(comp_ctx, n)))
                    return false;
                break;
            case VALUE_TYPE_F32:
                if (!store_value(comp_ctx, value, VALUE_TYPE_F32,
                                    func_ctx->cur_frame,
                                    offset_of_local(comp_ctx, n)))
                    return false;
                break;
            case VALUE_TYPE_F64:
                (p) += 1;
                if (!store_value(comp_ctx, value, VALUE_TYPE_F64,
                                    func_ctx->cur_frame,
                                    offset_of_local(comp_ctx, n)))
                    return false;
                break;
            case VALUE_TYPE_V128:
                (p) += 3;
                if (!store_value(comp_ctx, value, VALUE_TYPE_V128,
                                    func_ctx->cur_frame,
                                    offset_of_local(comp_ctx, n)))
                    return false;
                break;
            case VALUE_TYPE_I1:
                if (!(value = LLVMBuildZExt(comp_ctx->builder, value, I32_TYPE,
                                            "i32_val"))) {
                    aot_set_last_error("llvm build bit cast failed");
                    return false;
                }
                if (!store_value(comp_ctx, value, VALUE_TYPE_I32,
                                    func_ctx->cur_frame,
                                    offset_of_local(comp_ctx, n)))
                    return false;
                break;
            default:
                bh_assert(0);
                break;
        }
    }

    return true;
}

static LLVMValueRef
load_value(AOTCompContext *comp_ctx, uint8 value_type, LLVMValueRef cur_frame,
           uint32 offset)
{
    LLVMValueRef value_offset, value_addr, value_ptr = NULL, res;
    LLVMTypeRef value_ptr_type, llvm_value_type;

    if (!(value_offset = I32_CONST(offset))) {
        aot_set_last_error("llvm build const failed");
        return false;
    }

    if (!(value_addr =
              LLVMBuildInBoundsGEP2(comp_ctx->builder, INT8_TYPE, cur_frame,
                                    &value_offset, 1, "value_addr"))) {
        aot_set_last_error("llvm build in bounds gep failed");
        return false;
    }

    switch (value_type) {
        case VALUE_TYPE_I32:
            llvm_value_type = I32_TYPE;
            value_ptr_type = INT32_PTR_TYPE;
            break;
        case VALUE_TYPE_I64:
            llvm_value_type = I64_TYPE;
            value_ptr_type = INT64_PTR_TYPE;
            break;
        case VALUE_TYPE_F32:
            llvm_value_type = F32_TYPE;
            value_ptr_type = F32_PTR_TYPE;
            break;
        case VALUE_TYPE_F64:
            llvm_value_type = F64_TYPE;
            value_ptr_type = F64_PTR_TYPE;
            break;
        case VALUE_TYPE_V128:
            llvm_value_type = V128_TYPE;
            value_ptr_type = V128_PTR_TYPE;
            break;
        default:
            bh_assert(0);
            break;
    }

    if (!(value_ptr = LLVMBuildBitCast(comp_ctx->builder, value_addr,
                                       value_ptr_type, "value_ptr"))) {
        aot_set_last_error("llvm build bit cast failed");
        return false;
    }

    if (!(res = LLVMBuildLoad2(comp_ctx->builder, llvm_value_type, value_ptr,
                               "restore_val"))) {
        aot_set_last_error("llvm build load failed");
        return false;
    }

    return res;
}

bool
aot_gen_restore_values(AOTCompFrame *frame)
{
    AOTCompContext *comp_ctx = frame->comp_ctx;
    AOTFuncContext *func_ctx = frame->func_ctx;
    AOTValueSlot *p;
    LLVMValueRef restore_value, store, value_ptr;
    uint32 n, local_idx = 0;

    for (p = frame->lp; p < frame->sp; p++, local_idx++) {
        n = p - frame->lp;

        if (local_idx < func_ctx->aot_func->func_type->param_count
                            + func_ctx->aot_func->local_count) {
            value_ptr = func_ctx->locals[local_idx];
        }
        else {
            value_ptr = p->value;
        }
        if (!value_ptr) {
            fprintf(stderr, "restore value is null, %d %d\n", local_idx, n);
            exit(-1);
        }

        switch (p->type) {
            case VALUE_TYPE_I32:
            case VALUE_TYPE_FUNCREF:
            case VALUE_TYPE_EXTERNREF:
                if (!(restore_value = load_value(comp_ctx, VALUE_TYPE_I32,
                                                 func_ctx->cur_frame,
                                                 offset_of_local(comp_ctx, n))))
                    return false;
                break;
            case VALUE_TYPE_I64:
                ++p;
                if (!(restore_value = load_value(comp_ctx, VALUE_TYPE_I64,
                                                 func_ctx->cur_frame,
                                                 offset_of_local(comp_ctx, n))))
                    return false;
                break;
            case VALUE_TYPE_F32:
                if (!(restore_value = load_value(comp_ctx, VALUE_TYPE_F32,
                                                 func_ctx->cur_frame,
                                                 offset_of_local(comp_ctx, n))))
                    return false;
                break;
            case VALUE_TYPE_F64:
                ++p;
                if (!(restore_value = load_value(comp_ctx, VALUE_TYPE_F64,
                                                 func_ctx->cur_frame,
                                                 offset_of_local(comp_ctx, n))))
                    return false;
                break;
            case VALUE_TYPE_V128:
                ++p;
                ++p;
                ++p;
                if (!(restore_value = load_value(comp_ctx, VALUE_TYPE_V128,
                                                 func_ctx->cur_frame,
                                                 offset_of_local(comp_ctx, n))))
                    return false;
                break;
            case VALUE_TYPE_I1:
                if (!(restore_value = load_value(comp_ctx, VALUE_TYPE_I32,
                                                 func_ctx->cur_frame,
                                                 offset_of_local(comp_ctx, n))))
                    return false;
                if (!(restore_value =
                          LLVMBuildTrunc(comp_ctx->builder, restore_value,
                                         INT1_TYPE, "restore_i1_val"))) {
                    aot_set_last_error("llvm build bit cast failed");
                }
                break;
            default:
                bh_assert(0);
                break;
        }

        store = LLVMBuildStore(comp_ctx->builder, restore_value, value_ptr);
        if (!store) {
            aot_set_last_error("llvm build store failed");
            return false;
        }
    }

    return true;
}

bool
aot_gen_commit_sp_ip(AOTCompFrame *frame, const AOTValueSlot *sp,
                     const uint8 *ip)
{
    AOTCompContext *comp_ctx = frame->comp_ctx;
    AOTFuncContext *func_ctx = frame->func_ctx;
    LLVMValueRef cur_frame = func_ctx->cur_frame;
    LLVMValueRef value_offset, value_addr, value_ptr, value;
    LLVMTypeRef int8_ptr_ptr_type;
    uint32 offset_ip, offset_sp, n;
    bool is_64bit = (comp_ctx->pointer_size == sizeof(uint64)) ? true : false;

    if (!comp_ctx->is_jit_mode) {
        offset_ip = offsetof(AOTFrame, ip_offset);
        offset_sp = offsetof(AOTFrame, sp);
    }
    else {
        offset_ip = offsetof(WASMInterpFrame, ip);
        offset_sp = offsetof(WASMInterpFrame, sp);
    }

    /* commit ip */

    if (!(value_offset = I32_CONST(offset_ip))) {
        aot_set_last_error("llvm build const failed");
        return false;
    }

    if (!(value_addr =
              LLVMBuildInBoundsGEP2(comp_ctx->builder, INT8_TYPE, cur_frame,
                                    &value_offset, 1, "ip_addr"))) {
        aot_set_last_error("llvm build in bounds gep failed");
        return false;
    }

    if (!(value_ptr = LLVMBuildBitCast(
              comp_ctx->builder, value_addr,
              is_64bit ? INT64_PTR_TYPE : INT32_PTR_TYPE, "ip_ptr"))) {
        aot_set_last_error("llvm build bit cast failed");
        return false;
    }

    if (!comp_ctx->is_jit_mode) {
        if (is_64bit)
            value =
                I64_CONST((uint64)(uintptr_t)(ip - func_ctx->aot_func->code));
        else
            value =
                I32_CONST((uint32)(uintptr_t)(ip - func_ctx->aot_func->code));
    }
    else {
        if (is_64bit)
            value = I64_CONST((uint64)(uintptr_t)ip);
        else
            value = I32_CONST((uint32)(uintptr_t)ip);
    }

    if (!value) {
        aot_set_last_error("llvm build const failed");
        return false;
    }

    if (!LLVMBuildStore(comp_ctx->builder, value, value_ptr)) {
        aot_set_last_error("llvm build store failed");
        return false;
    }

    /* commit sp */

    n = sp - frame->lp;
    value = I32_CONST(offset_of_local(comp_ctx, n));
    if (!value) {
        aot_set_last_error("llvm build const failed");
        return false;
    }

    if (!(value = LLVMBuildInBoundsGEP2(comp_ctx->builder, INT8_TYPE, cur_frame,
                                        &value, 1, "sp"))) {
        aot_set_last_error("llvm build in bounds gep failed");
        return false;
    }

    if (!(value_offset = I32_CONST(offset_sp))) {
        aot_set_last_error("llvm build const failed");
        return false;
    }

    if (!(value_addr =
              LLVMBuildInBoundsGEP2(comp_ctx->builder, INT8_TYPE, cur_frame,
                                    &value_offset, 1, "sp_addr"))) {
        aot_set_last_error("llvm build in bounds gep failed");
        return false;
    }

    if (!(int8_ptr_ptr_type = LLVMPointerType(INT8_PTR_TYPE, 0))) {
        aot_set_last_error("llvm build pointer type failed");
        return false;
    }

    if (!(value_ptr = LLVMBuildBitCast(comp_ctx->builder, value_addr,
                                       int8_ptr_ptr_type, "sp_ptr"))) {
        aot_set_last_error("llvm build bit cast failed");
        return false;
    }

    if (!LLVMBuildStore(comp_ctx->builder, value, value_ptr)) {
        aot_set_last_error("llvm build store failed");
        return false;
    }

    /* commit sp */
    return true;
}

bool
aot_gen_commit_ref_flags(AOTCompFrame *frame)
{
    /* TODO */
    return true;
}

static bool
init_comp_frame(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                uint32 func_idx)
{
    AOTCompFrame *aot_frame;
    WASMModule *wasm_module = comp_ctx->comp_data->wasm_module;
    AOTFunc *aot_func = func_ctx->aot_func;
    AOTFuncType *func_type = aot_func->func_type;
    AOTBlock *block = func_ctx->block_stack.block_list_end;
    LLVMValueRef local_value;
    uint32 max_local_cell_num =
        aot_func->param_cell_num + aot_func->local_cell_num;
    uint32 max_stack_cell_num = aot_func->max_stack_cell_num;
    uint32 all_cell_num = max_local_cell_num + max_stack_cell_num;
    uint32 i, n;
    uint64 total_size;
    uint8 local_type;

    /* Free aot_frame if it was allocated previously for
       compiling other functions */
    if (comp_ctx->aot_frame) {
        wasm_runtime_free(comp_ctx->aot_frame);
        comp_ctx->aot_frame = NULL;
    }

    /* Allocate extra 2 cells since some operations may push more
       operands than the number calculated in wasm loader, such as
       PUSH_F64(F64_CONST(1.0)) in aot_compile_op_f64_promote_f32 */
    all_cell_num += 2;
    total_size = offsetof(AOTCompFrame, lp)
                 + (uint64)sizeof(AOTValueSlot) * all_cell_num;

    if (total_size > UINT32_MAX
        || !(comp_ctx->aot_frame = aot_frame =
                 wasm_runtime_malloc((uint32)total_size))) {
        aot_set_last_error("allocate memory failed.");
        return false;
    }
    memset(aot_frame, 0, (uint32)total_size);

    aot_frame->cur_wasm_module = wasm_module;
    aot_frame->cur_wasm_func = wasm_module->functions[func_idx];
    aot_frame->cur_wasm_func_idx =
        func_idx + wasm_module->import_function_count;
    aot_frame->comp_ctx = comp_ctx;
    aot_frame->func_ctx = func_ctx;

    aot_frame->max_local_cell_num = max_local_cell_num;
    aot_frame->max_stack_cell_num = max_stack_cell_num;

    aot_frame->sp = aot_frame->lp + max_local_cell_num;

    /* Init the frame_sp_begin of the function block */
    block->frame_sp_begin = aot_frame->sp;

    n = 0;

    /* Set all params dirty since they were set to llvm value but
       haven't been committed to the AOT/JIT stack frame */
    for (i = 0; i < func_type->param_count; i++) {
        local_type = func_type->types[i];
        local_value = LLVMGetParam(func_ctx->func, i + 1);

        switch (local_type) {
            case VALUE_TYPE_I32:
                set_local_i32(comp_ctx->aot_frame, n, local_value);
                n++;
                break;
            case VALUE_TYPE_I64:
                set_local_i64(comp_ctx->aot_frame, n, local_value);
                n += 2;
                break;
            case VALUE_TYPE_F32:
                set_local_f32(comp_ctx->aot_frame, n, local_value);
                n++;
                break;
            case VALUE_TYPE_F64:
                set_local_f64(comp_ctx->aot_frame, n, local_value);
                n += 2;
                break;
            case VALUE_TYPE_V128:
                set_local_v128(comp_ctx->aot_frame, n, local_value);
                n += 4;
                break;
            case VALUE_TYPE_FUNCREF:
            case VALUE_TYPE_EXTERNREF:
                set_local_ref(comp_ctx->aot_frame, n, local_value, local_type);
                n++;
                break;
            default:
                bh_assert(0);
                break;
        }
    }

    /* Set all locals dirty since they were set to llvm value but
       haven't been committed to the AOT/JIT stack frame */
    for (i = 0; i < aot_func->local_count; i++) {
        local_type = aot_func->local_types[i];

        switch (local_type) {
            case VALUE_TYPE_I32:
                set_local_i32(comp_ctx->aot_frame, n, I32_ZERO);
                n++;
                break;
            case VALUE_TYPE_I64:
                set_local_i64(comp_ctx->aot_frame, n, I64_ZERO);
                n += 2;
                break;
            case VALUE_TYPE_F32:
                set_local_f32(comp_ctx->aot_frame, n, F32_ZERO);
                n++;
                break;
            case VALUE_TYPE_F64:
                set_local_f64(comp_ctx->aot_frame, n, F64_ZERO);
                n += 2;
                break;
            case VALUE_TYPE_V128:
                set_local_v128(comp_ctx->aot_frame, n, V128_f64x2_ZERO);
                n += 4;
                break;
            case VALUE_TYPE_FUNCREF:
            case VALUE_TYPE_EXTERNREF:
                set_local_ref(comp_ctx->aot_frame, n, I32_ZERO, local_type);
                n++;
                break;
            default:
                bh_assert(0);
                break;
        }
    }

    return true;
}

bool
aot_gen_checkpoint(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                   const uint8 *frame_ip)
{
    comp_ctx->inst_checkpointed = true;

    bool disable_commit = comp_ctx->checkpoint_type != 0 && comp_ctx->exp_disable_commit_sp_ip;
    bool disable_restore_jump = comp_ctx->checkpoint_type != 0 && comp_ctx->exp_disable_restore_jump;
    bool disable_gen_fence_int3 = comp_ctx->checkpoint_type != 0 && comp_ctx->exp_disable_gen_fence_int3;

    if (disable_commit) {
    } else {
        if (!aot_gen_commit_sp_ip(comp_ctx->aot_frame, comp_ctx->aot_frame->sp,
                                frame_ip))
            return false;
        if (!aot_gen_commit_values(comp_ctx->aot_frame, false))
            return false;
    }
    if (disable_gen_fence_int3) {
    } else {
        if (!aot_compile_emit_fence_nop(comp_ctx, func_ctx))
            return false;
    }

    if (disable_restore_jump) {
    } else {
        char name[32];
        LLVMBasicBlockRef block_restore_jump, block_restore_value;

        snprintf(name, sizeof(name), "restore-%zu",
                (uint64)(uintptr_t)(frame_ip - func_ctx->aot_func->code));
        if (!(block_restore_value = LLVMAppendBasicBlockInContext(
                comp_ctx->context, func_ctx->func, name))) {
            aot_set_last_error("add LLVM basic block failed.");
            return false;
        }

        snprintf(name, sizeof(name), "restore-jump-%zu",
                (uint64)(uintptr_t)(frame_ip - func_ctx->aot_func->code));
        if (!(block_restore_jump = LLVMAppendBasicBlockInContext(
                comp_ctx->context, func_ctx->func, name))) {
            aot_set_last_error("add LLVM basic block failed.");
            return false;
        }

        LLVMMoveBasicBlockAfter(block_restore_value,
                                LLVMGetInsertBlock(comp_ctx->aot_frame_alloca_builder));
        LLVMMoveBasicBlockAfter(block_restore_jump, LLVMGetInsertBlock(comp_ctx->builder));

        if (!LLVMBuildBr(comp_ctx->builder, block_restore_jump)) {
            aot_set_last_error("llvm build br failed.");
            return false;
        }

        LLVMValueRef ip_offset;
        if (comp_ctx->pointer_size == sizeof(uint64))
            ip_offset =
                I64_CONST((uint64)(uintptr_t)(frame_ip - func_ctx->aot_func->code));
        else
            ip_offset =
                I32_CONST((uint32)(uintptr_t)(frame_ip - func_ctx->aot_func->code));
        LLVMAddCase(func_ctx->restore_switch, ip_offset, block_restore_value);

        LLVMPositionBuilderAtEnd(comp_ctx->builder, block_restore_value);
        if (!aot_gen_restore_values(comp_ctx->aot_frame))
            return false;
        LLVMBuildBr(comp_ctx->builder, block_restore_jump);

        LLVMPositionBuilderAtEnd(comp_ctx->builder, block_restore_jump);
    }

    return true;
}

static bool pgo_skip_loop(const char *aot_file_name, uint32 func_idx, uint64 ip) {
    static uint32* func_list = NULL;
    static uint64* ip_list = NULL;
    static int n = -1;
    if (n == -1) {
        n = 0;
        // f"{aot_file_name}.pgo"
        char *pgo_file_name = (char*)malloc(strlen(aot_file_name) + 5);
        strcpy(pgo_file_name, aot_file_name);
        strcat(pgo_file_name, ".pgo");
        FILE* f = fopen(pgo_file_name, "r");
        if (!f) {
            return false;
        }
        fscanf(f, "%d", &n);
        func_list = (uint32*)malloc(n * sizeof(uint32));
        ip_list = (uint64*)malloc(n * sizeof(uint64));
        for (int i = 0; i < n; i++) {
            fscanf(f, "%d %lu", &func_list[i], &ip_list[i]);
        }
        free(pgo_file_name);
    }
    for (int i = 0; i < n; i++) {
        if (func_list[i] == func_idx && ip_list[i] == ip) {
            return true;
        }
    }
    return false;
}

static bool
aot_compile_func(AOTCompContext *comp_ctx, uint32 func_index)
{
    AOTFuncContext *func_ctx = comp_ctx->func_ctxes[func_index];
    uint8 *frame_ip = func_ctx->aot_func->code, opcode, *p_f32, *p_f64;
    uint8 *frame_ip_end = frame_ip + func_ctx->aot_func->code_size;
    uint8 *param_types = NULL;
    uint8 *result_types = NULL;
    uint8 value_type;
    uint16 param_count;
    uint16 result_count;
    uint32 br_depth, *br_depths, br_count;
    uint32 func_idx, type_idx, mem_idx, local_idx, global_idx, i;
    uint32 bytes = 4, align, offset;
    uint32 type_index;
    bool sign = true;
    int32 i32_const;
    int64 i64_const;
    float32 f32_const;
    float64 f64_const;
    AOTFuncType *func_type = NULL;
    bool last_op_is_loop = false;
    LLVMValueRef last_loop_counter = NULL;
#if WASM_ENABLE_DEBUG_AOT != 0
    LLVMMetadataRef location;
#endif

    if (comp_ctx->enable_aux_stack_frame) {
        if (!init_comp_frame(comp_ctx, func_ctx, func_index)) {
            return false;
        }
    }

    /* Start to translate the opcodes */
    LLVMPositionBuilderAtEnd(
        comp_ctx->builder,
        func_ctx->block_stack.block_list_head->llvm_entry_block);

    {
        if (comp_ctx->aot_frame) {
            uint32 offset_ip = offsetof(AOTFrame, ip_offset);
            LLVMValueRef cur_frame = func_ctx->cur_frame;
            LLVMValueRef value_offset, value_addr, value_ptr, value;
            bool is_64bit =
                (comp_ctx->pointer_size == sizeof(uint64)) ? true : false;
            if (!(value_offset = I32_CONST(offset_ip))) {
                aot_set_last_error("llvm build const failed");
                return false;
            }

            if (!(value_addr = LLVMBuildInBoundsGEP2(
                      comp_ctx->builder, INT8_TYPE, cur_frame, &value_offset, 1,
                      "ip_addr"))) {
                aot_set_last_error("llvm build in bounds gep failed");
                return false;
            }

            if (!(value_ptr = LLVMBuildBitCast(
                      comp_ctx->builder, value_addr,
                      is_64bit ? INT64_PTR_TYPE : INT32_PTR_TYPE, "ip_ptr"))) {
                aot_set_last_error("llvm build bit cast failed");
                return false;
            }

            if (!(value = LLVMBuildLoad2(comp_ctx->builder,
                                         is_64bit ? I64_TYPE : I32_TYPE,
                                         value_ptr, "init_ip"))) {
                aot_set_last_error("llvm build load failed");
                return false;
            }

            LLVMBasicBlockRef normal_block;
            char name[32];
            snprintf(name, sizeof(name), "restore-no_restore");
            if (!(normal_block = LLVMAppendBasicBlockInContext(
                      comp_ctx->context, func_ctx->func, name))) {
                aot_set_last_error("add LLVM basic block failed.");
                goto fail;
            }
            LLVMMoveBasicBlockAfter(normal_block,
                                    LLVMGetInsertBlock(comp_ctx->builder));
            func_ctx->restore_switch =
                LLVMBuildSwitch(comp_ctx->builder, value, normal_block, 0);
            LLVMPositionBuilderAtEnd(comp_ctx->builder, normal_block);

            LLVMPositionBuilderBefore(comp_ctx->aot_frame_alloca_builder,
                                      func_ctx->restore_switch);
        }
    }

    while (frame_ip < frame_ip_end) {
        // fprintf(stderr, "%p\n", (void*)comp_ctx->aot_frame);
        comp_ctx->inst_checkpointed = false;
        opcode = *frame_ip++;

        if (comp_ctx->enable_every_checkpoint) {
            bh_assert(comp_ctx->aot_frame);
            comp_ctx->checkpoint_type = 3;
            aot_gen_checkpoint(comp_ctx, func_ctx, frame_ip);
        }
        if (comp_ctx->enable_loop_checkpoint && last_op_is_loop) {
            bh_assert(comp_ctx->aot_frame);

            uint64 ip_offset = (uint64)(uintptr_t)(frame_ip - func_ctx->aot_func->code);
            bool skip_loop = pgo_skip_loop(comp_ctx->aot_file_name, func_index, ip_offset);
            if (skip_loop && comp_ctx->enable_checkpoint_pgo) {
                fprintf(stderr, "skip loop %zu\n", ip_offset);
            } else {
                if (comp_ctx->enable_counter_loop_checkpoint) {
                    // counter = counter + 1
                    LLVMValueRef counter = LLVMBuildLoad2(
                        comp_ctx->builder, I32_TYPE, last_loop_counter, "counter");
                    LLVMValueRef counter_inc = LLVMBuildAdd(
                        comp_ctx->builder, counter,  I32_CONST(1), "counter_inc");
                    LLVMBuildStore(comp_ctx->builder, counter_inc, last_loop_counter);
                    const int threshold = 1 << 20;

                    LLVMBasicBlockRef normal_block, ckpt_block;
                    char name[32];
                    snprintf(name, sizeof(name), "loop-ckpt-%zu",
                            (uint64)(uintptr_t)(frame_ip - 1 - func_ctx->aot_func->code));
                    if (!(ckpt_block = LLVMAppendBasicBlockInContext(
                            comp_ctx->context, func_ctx->func, name))) {
                        aot_set_last_error("add LLVM basic block failed.");
                        goto fail;
                    }
                    LLVMMoveBasicBlockAfter(ckpt_block,
                                            LLVMGetInsertBlock(comp_ctx->builder));
                    
                    snprintf(name, sizeof(name), "loop-normal-%zu",
                                (uint64)(uintptr_t)(frame_ip - 1 - func_ctx->aot_func->code));
                    if (!(normal_block = LLVMAppendBasicBlockInContext(
                            comp_ctx->context, func_ctx->func, name))) {
                        aot_set_last_error("add LLVM basic block failed.");
                        goto fail;
                    }

                    LLVMValueRef andvar = LLVMBuildAnd(comp_ctx->builder, counter, I32_CONST(threshold-1), "andvar");
                    LLVMValueRef cond = LLVMBuildICmp(comp_ctx->builder, LLVMIntEQ, andvar, I32_CONST(0), "cond");

                    LLVMBuildCondBr(comp_ctx->builder, cond, ckpt_block, normal_block);
                    LLVMPositionBuilderAtEnd(comp_ctx->builder, ckpt_block);

                    // TODO(huab): find a better way to commit locals
                    aot_gen_commit_all_locals(comp_ctx->aot_frame);

                    // checkpoint
                    comp_ctx->checkpoint_type = 1;
                    aot_gen_checkpoint(comp_ctx, func_ctx, frame_ip);

                    LLVMBuildBr(comp_ctx->builder, normal_block);

                    // normal
                    LLVMMoveBasicBlockAfter(normal_block, LLVMGetInsertBlock(comp_ctx->builder));
                    LLVMPositionBuilderAtEnd(comp_ctx->builder, normal_block);
                } else {
                    comp_ctx->checkpoint_type = 1;
                    aot_gen_checkpoint(comp_ctx, func_ctx, frame_ip);
                }
            }

            last_op_is_loop = false;
        }

#if WASM_ENABLE_DEBUG_AOT != 0
        location = dwarf_gen_location(
            comp_ctx, func_ctx,
            (frame_ip - 1) - comp_ctx->comp_data->wasm_module->buf_code);
        LLVMSetCurrentDebugLocation2(comp_ctx->builder, location);
#endif

        switch (opcode) {
            case WASM_OP_UNREACHABLE:
                if (!aot_compile_op_unreachable(comp_ctx, func_ctx, &frame_ip))
                    return false;
                break;

            case WASM_OP_NOP:
                break;

            case WASM_OP_BLOCK:
            case WASM_OP_LOOP:
            {
                if (opcode == WASM_OP_LOOP) {
                    last_op_is_loop = true;
                }
                if (comp_ctx->enable_loop_checkpoint) {
                    if (comp_ctx->exp_disable_stack_commit_before_block) {
                        fake_aot_gen_commit_values(comp_ctx->aot_frame, true);
                    } else {
                        aot_gen_commit_values(comp_ctx->aot_frame, true);
                    }
                    if (comp_ctx->enable_counter_loop_checkpoint) {
                        last_loop_counter = LLVMBuildAlloca(comp_ctx->aot_frame_alloca_builder,
                            I32_TYPE, "wasm_loop_ckpt_counter");
                        LLVMBuildStore(comp_ctx->builder, I32_ZERO, last_loop_counter);
                    }
                }
                value_type = *frame_ip++;
                if (value_type == VALUE_TYPE_I32 || value_type == VALUE_TYPE_I64
                    || value_type == VALUE_TYPE_F32
                    || value_type == VALUE_TYPE_F64
                    || value_type == VALUE_TYPE_V128
                    || value_type == VALUE_TYPE_VOID
                    || value_type == VALUE_TYPE_FUNCREF
                    || value_type == VALUE_TYPE_EXTERNREF) {
                    param_count = 0;
                    param_types = NULL;
                    if (value_type == VALUE_TYPE_VOID) {
                        result_count = 0;
                        result_types = NULL;
                    }
                    else {
                        result_count = 1;
                        result_types = &value_type;
                    }
                }
                else {
                    frame_ip--;
                    read_leb_uint32(frame_ip, frame_ip_end, type_index);
                    func_type = comp_ctx->comp_data->func_types[type_index];
                    param_count = func_type->param_count;
                    param_types = func_type->types;
                    result_count = func_type->result_count;
                    result_types = func_type->types + param_count;
                }
                // generate the stable mapping to the register
                if (!aot_compile_op_block(
                        comp_ctx, func_ctx, &frame_ip, frame_ip_end,
                        (uint32)(LABEL_TYPE_BLOCK + opcode - WASM_OP_BLOCK),
                        param_count, param_types, result_count, result_types))
                    return false;
                break;
            }

            case WASM_OP_IF:
            case EXT_OP_BLOCK:
            case EXT_OP_LOOP:
            case EXT_OP_IF:
            case WASM_OP_ELSE:
                aot_set_last_error("encounter opcode without aot csp support.");
                goto fail;
                break;

            case WASM_OP_END:
                if (!aot_compile_op_end(comp_ctx, func_ctx, &frame_ip))
                    return false;
                break;

            case WASM_OP_BR:
                if (comp_ctx->enable_checkpoint && !comp_ctx->inst_checkpointed) {
                    if (comp_ctx->enable_br_checkpoint) {
                        comp_ctx->checkpoint_type = 2;
                        aot_gen_checkpoint(comp_ctx, func_ctx, frame_ip);
                    } else {
                        if (comp_ctx->enable_aux_stack_dirty_bit) {
                            aot_gen_commit_values(comp_ctx->aot_frame, false);
                        }
                    }
                }

                read_leb_uint32(frame_ip, frame_ip_end, br_depth);
                if (!aot_compile_op_br(comp_ctx, func_ctx, br_depth, &frame_ip))
                    return false;
                break;

            case WASM_OP_BR_IF:
                if (comp_ctx->enable_checkpoint && !comp_ctx->inst_checkpointed) {
                    if (comp_ctx->enable_br_checkpoint) {
                        comp_ctx->checkpoint_type = 2;
                        aot_gen_checkpoint(comp_ctx, func_ctx, frame_ip);
                    } else {
                        if (comp_ctx->enable_aux_stack_dirty_bit) {
                            aot_gen_commit_values(comp_ctx->aot_frame, false);
                        }
                    }
                }

                read_leb_uint32(frame_ip, frame_ip_end, br_depth);
                if (!aot_compile_op_br_if(comp_ctx, func_ctx, br_depth,
                                          &frame_ip))
                    return false;
                break;

            case WASM_OP_BR_TABLE:
                if (comp_ctx->enable_checkpoint && !comp_ctx->inst_checkpointed) {
                    if (comp_ctx->enable_br_checkpoint) {
                        comp_ctx->checkpoint_type = 2;
                        aot_gen_checkpoint(comp_ctx, func_ctx, frame_ip);
                    } else {
                        if (comp_ctx->enable_aux_stack_dirty_bit) {
                            aot_gen_commit_values(comp_ctx->aot_frame, false);
                        }
                    }
                }

                read_leb_uint32(frame_ip, frame_ip_end, br_count);
                if (!(br_depths = wasm_runtime_malloc((uint32)sizeof(uint32)
                                                      * (br_count + 1)))) {
                    aot_set_last_error("allocate memory failed.");
                    goto fail;
                }
#if WASM_ENABLE_FAST_INTERP != 0
                for (i = 0; i <= br_count; i++)
                    read_leb_uint32(frame_ip, frame_ip_end, br_depths[i]);
#else
                for (i = 0; i <= br_count; i++)
                    br_depths[i] = *frame_ip++;
#endif

                if (!aot_compile_op_br_table(comp_ctx, func_ctx, br_depths,
                                             br_count, &frame_ip)) {
                    wasm_runtime_free(br_depths);
                    return false;
                }

                wasm_runtime_free(br_depths);
                break;

#if WASM_ENABLE_FAST_INTERP == 0
            case EXT_OP_BR_TABLE_CACHE:
            {
                if (comp_ctx->enable_checkpoint && !comp_ctx->inst_checkpointed) {
                    if (comp_ctx->enable_br_checkpoint) {
                        comp_ctx->checkpoint_type = 2;
                        aot_gen_checkpoint(comp_ctx, func_ctx, frame_ip);
                    } else {
                        if (comp_ctx->enable_aux_stack_dirty_bit) {
                            aot_gen_commit_values(comp_ctx->aot_frame, false);
                        }
                    }
                }

                BrTableCache *node = bh_list_first_elem(
                    comp_ctx->comp_data->wasm_module->br_table_cache_list);
                BrTableCache *node_next;
                uint8 *p_opcode = frame_ip - 1;

                read_leb_uint32(frame_ip, frame_ip_end, br_count);

                while (node) {
                    node_next = bh_list_elem_next(node);
                    if (node->br_table_op_addr == p_opcode) {
                        br_depths = node->br_depths;
                        if (!aot_compile_op_br_table(comp_ctx, func_ctx,
                                                     br_depths, br_count,
                                                     &frame_ip)) {
                            return false;
                        }
                        break;
                    }
                    node = node_next;
                }
                bh_assert(node);

                break;
            }
#endif

            case WASM_OP_RETURN:
                if (!aot_compile_op_return(comp_ctx, func_ctx, &frame_ip))
                    return false;
                break;

            case WASM_OP_CALL:
            {
                uint8 *frame_ip_org = frame_ip;
                if (comp_ctx->enable_checkpoint && !comp_ctx->inst_checkpointed) {
                    bh_assert(comp_ctx->aot_frame);
                    comp_ctx->checkpoint_type = 0;
                    aot_gen_checkpoint(comp_ctx, func_ctx, frame_ip_org);
                }

                read_leb_uint32(frame_ip, frame_ip_end, func_idx);
                if (!aot_compile_op_call(comp_ctx, func_ctx, func_idx, false,
                                         frame_ip_org))
                    return false;
                break;
            }

            case WASM_OP_CALL_INDIRECT:
            {
                uint8 *frame_ip_org = frame_ip;
                uint32 tbl_idx;

                if (comp_ctx->enable_checkpoint && !comp_ctx->inst_checkpointed) {
                    bh_assert(comp_ctx->aot_frame);
                    comp_ctx->checkpoint_type = 0;
                    aot_gen_checkpoint(comp_ctx, func_ctx, frame_ip_org);
                }

                read_leb_uint32(frame_ip, frame_ip_end, type_idx);

#if WASM_ENABLE_REF_TYPES != 0
                if (comp_ctx->enable_ref_types) {
                    read_leb_uint32(frame_ip, frame_ip_end, tbl_idx);
                }
                else
#endif
                {
                    frame_ip++;
                    tbl_idx = 0;
                }

                if (!aot_compile_op_call_indirect(comp_ctx, func_ctx, type_idx,
                                                  tbl_idx, frame_ip_org))
                    return false;
                break;
            }

#if WASM_ENABLE_TAIL_CALL != 0
            case WASM_OP_RETURN_CALL:
            {
                uint8 *frame_ip_org = frame_ip;

                if (!comp_ctx->enable_tail_call) {
                    aot_set_last_error("unsupported opcode");
                    return false;
                }
                read_leb_uint32(frame_ip, frame_ip_end, func_idx);
                if (!aot_compile_op_call(comp_ctx, func_ctx, func_idx, true,
                                         frame_ip_org))
                    return false;
                if (!aot_compile_op_return(comp_ctx, func_ctx, &frame_ip))
                    return false;
                break;
            }

            case WASM_OP_RETURN_CALL_INDIRECT:
            {
                uint8 *frame_ip_org = frame_ip;
                uint32 tbl_idx;

                if (!comp_ctx->enable_tail_call) {
                    aot_set_last_error("unsupported opcode");
                    return false;
                }

                read_leb_uint32(frame_ip, frame_ip_end, type_idx);
#if WASM_ENABLE_REF_TYPES != 0
                if (comp_ctx->enable_ref_types) {
                    read_leb_uint32(frame_ip, frame_ip_end, tbl_idx);
                }
                else
#endif
                {
                    frame_ip++;
                    tbl_idx = 0;
                }

                if (!aot_compile_op_call_indirect(comp_ctx, func_ctx, type_idx,
                                                  tbl_idx, frame_ip_org))
                    return false;
                if (!aot_compile_op_return(comp_ctx, func_ctx, &frame_ip))
                    return false;
                break;
            }
#endif /* end of WASM_ENABLE_TAIL_CALL */

            case WASM_OP_DROP:
                if (!aot_compile_op_drop(comp_ctx, func_ctx, true))
                    return false;
                break;

            case WASM_OP_DROP_64:
                if (!aot_compile_op_drop(comp_ctx, func_ctx, false))
                    return false;
                break;

            case WASM_OP_SELECT:
                if (!aot_compile_op_select(comp_ctx, func_ctx, true))
                    return false;
                break;

            case WASM_OP_SELECT_64:
                if (!aot_compile_op_select(comp_ctx, func_ctx, false))
                    return false;
                break;

#if WASM_ENABLE_REF_TYPES != 0
            case WASM_OP_SELECT_T:
            {
                uint32 vec_len;

                if (!comp_ctx->enable_ref_types) {
                    goto unsupport_ref_types;
                }

                read_leb_uint32(frame_ip, frame_ip_end, vec_len);
                bh_assert(vec_len == 1);
                (void)vec_len;

                type_idx = *frame_ip++;
                if (!aot_compile_op_select(comp_ctx, func_ctx,
                                           (type_idx != VALUE_TYPE_I64)
                                               && (type_idx != VALUE_TYPE_F64)))
                    return false;
                break;
            }
            case WASM_OP_TABLE_GET:
            {
                uint32 tbl_idx;

                if (!comp_ctx->enable_ref_types) {
                    goto unsupport_ref_types;
                }

                read_leb_uint32(frame_ip, frame_ip_end, tbl_idx);
                if (!aot_compile_op_table_get(comp_ctx, func_ctx, tbl_idx))
                    return false;
                break;
            }
            case WASM_OP_TABLE_SET:
            {
                uint32 tbl_idx;

                if (!comp_ctx->enable_ref_types) {
                    goto unsupport_ref_types;
                }

                read_leb_uint32(frame_ip, frame_ip_end, tbl_idx);
                if (!aot_compile_op_table_set(comp_ctx, func_ctx, tbl_idx))
                    return false;
                break;
            }
            case WASM_OP_REF_NULL:
            {
                uint32 type;

                if (!comp_ctx->enable_ref_types) {
                    goto unsupport_ref_types;
                }

                read_leb_uint32(frame_ip, frame_ip_end, type);

                if (!aot_compile_op_ref_null(comp_ctx, func_ctx))
                    return false;

                (void)type;
                break;
            }
            case WASM_OP_REF_IS_NULL:
            {
                if (!comp_ctx->enable_ref_types) {
                    goto unsupport_ref_types;
                }

                if (!aot_compile_op_ref_is_null(comp_ctx, func_ctx))
                    return false;
                break;
            }
            case WASM_OP_REF_FUNC:
            {
                if (!comp_ctx->enable_ref_types) {
                    goto unsupport_ref_types;
                }

                read_leb_uint32(frame_ip, frame_ip_end, func_idx);
                if (!aot_compile_op_ref_func(comp_ctx, func_ctx, func_idx))
                    return false;
                break;
            }
#endif

            case WASM_OP_GET_LOCAL:
                read_leb_uint32(frame_ip, frame_ip_end, local_idx);
                if (!aot_compile_op_get_local(comp_ctx, func_ctx, local_idx))
                    return false;
                break;

            case WASM_OP_SET_LOCAL:
                read_leb_uint32(frame_ip, frame_ip_end, local_idx);
                if (!aot_compile_op_set_local(comp_ctx, func_ctx, local_idx))
                    return false;
                if (comp_ctx->enable_loop_checkpoint && !comp_ctx->enable_aux_stack_dirty_bit) {
                    if (comp_ctx->enable_counter_loop_checkpoint) {
                    } else {
                        AOTValueSlot* p = comp_ctx->aot_frame->lp + comp_ctx->aot_frame->cur_wasm_func->local_offsets[local_idx];
                        if (comp_ctx->exp_disable_local_commit) {
                            fake_aot_gen_commit_value(comp_ctx->aot_frame, true, &p, local_idx);
                        } else {
                            if (!aot_gen_commit_value(comp_ctx->aot_frame, true, &p, local_idx))
                                return false;
                        }
                    }
                }
                break;

            case WASM_OP_TEE_LOCAL:
                read_leb_uint32(frame_ip, frame_ip_end, local_idx);
                if (!aot_compile_op_tee_local(comp_ctx, func_ctx, local_idx))
                    return false;
                if (comp_ctx->enable_loop_checkpoint && !comp_ctx->enable_aux_stack_dirty_bit) {
                    if (comp_ctx->enable_counter_loop_checkpoint) {
                    } else {
                        AOTValueSlot* p = comp_ctx->aot_frame->lp + comp_ctx->aot_frame->cur_wasm_func->local_offsets[local_idx];
                        if (comp_ctx->exp_disable_local_commit) {
                            fake_aot_gen_commit_value(comp_ctx->aot_frame, true, &p, local_idx);
                        } else {
                            if (!aot_gen_commit_value(comp_ctx->aot_frame, true, &p, local_idx))
                                return false;
                        }
                    }
                }
                break;

            case WASM_OP_GET_GLOBAL:
            case WASM_OP_GET_GLOBAL_64:
                read_leb_uint32(frame_ip, frame_ip_end, global_idx);
                if (!aot_compile_op_get_global(comp_ctx, func_ctx, global_idx))
                    return false;
                break;

            case WASM_OP_SET_GLOBAL:
            case WASM_OP_SET_GLOBAL_64:
            case WASM_OP_SET_GLOBAL_AUX_STACK:
                read_leb_uint32(frame_ip, frame_ip_end, global_idx);
                if (!aot_compile_op_set_global(
                        comp_ctx, func_ctx, global_idx,
                        opcode == WASM_OP_SET_GLOBAL_AUX_STACK ? true : false))
                    return false;
                break;

            case WASM_OP_I32_LOAD:
                bytes = 4;
                sign = true;
                goto op_i32_load;
            case WASM_OP_I32_LOAD8_S:
            case WASM_OP_I32_LOAD8_U:
                bytes = 1;
                sign = (opcode == WASM_OP_I32_LOAD8_S) ? true : false;
                goto op_i32_load;
            case WASM_OP_I32_LOAD16_S:
            case WASM_OP_I32_LOAD16_U:
                bytes = 2;
                sign = (opcode == WASM_OP_I32_LOAD16_S) ? true : false;
            op_i32_load:
                read_leb_uint32(frame_ip, frame_ip_end, align);
                read_leb_uint32(frame_ip, frame_ip_end, offset);
                if (!aot_compile_op_i32_load(comp_ctx, func_ctx, align, offset,
                                             bytes, sign, false))
                    return false;
                break;

            case WASM_OP_I64_LOAD:
                bytes = 8;
                sign = true;
                goto op_i64_load;
            case WASM_OP_I64_LOAD8_S:
            case WASM_OP_I64_LOAD8_U:
                bytes = 1;
                sign = (opcode == WASM_OP_I64_LOAD8_S) ? true : false;
                goto op_i64_load;
            case WASM_OP_I64_LOAD16_S:
            case WASM_OP_I64_LOAD16_U:
                bytes = 2;
                sign = (opcode == WASM_OP_I64_LOAD16_S) ? true : false;
                goto op_i64_load;
            case WASM_OP_I64_LOAD32_S:
            case WASM_OP_I64_LOAD32_U:
                bytes = 4;
                sign = (opcode == WASM_OP_I64_LOAD32_S) ? true : false;
            op_i64_load:
                read_leb_uint32(frame_ip, frame_ip_end, align);
                read_leb_uint32(frame_ip, frame_ip_end, offset);
                if (!aot_compile_op_i64_load(comp_ctx, func_ctx, align, offset,
                                             bytes, sign, false))
                    return false;
                break;

            case WASM_OP_F32_LOAD:
                read_leb_uint32(frame_ip, frame_ip_end, align);
                read_leb_uint32(frame_ip, frame_ip_end, offset);
                if (!aot_compile_op_f32_load(comp_ctx, func_ctx, align, offset))
                    return false;
                break;

            case WASM_OP_F64_LOAD:
                read_leb_uint32(frame_ip, frame_ip_end, align);
                read_leb_uint32(frame_ip, frame_ip_end, offset);
                if (!aot_compile_op_f64_load(comp_ctx, func_ctx, align, offset))
                    return false;
                break;

            case WASM_OP_I32_STORE:
                bytes = 4;
                goto op_i32_store;
            case WASM_OP_I32_STORE8:
                bytes = 1;
                goto op_i32_store;
            case WASM_OP_I32_STORE16:
                bytes = 2;
            op_i32_store:
                read_leb_uint32(frame_ip, frame_ip_end, align);
                read_leb_uint32(frame_ip, frame_ip_end, offset);
                if (!aot_compile_op_i32_store(comp_ctx, func_ctx, align, offset,
                                              bytes, false))
                    return false;
                break;

            case WASM_OP_I64_STORE:
                bytes = 8;
                goto op_i64_store;
            case WASM_OP_I64_STORE8:
                bytes = 1;
                goto op_i64_store;
            case WASM_OP_I64_STORE16:
                bytes = 2;
                goto op_i64_store;
            case WASM_OP_I64_STORE32:
                bytes = 4;
            op_i64_store:
                read_leb_uint32(frame_ip, frame_ip_end, align);
                read_leb_uint32(frame_ip, frame_ip_end, offset);
                if (!aot_compile_op_i64_store(comp_ctx, func_ctx, align, offset,
                                              bytes, false))
                    return false;
                break;

            case WASM_OP_F32_STORE:
                read_leb_uint32(frame_ip, frame_ip_end, align);
                read_leb_uint32(frame_ip, frame_ip_end, offset);
                if (!aot_compile_op_f32_store(comp_ctx, func_ctx, align,
                                              offset))
                    return false;
                break;

            case WASM_OP_F64_STORE:
                read_leb_uint32(frame_ip, frame_ip_end, align);
                read_leb_uint32(frame_ip, frame_ip_end, offset);
                if (!aot_compile_op_f64_store(comp_ctx, func_ctx, align,
                                              offset))
                    return false;
                break;

            case WASM_OP_MEMORY_SIZE:
                read_leb_uint32(frame_ip, frame_ip_end, mem_idx);
                if (!aot_compile_op_memory_size(comp_ctx, func_ctx))
                    return false;
                (void)mem_idx;
                break;

            case WASM_OP_MEMORY_GROW:
                read_leb_uint32(frame_ip, frame_ip_end, mem_idx);
                if (!aot_compile_op_memory_grow(comp_ctx, func_ctx))
                    return false;
                break;

            case WASM_OP_I32_CONST:
                read_leb_int32(frame_ip, frame_ip_end, i32_const);
                if (!aot_compile_op_i32_const(comp_ctx, func_ctx, i32_const))
                    return false;
                break;

            case WASM_OP_I64_CONST:
                read_leb_int64(frame_ip, frame_ip_end, i64_const);
                if (!aot_compile_op_i64_const(comp_ctx, func_ctx, i64_const))
                    return false;
                break;

            case WASM_OP_F32_CONST:
                p_f32 = (uint8 *)&f32_const;
                for (i = 0; i < sizeof(float32); i++)
                    *p_f32++ = *frame_ip++;
                if (!aot_compile_op_f32_const(comp_ctx, func_ctx, f32_const))
                    return false;
                break;

            case WASM_OP_F64_CONST:
                p_f64 = (uint8 *)&f64_const;
                for (i = 0; i < sizeof(float64); i++)
                    *p_f64++ = *frame_ip++;
                if (!aot_compile_op_f64_const(comp_ctx, func_ctx, f64_const))
                    return false;
                break;

            case WASM_OP_I32_EQZ:
            case WASM_OP_I32_EQ:
            case WASM_OP_I32_NE:
            case WASM_OP_I32_LT_S:
            case WASM_OP_I32_LT_U:
            case WASM_OP_I32_GT_S:
            case WASM_OP_I32_GT_U:
            case WASM_OP_I32_LE_S:
            case WASM_OP_I32_LE_U:
            case WASM_OP_I32_GE_S:
            case WASM_OP_I32_GE_U:
                if (!aot_compile_op_i32_compare(
                        comp_ctx, func_ctx, INT_EQZ + opcode - WASM_OP_I32_EQZ))
                    return false;
                break;

            case WASM_OP_I64_EQZ:
            case WASM_OP_I64_EQ:
            case WASM_OP_I64_NE:
            case WASM_OP_I64_LT_S:
            case WASM_OP_I64_LT_U:
            case WASM_OP_I64_GT_S:
            case WASM_OP_I64_GT_U:
            case WASM_OP_I64_LE_S:
            case WASM_OP_I64_LE_U:
            case WASM_OP_I64_GE_S:
            case WASM_OP_I64_GE_U:
                if (!aot_compile_op_i64_compare(
                        comp_ctx, func_ctx, INT_EQZ + opcode - WASM_OP_I64_EQZ))
                    return false;
                break;

            case WASM_OP_F32_EQ:
            case WASM_OP_F32_NE:
            case WASM_OP_F32_LT:
            case WASM_OP_F32_GT:
            case WASM_OP_F32_LE:
            case WASM_OP_F32_GE:
                if (!aot_compile_op_f32_compare(
                        comp_ctx, func_ctx, FLOAT_EQ + opcode - WASM_OP_F32_EQ))
                    return false;
                break;

            case WASM_OP_F64_EQ:
            case WASM_OP_F64_NE:
            case WASM_OP_F64_LT:
            case WASM_OP_F64_GT:
            case WASM_OP_F64_LE:
            case WASM_OP_F64_GE:
                if (!aot_compile_op_f64_compare(
                        comp_ctx, func_ctx, FLOAT_EQ + opcode - WASM_OP_F64_EQ))
                    return false;
                break;

            case WASM_OP_I32_CLZ:
                if (!aot_compile_op_i32_clz(comp_ctx, func_ctx))
                    return false;
                break;

            case WASM_OP_I32_CTZ:
                if (!aot_compile_op_i32_ctz(comp_ctx, func_ctx))
                    return false;
                break;

            case WASM_OP_I32_POPCNT:
                if (!aot_compile_op_i32_popcnt(comp_ctx, func_ctx))
                    return false;
                break;

            case WASM_OP_I32_ADD:
            case WASM_OP_I32_SUB:
            case WASM_OP_I32_MUL:
            case WASM_OP_I32_DIV_S:
            case WASM_OP_I32_DIV_U:
            case WASM_OP_I32_REM_S:
            case WASM_OP_I32_REM_U:
                if (!aot_compile_op_i32_arithmetic(
                        comp_ctx, func_ctx, INT_ADD + opcode - WASM_OP_I32_ADD,
                        &frame_ip))
                    return false;
                break;

            case WASM_OP_I32_AND:
            case WASM_OP_I32_OR:
            case WASM_OP_I32_XOR:
                if (!aot_compile_op_i32_bitwise(
                        comp_ctx, func_ctx, INT_SHL + opcode - WASM_OP_I32_AND))
                    return false;
                break;

            case WASM_OP_I32_SHL:
            case WASM_OP_I32_SHR_S:
            case WASM_OP_I32_SHR_U:
            case WASM_OP_I32_ROTL:
            case WASM_OP_I32_ROTR:
                if (!aot_compile_op_i32_shift(
                        comp_ctx, func_ctx, INT_SHL + opcode - WASM_OP_I32_SHL))
                    return false;
                break;

            case WASM_OP_I64_CLZ:
                if (!aot_compile_op_i64_clz(comp_ctx, func_ctx))
                    return false;
                break;

            case WASM_OP_I64_CTZ:
                if (!aot_compile_op_i64_ctz(comp_ctx, func_ctx))
                    return false;
                break;

            case WASM_OP_I64_POPCNT:
                if (!aot_compile_op_i64_popcnt(comp_ctx, func_ctx))
                    return false;
                break;

            case WASM_OP_I64_ADD:
            case WASM_OP_I64_SUB:
            case WASM_OP_I64_MUL:
            case WASM_OP_I64_DIV_S:
            case WASM_OP_I64_DIV_U:
            case WASM_OP_I64_REM_S:
            case WASM_OP_I64_REM_U:
                if (!aot_compile_op_i64_arithmetic(
                        comp_ctx, func_ctx, INT_ADD + opcode - WASM_OP_I64_ADD,
                        &frame_ip))
                    return false;
                break;

            case WASM_OP_I64_AND:
            case WASM_OP_I64_OR:
            case WASM_OP_I64_XOR:
                if (!aot_compile_op_i64_bitwise(
                        comp_ctx, func_ctx, INT_SHL + opcode - WASM_OP_I64_AND))
                    return false;
                break;

            case WASM_OP_I64_SHL:
            case WASM_OP_I64_SHR_S:
            case WASM_OP_I64_SHR_U:
            case WASM_OP_I64_ROTL:
            case WASM_OP_I64_ROTR:
                if (!aot_compile_op_i64_shift(
                        comp_ctx, func_ctx, INT_SHL + opcode - WASM_OP_I64_SHL))
                    return false;
                break;

            case WASM_OP_F32_ABS:
            case WASM_OP_F32_NEG:
            case WASM_OP_F32_CEIL:
            case WASM_OP_F32_FLOOR:
            case WASM_OP_F32_TRUNC:
            case WASM_OP_F32_NEAREST:
            case WASM_OP_F32_SQRT:
                if (!aot_compile_op_f32_math(comp_ctx, func_ctx,
                                             FLOAT_ABS + opcode
                                                 - WASM_OP_F32_ABS))
                    return false;
                break;

            case WASM_OP_F32_ADD:
            case WASM_OP_F32_SUB:
            case WASM_OP_F32_MUL:
            case WASM_OP_F32_DIV:
            case WASM_OP_F32_MIN:
            case WASM_OP_F32_MAX:
                if (!aot_compile_op_f32_arithmetic(comp_ctx, func_ctx,
                                                   FLOAT_ADD + opcode
                                                       - WASM_OP_F32_ADD))
                    return false;
                break;

            case WASM_OP_F32_COPYSIGN:
                if (!aot_compile_op_f32_copysign(comp_ctx, func_ctx))
                    return false;
                break;

            case WASM_OP_F64_ABS:
            case WASM_OP_F64_NEG:
            case WASM_OP_F64_CEIL:
            case WASM_OP_F64_FLOOR:
            case WASM_OP_F64_TRUNC:
            case WASM_OP_F64_NEAREST:
            case WASM_OP_F64_SQRT:
                if (!aot_compile_op_f64_math(comp_ctx, func_ctx,
                                             FLOAT_ABS + opcode
                                                 - WASM_OP_F64_ABS))
                    return false;
                break;

            case WASM_OP_F64_ADD:
            case WASM_OP_F64_SUB:
            case WASM_OP_F64_MUL:
            case WASM_OP_F64_DIV:
            case WASM_OP_F64_MIN:
            case WASM_OP_F64_MAX:
                if (!aot_compile_op_f64_arithmetic(comp_ctx, func_ctx,
                                                   FLOAT_ADD + opcode
                                                       - WASM_OP_F64_ADD))
                    return false;
                break;

            case WASM_OP_F64_COPYSIGN:
                if (!aot_compile_op_f64_copysign(comp_ctx, func_ctx))
                    return false;
                break;

            case WASM_OP_I32_WRAP_I64:
                if (!aot_compile_op_i32_wrap_i64(comp_ctx, func_ctx))
                    return false;
                break;

            case WASM_OP_I32_TRUNC_S_F32:
            case WASM_OP_I32_TRUNC_U_F32:
                sign = (opcode == WASM_OP_I32_TRUNC_S_F32) ? true : false;
                if (!aot_compile_op_i32_trunc_f32(comp_ctx, func_ctx, sign,
                                                  false))
                    return false;
                break;

            case WASM_OP_I32_TRUNC_S_F64:
            case WASM_OP_I32_TRUNC_U_F64:
                sign = (opcode == WASM_OP_I32_TRUNC_S_F64) ? true : false;
                if (!aot_compile_op_i32_trunc_f64(comp_ctx, func_ctx, sign,
                                                  false))
                    return false;
                break;

            case WASM_OP_I64_EXTEND_S_I32:
            case WASM_OP_I64_EXTEND_U_I32:
                sign = (opcode == WASM_OP_I64_EXTEND_S_I32) ? true : false;
                if (!aot_compile_op_i64_extend_i32(comp_ctx, func_ctx, sign))
                    return false;
                break;

            case WASM_OP_I64_TRUNC_S_F32:
            case WASM_OP_I64_TRUNC_U_F32:
                sign = (opcode == WASM_OP_I64_TRUNC_S_F32) ? true : false;
                if (!aot_compile_op_i64_trunc_f32(comp_ctx, func_ctx, sign,
                                                  false))
                    return false;
                break;

            case WASM_OP_I64_TRUNC_S_F64:
            case WASM_OP_I64_TRUNC_U_F64:
                sign = (opcode == WASM_OP_I64_TRUNC_S_F64) ? true : false;
                if (!aot_compile_op_i64_trunc_f64(comp_ctx, func_ctx, sign,
                                                  false))
                    return false;
                break;

            case WASM_OP_F32_CONVERT_S_I32:
            case WASM_OP_F32_CONVERT_U_I32:
                sign = (opcode == WASM_OP_F32_CONVERT_S_I32) ? true : false;
                if (!aot_compile_op_f32_convert_i32(comp_ctx, func_ctx, sign))
                    return false;
                break;

            case WASM_OP_F32_CONVERT_S_I64:
            case WASM_OP_F32_CONVERT_U_I64:
                sign = (opcode == WASM_OP_F32_CONVERT_S_I64) ? true : false;
                if (!aot_compile_op_f32_convert_i64(comp_ctx, func_ctx, sign))
                    return false;
                break;

            case WASM_OP_F32_DEMOTE_F64:
                if (!aot_compile_op_f32_demote_f64(comp_ctx, func_ctx))
                    return false;
                break;

            case WASM_OP_F64_CONVERT_S_I32:
            case WASM_OP_F64_CONVERT_U_I32:
                sign = (opcode == WASM_OP_F64_CONVERT_S_I32) ? true : false;
                if (!aot_compile_op_f64_convert_i32(comp_ctx, func_ctx, sign))
                    return false;
                break;

            case WASM_OP_F64_CONVERT_S_I64:
            case WASM_OP_F64_CONVERT_U_I64:
                sign = (opcode == WASM_OP_F64_CONVERT_S_I64) ? true : false;
                if (!aot_compile_op_f64_convert_i64(comp_ctx, func_ctx, sign))
                    return false;
                break;

            case WASM_OP_F64_PROMOTE_F32:
                if (!aot_compile_op_f64_promote_f32(comp_ctx, func_ctx))
                    return false;
                break;

            case WASM_OP_I32_REINTERPRET_F32:
                if (!aot_compile_op_i32_reinterpret_f32(comp_ctx, func_ctx))
                    return false;
                break;

            case WASM_OP_I64_REINTERPRET_F64:
                if (!aot_compile_op_i64_reinterpret_f64(comp_ctx, func_ctx))
                    return false;
                break;

            case WASM_OP_F32_REINTERPRET_I32:
                if (!aot_compile_op_f32_reinterpret_i32(comp_ctx, func_ctx))
                    return false;
                break;

            case WASM_OP_F64_REINTERPRET_I64:
                if (!aot_compile_op_f64_reinterpret_i64(comp_ctx, func_ctx))
                    return false;
                break;

            case WASM_OP_I32_EXTEND8_S:
                if (!aot_compile_op_i32_extend_i32(comp_ctx, func_ctx, 8))
                    return false;
                break;

            case WASM_OP_I32_EXTEND16_S:
                if (!aot_compile_op_i32_extend_i32(comp_ctx, func_ctx, 16))
                    return false;
                break;

            case WASM_OP_I64_EXTEND8_S:
                if (!aot_compile_op_i64_extend_i64(comp_ctx, func_ctx, 8))
                    return false;
                break;

            case WASM_OP_I64_EXTEND16_S:
                if (!aot_compile_op_i64_extend_i64(comp_ctx, func_ctx, 16))
                    return false;
                break;

            case WASM_OP_I64_EXTEND32_S:
                if (!aot_compile_op_i64_extend_i64(comp_ctx, func_ctx, 32))
                    return false;
                break;

            case WASM_OP_MISC_PREFIX:
            {
                uint32 opcode1;

                read_leb_uint32(frame_ip, frame_ip_end, opcode1);
                opcode = (uint32)opcode1;

#if WASM_ENABLE_BULK_MEMORY != 0
                if (WASM_OP_MEMORY_INIT <= opcode
                    && opcode <= WASM_OP_MEMORY_FILL
                    && !comp_ctx->enable_bulk_memory) {
                    goto unsupport_bulk_memory;
                }
#endif

#if WASM_ENABLE_REF_TYPES != 0
                if (WASM_OP_TABLE_INIT <= opcode && opcode <= WASM_OP_TABLE_FILL
                    && !comp_ctx->enable_ref_types) {
                    goto unsupport_ref_types;
                }
#endif

                switch (opcode) {
                    case WASM_OP_I32_TRUNC_SAT_S_F32:
                    case WASM_OP_I32_TRUNC_SAT_U_F32:
                        sign = (opcode == WASM_OP_I32_TRUNC_SAT_S_F32) ? true
                                                                       : false;
                        if (!aot_compile_op_i32_trunc_f32(comp_ctx, func_ctx,
                                                          sign, true))
                            return false;
                        break;
                    case WASM_OP_I32_TRUNC_SAT_S_F64:
                    case WASM_OP_I32_TRUNC_SAT_U_F64:
                        sign = (opcode == WASM_OP_I32_TRUNC_SAT_S_F64) ? true
                                                                       : false;
                        if (!aot_compile_op_i32_trunc_f64(comp_ctx, func_ctx,
                                                          sign, true))
                            return false;
                        break;
                    case WASM_OP_I64_TRUNC_SAT_S_F32:
                    case WASM_OP_I64_TRUNC_SAT_U_F32:
                        sign = (opcode == WASM_OP_I64_TRUNC_SAT_S_F32) ? true
                                                                       : false;
                        if (!aot_compile_op_i64_trunc_f32(comp_ctx, func_ctx,
                                                          sign, true))
                            return false;
                        break;
                    case WASM_OP_I64_TRUNC_SAT_S_F64:
                    case WASM_OP_I64_TRUNC_SAT_U_F64:
                        sign = (opcode == WASM_OP_I64_TRUNC_SAT_S_F64) ? true
                                                                       : false;
                        if (!aot_compile_op_i64_trunc_f64(comp_ctx, func_ctx,
                                                          sign, true))
                            return false;
                        break;
#if WASM_ENABLE_BULK_MEMORY != 0
                    case WASM_OP_MEMORY_INIT:
                    {
                        uint32 seg_index;
                        read_leb_uint32(frame_ip, frame_ip_end, seg_index);
                        frame_ip++;
                        if (!aot_compile_op_memory_init(comp_ctx, func_ctx,
                                                        seg_index))
                            return false;
                        break;
                    }
                    case WASM_OP_DATA_DROP:
                    {
                        uint32 seg_index;
                        read_leb_uint32(frame_ip, frame_ip_end, seg_index);
                        if (!aot_compile_op_data_drop(comp_ctx, func_ctx,
                                                      seg_index))
                            return false;
                        break;
                    }
                    case WASM_OP_MEMORY_COPY:
                    {
                        frame_ip += 2;
                        if (!aot_compile_op_memory_copy(comp_ctx, func_ctx))
                            return false;
                        break;
                    }
                    case WASM_OP_MEMORY_FILL:
                    {
                        frame_ip++;
                        if (!aot_compile_op_memory_fill(comp_ctx, func_ctx))
                            return false;
                        break;
                    }
#endif /* WASM_ENABLE_BULK_MEMORY */
#if WASM_ENABLE_REF_TYPES != 0
                    case WASM_OP_TABLE_INIT:
                    {
                        uint32 tbl_idx, tbl_seg_idx;

                        read_leb_uint32(frame_ip, frame_ip_end, tbl_seg_idx);
                        read_leb_uint32(frame_ip, frame_ip_end, tbl_idx);
                        if (!aot_compile_op_table_init(comp_ctx, func_ctx,
                                                       tbl_idx, tbl_seg_idx))
                            return false;
                        break;
                    }
                    case WASM_OP_ELEM_DROP:
                    {
                        uint32 tbl_seg_idx;

                        read_leb_uint32(frame_ip, frame_ip_end, tbl_seg_idx);
                        if (!aot_compile_op_elem_drop(comp_ctx, func_ctx,
                                                      tbl_seg_idx))
                            return false;
                        break;
                    }
                    case WASM_OP_TABLE_COPY:
                    {
                        uint32 src_tbl_idx, dst_tbl_idx;

                        read_leb_uint32(frame_ip, frame_ip_end, dst_tbl_idx);
                        read_leb_uint32(frame_ip, frame_ip_end, src_tbl_idx);
                        if (!aot_compile_op_table_copy(
                                comp_ctx, func_ctx, src_tbl_idx, dst_tbl_idx))
                            return false;
                        break;
                    }
                    case WASM_OP_TABLE_GROW:
                    {
                        uint32 tbl_idx;

                        read_leb_uint32(frame_ip, frame_ip_end, tbl_idx);
                        if (!aot_compile_op_table_grow(comp_ctx, func_ctx,
                                                       tbl_idx))
                            return false;
                        break;
                    }

                    case WASM_OP_TABLE_SIZE:
                    {
                        uint32 tbl_idx;

                        read_leb_uint32(frame_ip, frame_ip_end, tbl_idx);
                        if (!aot_compile_op_table_size(comp_ctx, func_ctx,
                                                       tbl_idx))
                            return false;
                        break;
                    }
                    case WASM_OP_TABLE_FILL:
                    {
                        uint32 tbl_idx;

                        read_leb_uint32(frame_ip, frame_ip_end, tbl_idx);
                        if (!aot_compile_op_table_fill(comp_ctx, func_ctx,
                                                       tbl_idx))
                            return false;
                        break;
                    }
#endif /* WASM_ENABLE_REF_TYPES */
                    default:
                        aot_set_last_error("unsupported opcode");
                        return false;
                }
                break;
            }

#if WASM_ENABLE_SHARED_MEMORY != 0
            case WASM_OP_ATOMIC_PREFIX:
            {
                uint8 bin_op, op_type;

                if (frame_ip < frame_ip_end) {
                    opcode = *frame_ip++;
                }
                if (opcode != WASM_OP_ATOMIC_FENCE) {
                    read_leb_uint32(frame_ip, frame_ip_end, align);
                    read_leb_uint32(frame_ip, frame_ip_end, offset);
                }
                switch (opcode) {
                    case WASM_OP_ATOMIC_WAIT32:
                        if (!aot_compile_op_atomic_wait(comp_ctx, func_ctx,
                                                        VALUE_TYPE_I32, align,
                                                        offset, 4))
                            return false;
                        break;
                    case WASM_OP_ATOMIC_WAIT64:
                        if (!aot_compile_op_atomic_wait(comp_ctx, func_ctx,
                                                        VALUE_TYPE_I64, align,
                                                        offset, 8))
                            return false;
                        break;
                    case WASM_OP_ATOMIC_NOTIFY:
                        if (!aot_compiler_op_atomic_notify(
                                comp_ctx, func_ctx, align, offset, bytes))
                            return false;
                        break;
                    case WASM_OP_ATOMIC_FENCE:
                        /* Skip memory index */
                        frame_ip++;
                        if (!aot_compiler_op_atomic_fence(comp_ctx, func_ctx))
                            return false;
                        break;
                    case WASM_OP_ATOMIC_I32_LOAD:
                        bytes = 4;
                        goto op_atomic_i32_load;
                    case WASM_OP_ATOMIC_I32_LOAD8_U:
                        bytes = 1;
                        goto op_atomic_i32_load;
                    case WASM_OP_ATOMIC_I32_LOAD16_U:
                        bytes = 2;
                    op_atomic_i32_load:
                        if (!aot_compile_op_i32_load(comp_ctx, func_ctx, align,
                                                     offset, bytes, sign, true))
                            return false;
                        break;

                    case WASM_OP_ATOMIC_I64_LOAD:
                        bytes = 8;
                        goto op_atomic_i64_load;
                    case WASM_OP_ATOMIC_I64_LOAD8_U:
                        bytes = 1;
                        goto op_atomic_i64_load;
                    case WASM_OP_ATOMIC_I64_LOAD16_U:
                        bytes = 2;
                        goto op_atomic_i64_load;
                    case WASM_OP_ATOMIC_I64_LOAD32_U:
                        bytes = 4;
                    op_atomic_i64_load:
                        if (!aot_compile_op_i64_load(comp_ctx, func_ctx, align,
                                                     offset, bytes, sign, true))
                            return false;
                        break;

                    case WASM_OP_ATOMIC_I32_STORE:
                        bytes = 4;
                        goto op_atomic_i32_store;
                    case WASM_OP_ATOMIC_I32_STORE8:
                        bytes = 1;
                        goto op_atomic_i32_store;
                    case WASM_OP_ATOMIC_I32_STORE16:
                        bytes = 2;
                    op_atomic_i32_store:
                        if (!aot_compile_op_i32_store(comp_ctx, func_ctx, align,
                                                      offset, bytes, true))
                            return false;
                        break;

                    case WASM_OP_ATOMIC_I64_STORE:
                        bytes = 8;
                        goto op_atomic_i64_store;
                    case WASM_OP_ATOMIC_I64_STORE8:
                        bytes = 1;
                        goto op_atomic_i64_store;
                    case WASM_OP_ATOMIC_I64_STORE16:
                        bytes = 2;
                        goto op_atomic_i64_store;
                    case WASM_OP_ATOMIC_I64_STORE32:
                        bytes = 4;
                    op_atomic_i64_store:
                        if (!aot_compile_op_i64_store(comp_ctx, func_ctx, align,
                                                      offset, bytes, true))
                            return false;
                        break;

                    case WASM_OP_ATOMIC_RMW_I32_CMPXCHG:
                        bytes = 4;
                        op_type = VALUE_TYPE_I32;
                        goto op_atomic_cmpxchg;
                    case WASM_OP_ATOMIC_RMW_I64_CMPXCHG:
                        bytes = 8;
                        op_type = VALUE_TYPE_I64;
                        goto op_atomic_cmpxchg;
                    case WASM_OP_ATOMIC_RMW_I32_CMPXCHG8_U:
                        bytes = 1;
                        op_type = VALUE_TYPE_I32;
                        goto op_atomic_cmpxchg;
                    case WASM_OP_ATOMIC_RMW_I32_CMPXCHG16_U:
                        bytes = 2;
                        op_type = VALUE_TYPE_I32;
                        goto op_atomic_cmpxchg;
                    case WASM_OP_ATOMIC_RMW_I64_CMPXCHG8_U:
                        bytes = 1;
                        op_type = VALUE_TYPE_I64;
                        goto op_atomic_cmpxchg;
                    case WASM_OP_ATOMIC_RMW_I64_CMPXCHG16_U:
                        bytes = 2;
                        op_type = VALUE_TYPE_I64;
                        goto op_atomic_cmpxchg;
                    case WASM_OP_ATOMIC_RMW_I64_CMPXCHG32_U:
                        bytes = 4;
                        op_type = VALUE_TYPE_I64;
                    op_atomic_cmpxchg:
                        if (!aot_compile_op_atomic_cmpxchg(comp_ctx, func_ctx,
                                                           op_type, align,
                                                           offset, bytes))
                            return false;
                        break;

                        COMPILE_ATOMIC_RMW(Add, ADD);
                        COMPILE_ATOMIC_RMW(Sub, SUB);
                        COMPILE_ATOMIC_RMW(And, AND);
                        COMPILE_ATOMIC_RMW(Or, OR);
                        COMPILE_ATOMIC_RMW(Xor, XOR);
                        COMPILE_ATOMIC_RMW(Xchg, XCHG);

                    build_atomic_rmw:
                        if (!aot_compile_op_atomic_rmw(comp_ctx, func_ctx,
                                                       bin_op, op_type, align,
                                                       offset, bytes))
                            return false;
                        break;

                    default:
                        aot_set_last_error("unsupported opcode");
                        return false;
                }
                break;
            }
#endif /* end of WASM_ENABLE_SHARED_MEMORY */

#if WASM_ENABLE_SIMD != 0
            case WASM_OP_SIMD_PREFIX:
            {
                if (!comp_ctx->enable_simd) {
                    goto unsupport_simd;
                }

                opcode = *frame_ip++;
                /* follow the order of enum WASMSimdEXTOpcode in
                   wasm_opcode.h */
                switch (opcode) {
                    /* Memory instruction */
                    case SIMD_v128_load:
                    {
                        read_leb_uint32(frame_ip, frame_ip_end, align);
                        read_leb_uint32(frame_ip, frame_ip_end, offset);
                        if (!aot_compile_simd_v128_load(comp_ctx, func_ctx,
                                                        align, offset))
                            return false;
                        break;
                    }

                    case SIMD_v128_load8x8_s:
                    case SIMD_v128_load8x8_u:
                    case SIMD_v128_load16x4_s:
                    case SIMD_v128_load16x4_u:
                    case SIMD_v128_load32x2_s:
                    case SIMD_v128_load32x2_u:
                    {
                        read_leb_uint32(frame_ip, frame_ip_end, align);
                        read_leb_uint32(frame_ip, frame_ip_end, offset);
                        if (!aot_compile_simd_load_extend(
                                comp_ctx, func_ctx, opcode, align, offset))
                            return false;
                        break;
                    }

                    case SIMD_v128_load8_splat:
                    case SIMD_v128_load16_splat:
                    case SIMD_v128_load32_splat:
                    case SIMD_v128_load64_splat:
                    {
                        read_leb_uint32(frame_ip, frame_ip_end, align);
                        read_leb_uint32(frame_ip, frame_ip_end, offset);
                        if (!aot_compile_simd_load_splat(comp_ctx, func_ctx,
                                                         opcode, align, offset))
                            return false;
                        break;
                    }

                    case SIMD_v128_store:
                    {
                        read_leb_uint32(frame_ip, frame_ip_end, align);
                        read_leb_uint32(frame_ip, frame_ip_end, offset);
                        if (!aot_compile_simd_v128_store(comp_ctx, func_ctx,
                                                         align, offset))
                            return false;
                        break;
                    }

                    /* Basic operation */
                    case SIMD_v128_const:
                    {
                        if (!aot_compile_simd_v128_const(comp_ctx, func_ctx,
                                                         frame_ip))
                            return false;
                        frame_ip += 16;
                        break;
                    }

                    case SIMD_v8x16_shuffle:
                    {
                        if (!aot_compile_simd_shuffle(comp_ctx, func_ctx,
                                                      frame_ip))
                            return false;
                        frame_ip += 16;
                        break;
                    }

                    case SIMD_v8x16_swizzle:
                    {
                        if (!aot_compile_simd_swizzle(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    /* Splat operation */
                    case SIMD_i8x16_splat:
                    case SIMD_i16x8_splat:
                    case SIMD_i32x4_splat:
                    case SIMD_i64x2_splat:
                    case SIMD_f32x4_splat:
                    case SIMD_f64x2_splat:
                    {
                        if (!aot_compile_simd_splat(comp_ctx, func_ctx, opcode))
                            return false;
                        break;
                    }

                    /* Lane operation */
                    case SIMD_i8x16_extract_lane_s:
                    case SIMD_i8x16_extract_lane_u:
                    {
                        if (!aot_compile_simd_extract_i8x16(
                                comp_ctx, func_ctx, *frame_ip++,
                                SIMD_i8x16_extract_lane_s == opcode))
                            return false;
                        break;
                    }

                    case SIMD_i8x16_replace_lane:
                    {
                        if (!aot_compile_simd_replace_i8x16(comp_ctx, func_ctx,
                                                            *frame_ip++))
                            return false;
                        break;
                    }

                    case SIMD_i16x8_extract_lane_s:
                    case SIMD_i16x8_extract_lane_u:
                    {
                        if (!aot_compile_simd_extract_i16x8(
                                comp_ctx, func_ctx, *frame_ip++,
                                SIMD_i16x8_extract_lane_s == opcode))
                            return false;
                        break;
                    }

                    case SIMD_i16x8_replace_lane:
                    {
                        if (!aot_compile_simd_replace_i16x8(comp_ctx, func_ctx,
                                                            *frame_ip++))
                            return false;
                        break;
                    }

                    case SIMD_i32x4_extract_lane:
                    {
                        if (!aot_compile_simd_extract_i32x4(comp_ctx, func_ctx,
                                                            *frame_ip++))
                            return false;
                        break;
                    }

                    case SIMD_i32x4_replace_lane:
                    {
                        if (!aot_compile_simd_replace_i32x4(comp_ctx, func_ctx,
                                                            *frame_ip++))
                            return false;
                        break;
                    }

                    case SIMD_i64x2_extract_lane:
                    {
                        if (!aot_compile_simd_extract_i64x2(comp_ctx, func_ctx,
                                                            *frame_ip++))
                            return false;
                        break;
                    }

                    case SIMD_i64x2_replace_lane:
                    {
                        if (!aot_compile_simd_replace_i64x2(comp_ctx, func_ctx,
                                                            *frame_ip++))
                            return false;
                        break;
                    }

                    case SIMD_f32x4_extract_lane:
                    {
                        if (!aot_compile_simd_extract_f32x4(comp_ctx, func_ctx,
                                                            *frame_ip++))
                            return false;
                        break;
                    }

                    case SIMD_f32x4_replace_lane:
                    {
                        if (!aot_compile_simd_replace_f32x4(comp_ctx, func_ctx,
                                                            *frame_ip++))
                            return false;
                        break;
                    }

                    case SIMD_f64x2_extract_lane:
                    {
                        if (!aot_compile_simd_extract_f64x2(comp_ctx, func_ctx,
                                                            *frame_ip++))
                            return false;
                        break;
                    }

                    case SIMD_f64x2_replace_lane:
                    {
                        if (!aot_compile_simd_replace_f64x2(comp_ctx, func_ctx,
                                                            *frame_ip++))
                            return false;
                        break;
                    }

                    /* i8x16 Cmp */
                    case SIMD_i8x16_eq:
                    case SIMD_i8x16_ne:
                    case SIMD_i8x16_lt_s:
                    case SIMD_i8x16_lt_u:
                    case SIMD_i8x16_gt_s:
                    case SIMD_i8x16_gt_u:
                    case SIMD_i8x16_le_s:
                    case SIMD_i8x16_le_u:
                    case SIMD_i8x16_ge_s:
                    case SIMD_i8x16_ge_u:
                    {
                        if (!aot_compile_simd_i8x16_compare(
                                comp_ctx, func_ctx,
                                INT_EQ + opcode - SIMD_i8x16_eq))
                            return false;
                        break;
                    }

                    /* i16x8 Cmp */
                    case SIMD_i16x8_eq:
                    case SIMD_i16x8_ne:
                    case SIMD_i16x8_lt_s:
                    case SIMD_i16x8_lt_u:
                    case SIMD_i16x8_gt_s:
                    case SIMD_i16x8_gt_u:
                    case SIMD_i16x8_le_s:
                    case SIMD_i16x8_le_u:
                    case SIMD_i16x8_ge_s:
                    case SIMD_i16x8_ge_u:
                    {
                        if (!aot_compile_simd_i16x8_compare(
                                comp_ctx, func_ctx,
                                INT_EQ + opcode - SIMD_i16x8_eq))
                            return false;
                        break;
                    }

                    /* i32x4 Cmp */
                    case SIMD_i32x4_eq:
                    case SIMD_i32x4_ne:
                    case SIMD_i32x4_lt_s:
                    case SIMD_i32x4_lt_u:
                    case SIMD_i32x4_gt_s:
                    case SIMD_i32x4_gt_u:
                    case SIMD_i32x4_le_s:
                    case SIMD_i32x4_le_u:
                    case SIMD_i32x4_ge_s:
                    case SIMD_i32x4_ge_u:
                    {
                        if (!aot_compile_simd_i32x4_compare(
                                comp_ctx, func_ctx,
                                INT_EQ + opcode - SIMD_i32x4_eq))
                            return false;
                        break;
                    }

                    /* f32x4 Cmp */
                    case SIMD_f32x4_eq:
                    case SIMD_f32x4_ne:
                    case SIMD_f32x4_lt:
                    case SIMD_f32x4_gt:
                    case SIMD_f32x4_le:
                    case SIMD_f32x4_ge:
                    {
                        if (!aot_compile_simd_f32x4_compare(
                                comp_ctx, func_ctx,
                                FLOAT_EQ + opcode - SIMD_f32x4_eq))
                            return false;
                        break;
                    }

                    /* f64x2 Cmp */
                    case SIMD_f64x2_eq:
                    case SIMD_f64x2_ne:
                    case SIMD_f64x2_lt:
                    case SIMD_f64x2_gt:
                    case SIMD_f64x2_le:
                    case SIMD_f64x2_ge:
                    {
                        if (!aot_compile_simd_f64x2_compare(
                                comp_ctx, func_ctx,
                                FLOAT_EQ + opcode - SIMD_f64x2_eq))
                            return false;
                        break;
                    }

                    /* v128 Op */
                    case SIMD_v128_not:
                    case SIMD_v128_and:
                    case SIMD_v128_andnot:
                    case SIMD_v128_or:
                    case SIMD_v128_xor:
                    case SIMD_v128_bitselect:
                    {
                        if (!aot_compile_simd_v128_bitwise(comp_ctx, func_ctx,
                                                           V128_NOT + opcode
                                                               - SIMD_v128_not))
                            return false;
                        break;
                    }

                    case SIMD_v128_any_true:
                    {
                        if (!aot_compile_simd_v128_any_true(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    /* Load Lane Op */
                    case SIMD_v128_load8_lane:
                    case SIMD_v128_load16_lane:
                    case SIMD_v128_load32_lane:
                    case SIMD_v128_load64_lane:
                    {
                        read_leb_uint32(frame_ip, frame_ip_end, align);
                        read_leb_uint32(frame_ip, frame_ip_end, offset);
                        if (!aot_compile_simd_load_lane(comp_ctx, func_ctx,
                                                        opcode, align, offset,
                                                        *frame_ip++))
                            return false;
                        break;
                    }

                    case SIMD_v128_store8_lane:
                    case SIMD_v128_store16_lane:
                    case SIMD_v128_store32_lane:
                    case SIMD_v128_store64_lane:
                    {
                        read_leb_uint32(frame_ip, frame_ip_end, align);
                        read_leb_uint32(frame_ip, frame_ip_end, offset);
                        if (!aot_compile_simd_store_lane(comp_ctx, func_ctx,
                                                         opcode, align, offset,
                                                         *frame_ip++))
                            return false;
                        break;
                    }

                    case SIMD_v128_load32_zero:
                    case SIMD_v128_load64_zero:
                    {
                        read_leb_uint32(frame_ip, frame_ip_end, align);
                        read_leb_uint32(frame_ip, frame_ip_end, offset);
                        if (!aot_compile_simd_load_zero(comp_ctx, func_ctx,
                                                        opcode, align, offset))
                            return false;
                        break;
                    }

                    /* Float conversion */
                    case SIMD_f32x4_demote_f64x2_zero:
                    {
                        if (!aot_compile_simd_f64x2_demote(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_f64x2_promote_low_f32x4_zero:
                    {
                        if (!aot_compile_simd_f32x4_promote(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    /* i8x16 Op */
                    case SIMD_i8x16_abs:
                    {
                        if (!aot_compile_simd_i8x16_abs(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i8x16_neg:
                    {
                        if (!aot_compile_simd_i8x16_neg(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i8x16_popcnt:
                    {
                        if (!aot_compile_simd_i8x16_popcnt(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i8x16_all_true:
                    {
                        if (!aot_compile_simd_i8x16_all_true(comp_ctx,
                                                             func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i8x16_bitmask:
                    {
                        if (!aot_compile_simd_i8x16_bitmask(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i8x16_narrow_i16x8_s:
                    case SIMD_i8x16_narrow_i16x8_u:
                    {
                        if (!aot_compile_simd_i8x16_narrow_i16x8(
                                comp_ctx, func_ctx,
                                (opcode == SIMD_i8x16_narrow_i16x8_s)))
                            return false;
                        break;
                    }

                    case SIMD_f32x4_ceil:
                    {
                        if (!aot_compile_simd_f32x4_ceil(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_f32x4_floor:
                    {
                        if (!aot_compile_simd_f32x4_floor(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_f32x4_trunc:
                    {
                        if (!aot_compile_simd_f32x4_trunc(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_f32x4_nearest:
                    {
                        if (!aot_compile_simd_f32x4_nearest(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i8x16_shl:
                    case SIMD_i8x16_shr_s:
                    case SIMD_i8x16_shr_u:
                    {
                        if (!aot_compile_simd_i8x16_shift(comp_ctx, func_ctx,
                                                          INT_SHL + opcode
                                                              - SIMD_i8x16_shl))
                            return false;
                        break;
                    }

                    case SIMD_i8x16_add:
                    {
                        if (!aot_compile_simd_i8x16_arith(comp_ctx, func_ctx,
                                                          V128_ADD))
                            return false;
                        break;
                    }

                    case SIMD_i8x16_add_sat_s:
                    case SIMD_i8x16_add_sat_u:
                    {
                        if (!aot_compile_simd_i8x16_saturate(
                                comp_ctx, func_ctx, V128_ADD,
                                opcode == SIMD_i8x16_add_sat_s))
                            return false;
                        break;
                    }

                    case SIMD_i8x16_sub:
                    {
                        if (!aot_compile_simd_i8x16_arith(comp_ctx, func_ctx,
                                                          V128_SUB))
                            return false;
                        break;
                    }

                    case SIMD_i8x16_sub_sat_s:
                    case SIMD_i8x16_sub_sat_u:
                    {
                        if (!aot_compile_simd_i8x16_saturate(
                                comp_ctx, func_ctx, V128_SUB,
                                opcode == SIMD_i8x16_sub_sat_s))
                            return false;
                        break;
                    }

                    case SIMD_f64x2_ceil:
                    {
                        if (!aot_compile_simd_f64x2_ceil(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_f64x2_floor:
                    {
                        if (!aot_compile_simd_f64x2_floor(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i8x16_min_s:
                    case SIMD_i8x16_min_u:
                    {
                        if (!aot_compile_simd_i8x16_cmp(
                                comp_ctx, func_ctx, V128_MIN,
                                opcode == SIMD_i8x16_min_s))
                            return false;
                        break;
                    }

                    case SIMD_i8x16_max_s:
                    case SIMD_i8x16_max_u:
                    {
                        if (!aot_compile_simd_i8x16_cmp(
                                comp_ctx, func_ctx, V128_MAX,
                                opcode == SIMD_i8x16_max_s))
                            return false;
                        break;
                    }

                    case SIMD_f64x2_trunc:
                    {
                        if (!aot_compile_simd_f64x2_trunc(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i8x16_avgr_u:
                    {
                        if (!aot_compile_simd_i8x16_avgr_u(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i16x8_extadd_pairwise_i8x16_s:
                    case SIMD_i16x8_extadd_pairwise_i8x16_u:
                    {
                        if (!aot_compile_simd_i16x8_extadd_pairwise_i8x16(
                                comp_ctx, func_ctx,
                                SIMD_i16x8_extadd_pairwise_i8x16_s == opcode))
                            return false;
                        break;
                    }

                    case SIMD_i32x4_extadd_pairwise_i16x8_s:
                    case SIMD_i32x4_extadd_pairwise_i16x8_u:
                    {
                        if (!aot_compile_simd_i32x4_extadd_pairwise_i16x8(
                                comp_ctx, func_ctx,
                                SIMD_i32x4_extadd_pairwise_i16x8_s == opcode))
                            return false;
                        break;
                    }

                    /* i16x8 Op */
                    case SIMD_i16x8_abs:
                    {
                        if (!aot_compile_simd_i16x8_abs(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i16x8_neg:
                    {
                        if (!aot_compile_simd_i16x8_neg(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i16x8_q15mulr_sat_s:
                    {
                        if (!aot_compile_simd_i16x8_q15mulr_sat(comp_ctx,
                                                                func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i16x8_all_true:
                    {
                        if (!aot_compile_simd_i16x8_all_true(comp_ctx,
                                                             func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i16x8_bitmask:
                    {
                        if (!aot_compile_simd_i16x8_bitmask(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i16x8_narrow_i32x4_s:
                    case SIMD_i16x8_narrow_i32x4_u:
                    {
                        if (!aot_compile_simd_i16x8_narrow_i32x4(
                                comp_ctx, func_ctx,
                                SIMD_i16x8_narrow_i32x4_s == opcode))
                            return false;
                        break;
                    }

                    case SIMD_i16x8_extend_low_i8x16_s:
                    case SIMD_i16x8_extend_high_i8x16_s:
                    {
                        if (!aot_compile_simd_i16x8_extend_i8x16(
                                comp_ctx, func_ctx,
                                SIMD_i16x8_extend_low_i8x16_s == opcode, true))
                            return false;
                        break;
                    }

                    case SIMD_i16x8_extend_low_i8x16_u:
                    case SIMD_i16x8_extend_high_i8x16_u:
                    {
                        if (!aot_compile_simd_i16x8_extend_i8x16(
                                comp_ctx, func_ctx,
                                SIMD_i16x8_extend_low_i8x16_u == opcode, false))
                            return false;
                        break;
                    }

                    case SIMD_i16x8_shl:
                    case SIMD_i16x8_shr_s:
                    case SIMD_i16x8_shr_u:
                    {
                        if (!aot_compile_simd_i16x8_shift(comp_ctx, func_ctx,
                                                          INT_SHL + opcode
                                                              - SIMD_i16x8_shl))
                            return false;
                        break;
                    }

                    case SIMD_i16x8_add:
                    {
                        if (!aot_compile_simd_i16x8_arith(comp_ctx, func_ctx,
                                                          V128_ADD))
                            return false;
                        break;
                    }

                    case SIMD_i16x8_add_sat_s:
                    case SIMD_i16x8_add_sat_u:
                    {
                        if (!aot_compile_simd_i16x8_saturate(
                                comp_ctx, func_ctx, V128_ADD,
                                opcode == SIMD_i16x8_add_sat_s ? true : false))
                            return false;
                        break;
                    }

                    case SIMD_i16x8_sub:
                    {
                        if (!aot_compile_simd_i16x8_arith(comp_ctx, func_ctx,
                                                          V128_SUB))
                            return false;
                        break;
                    }

                    case SIMD_i16x8_sub_sat_s:
                    case SIMD_i16x8_sub_sat_u:
                    {
                        if (!aot_compile_simd_i16x8_saturate(
                                comp_ctx, func_ctx, V128_SUB,
                                opcode == SIMD_i16x8_sub_sat_s ? true : false))
                            return false;
                        break;
                    }

                    case SIMD_f64x2_nearest:
                    {
                        if (!aot_compile_simd_f64x2_nearest(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i16x8_mul:
                    {
                        if (!aot_compile_simd_i16x8_arith(comp_ctx, func_ctx,
                                                          V128_MUL))
                            return false;
                        break;
                    }

                    case SIMD_i16x8_min_s:
                    case SIMD_i16x8_min_u:
                    {
                        if (!aot_compile_simd_i16x8_cmp(
                                comp_ctx, func_ctx, V128_MIN,
                                opcode == SIMD_i16x8_min_s))
                            return false;
                        break;
                    }

                    case SIMD_i16x8_max_s:
                    case SIMD_i16x8_max_u:
                    {
                        if (!aot_compile_simd_i16x8_cmp(
                                comp_ctx, func_ctx, V128_MAX,
                                opcode == SIMD_i16x8_max_s))
                            return false;
                        break;
                    }

                    case SIMD_i16x8_avgr_u:
                    {
                        if (!aot_compile_simd_i16x8_avgr_u(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i16x8_extmul_low_i8x16_s:
                    case SIMD_i16x8_extmul_high_i8x16_s:
                    {
                        if (!(aot_compile_simd_i16x8_extmul_i8x16(
                                comp_ctx, func_ctx,
                                SIMD_i16x8_extmul_low_i8x16_s == opcode, true)))
                            return false;
                        break;
                    }

                    case SIMD_i16x8_extmul_low_i8x16_u:
                    case SIMD_i16x8_extmul_high_i8x16_u:
                    {
                        if (!(aot_compile_simd_i16x8_extmul_i8x16(
                                comp_ctx, func_ctx,
                                SIMD_i16x8_extmul_low_i8x16_u == opcode,
                                false)))
                            return false;
                        break;
                    }

                    /* i32x4 Op */
                    case SIMD_i32x4_abs:
                    {
                        if (!aot_compile_simd_i32x4_abs(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i32x4_neg:
                    {
                        if (!aot_compile_simd_i32x4_neg(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i32x4_all_true:
                    {
                        if (!aot_compile_simd_i32x4_all_true(comp_ctx,
                                                             func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i32x4_bitmask:
                    {
                        if (!aot_compile_simd_i32x4_bitmask(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i32x4_narrow_i64x2_s:
                    case SIMD_i32x4_narrow_i64x2_u:
                    {
                        if (!aot_compile_simd_i32x4_narrow_i64x2(
                                comp_ctx, func_ctx,
                                SIMD_i32x4_narrow_i64x2_s == opcode))
                            return false;
                        break;
                    }

                    case SIMD_i32x4_extend_low_i16x8_s:
                    case SIMD_i32x4_extend_high_i16x8_s:
                    {
                        if (!aot_compile_simd_i32x4_extend_i16x8(
                                comp_ctx, func_ctx,
                                SIMD_i32x4_extend_low_i16x8_s == opcode, true))
                            return false;
                        break;
                    }

                    case SIMD_i32x4_extend_low_i16x8_u:
                    case SIMD_i32x4_extend_high_i16x8_u:
                    {
                        if (!aot_compile_simd_i32x4_extend_i16x8(
                                comp_ctx, func_ctx,
                                SIMD_i32x4_extend_low_i16x8_u == opcode, false))
                            return false;
                        break;
                    }

                    case SIMD_i32x4_shl:
                    case SIMD_i32x4_shr_s:
                    case SIMD_i32x4_shr_u:
                    {
                        if (!aot_compile_simd_i32x4_shift(comp_ctx, func_ctx,
                                                          INT_SHL + opcode
                                                              - SIMD_i32x4_shl))
                            return false;
                        break;
                    }

                    case SIMD_i32x4_add:
                    {
                        if (!aot_compile_simd_i32x4_arith(comp_ctx, func_ctx,
                                                          V128_ADD))
                            return false;
                        break;
                    }

                    case SIMD_i32x4_add_sat_s:
                    case SIMD_i32x4_add_sat_u:
                    {
                        if (!aot_compile_simd_i32x4_saturate(
                                comp_ctx, func_ctx, V128_ADD,
                                opcode == SIMD_i32x4_add_sat_s))
                            return false;
                        break;
                    }

                    case SIMD_i32x4_sub:
                    {
                        if (!aot_compile_simd_i32x4_arith(comp_ctx, func_ctx,
                                                          V128_SUB))
                            return false;
                        break;
                    }

                    case SIMD_i32x4_sub_sat_s:
                    case SIMD_i32x4_sub_sat_u:
                    {
                        if (!aot_compile_simd_i32x4_saturate(
                                comp_ctx, func_ctx, V128_SUB,
                                opcode == SIMD_i32x4_add_sat_s))
                            return false;
                        break;
                    }

                    case SIMD_i32x4_mul:
                    {
                        if (!aot_compile_simd_i32x4_arith(comp_ctx, func_ctx,
                                                          V128_MUL))
                            return false;
                        break;
                    }

                    case SIMD_i32x4_min_s:
                    case SIMD_i32x4_min_u:
                    {
                        if (!aot_compile_simd_i32x4_cmp(
                                comp_ctx, func_ctx, V128_MIN,
                                SIMD_i32x4_min_s == opcode))
                            return false;
                        break;
                    }

                    case SIMD_i32x4_max_s:
                    case SIMD_i32x4_max_u:
                    {
                        if (!aot_compile_simd_i32x4_cmp(
                                comp_ctx, func_ctx, V128_MAX,
                                SIMD_i32x4_max_s == opcode))
                            return false;
                        break;
                    }

                    case SIMD_i32x4_dot_i16x8_s:
                    {
                        if (!aot_compile_simd_i32x4_dot_i16x8(comp_ctx,
                                                              func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i32x4_avgr_u:
                    {
                        if (!aot_compile_simd_i32x4_avgr_u(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i32x4_extmul_low_i16x8_s:
                    case SIMD_i32x4_extmul_high_i16x8_s:
                    {
                        if (!aot_compile_simd_i32x4_extmul_i16x8(
                                comp_ctx, func_ctx,
                                SIMD_i32x4_extmul_low_i16x8_s == opcode, true))
                            return false;
                        break;
                    }

                    case SIMD_i32x4_extmul_low_i16x8_u:
                    case SIMD_i32x4_extmul_high_i16x8_u:
                    {
                        if (!aot_compile_simd_i32x4_extmul_i16x8(
                                comp_ctx, func_ctx,
                                SIMD_i32x4_extmul_low_i16x8_u == opcode, false))
                            return false;
                        break;
                    }

                    /* i64x2 Op */
                    case SIMD_i64x2_abs:
                    {
                        if (!aot_compile_simd_i64x2_abs(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i64x2_neg:
                    {
                        if (!aot_compile_simd_i64x2_neg(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i64x2_all_true:
                    {
                        if (!aot_compile_simd_i64x2_all_true(comp_ctx,
                                                             func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i64x2_bitmask:
                    {
                        if (!aot_compile_simd_i64x2_bitmask(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_i64x2_extend_low_i32x4_s:
                    case SIMD_i64x2_extend_high_i32x4_s:
                    {
                        if (!aot_compile_simd_i64x2_extend_i32x4(
                                comp_ctx, func_ctx,
                                SIMD_i64x2_extend_low_i32x4_s == opcode, true))
                            return false;
                        break;
                    }

                    case SIMD_i64x2_extend_low_i32x4_u:
                    case SIMD_i64x2_extend_high_i32x4_u:
                    {
                        if (!aot_compile_simd_i64x2_extend_i32x4(
                                comp_ctx, func_ctx,
                                SIMD_i64x2_extend_low_i32x4_u == opcode, false))
                            return false;
                        break;
                    }

                    case SIMD_i64x2_shl:
                    case SIMD_i64x2_shr_s:
                    case SIMD_i64x2_shr_u:
                    {
                        if (!aot_compile_simd_i64x2_shift(comp_ctx, func_ctx,
                                                          INT_SHL + opcode
                                                              - SIMD_i64x2_shl))
                            return false;
                        break;
                    }

                    case SIMD_i64x2_add:
                    {
                        if (!aot_compile_simd_i64x2_arith(comp_ctx, func_ctx,
                                                          V128_ADD))
                            return false;
                        break;
                    }

                    case SIMD_i64x2_sub:
                    {
                        if (!aot_compile_simd_i64x2_arith(comp_ctx, func_ctx,
                                                          V128_SUB))
                            return false;
                        break;
                    }

                    case SIMD_i64x2_mul:
                    {
                        if (!aot_compile_simd_i64x2_arith(comp_ctx, func_ctx,
                                                          V128_MUL))
                            return false;
                        break;
                    }

                    case SIMD_i64x2_eq:
                    case SIMD_i64x2_ne:
                    case SIMD_i64x2_lt_s:
                    case SIMD_i64x2_gt_s:
                    case SIMD_i64x2_le_s:
                    case SIMD_i64x2_ge_s:
                    {
                        IntCond icond[] = { INT_EQ,   INT_NE,   INT_LT_S,
                                            INT_GT_S, INT_LE_S, INT_GE_S };
                        if (!aot_compile_simd_i64x2_compare(
                                comp_ctx, func_ctx,
                                icond[opcode - SIMD_i64x2_eq]))
                            return false;
                        break;
                    }

                    case SIMD_i64x2_extmul_low_i32x4_s:
                    case SIMD_i64x2_extmul_high_i32x4_s:
                    {
                        if (!aot_compile_simd_i64x2_extmul_i32x4(
                                comp_ctx, func_ctx,
                                SIMD_i64x2_extmul_low_i32x4_s == opcode, true))
                            return false;
                        break;
                    }

                    case SIMD_i64x2_extmul_low_i32x4_u:
                    case SIMD_i64x2_extmul_high_i32x4_u:
                    {
                        if (!aot_compile_simd_i64x2_extmul_i32x4(
                                comp_ctx, func_ctx,
                                SIMD_i64x2_extmul_low_i32x4_u == opcode, false))
                            return false;
                        break;
                    }

                    /* f32x4 Op */
                    case SIMD_f32x4_abs:
                    {
                        if (!aot_compile_simd_f32x4_abs(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_f32x4_neg:
                    {
                        if (!aot_compile_simd_f32x4_neg(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_f32x4_round:
                    {
                        if (!aot_compile_simd_f32x4_round(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_f32x4_sqrt:
                    {
                        if (!aot_compile_simd_f32x4_sqrt(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_f32x4_add:
                    case SIMD_f32x4_sub:
                    case SIMD_f32x4_mul:
                    case SIMD_f32x4_div:
                    {
                        if (!aot_compile_simd_f32x4_arith(comp_ctx, func_ctx,
                                                          FLOAT_ADD + opcode
                                                              - SIMD_f32x4_add))
                            return false;
                        break;
                    }

                    case SIMD_f32x4_min:
                    case SIMD_f32x4_max:
                    {
                        if (!aot_compile_simd_f32x4_min_max(
                                comp_ctx, func_ctx, SIMD_f32x4_min == opcode))
                            return false;
                        break;
                    }

                    case SIMD_f32x4_pmin:
                    case SIMD_f32x4_pmax:
                    {
                        if (!aot_compile_simd_f32x4_pmin_pmax(
                                comp_ctx, func_ctx, SIMD_f32x4_pmin == opcode))
                            return false;
                        break;
                    }

                        /* f64x2 Op */

                    case SIMD_f64x2_abs:
                    {
                        if (!aot_compile_simd_f64x2_abs(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_f64x2_neg:
                    {
                        if (!aot_compile_simd_f64x2_neg(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_f64x2_round:
                    {
                        if (!aot_compile_simd_f64x2_round(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_f64x2_sqrt:
                    {
                        if (!aot_compile_simd_f64x2_sqrt(comp_ctx, func_ctx))
                            return false;
                        break;
                    }

                    case SIMD_f64x2_add:
                    case SIMD_f64x2_sub:
                    case SIMD_f64x2_mul:
                    case SIMD_f64x2_div:
                    {
                        if (!aot_compile_simd_f64x2_arith(comp_ctx, func_ctx,
                                                          FLOAT_ADD + opcode
                                                              - SIMD_f64x2_add))
                            return false;
                        break;
                    }

                    case SIMD_f64x2_min:
                    case SIMD_f64x2_max:
                    {
                        if (!aot_compile_simd_f64x2_min_max(
                                comp_ctx, func_ctx, SIMD_f64x2_min == opcode))
                            return false;
                        break;
                    }

                    case SIMD_f64x2_pmin:
                    case SIMD_f64x2_pmax:
                    {
                        if (!aot_compile_simd_f64x2_pmin_pmax(
                                comp_ctx, func_ctx, SIMD_f64x2_pmin == opcode))
                            return false;
                        break;
                    }

                    /* Conversion Op */
                    case SIMD_i32x4_trunc_sat_f32x4_s:
                    case SIMD_i32x4_trunc_sat_f32x4_u:
                    {
                        if (!aot_compile_simd_i32x4_trunc_sat_f32x4(
                                comp_ctx, func_ctx,
                                SIMD_i32x4_trunc_sat_f32x4_s == opcode))
                            return false;
                        break;
                    }

                    case SIMD_f32x4_convert_i32x4_s:
                    case SIMD_f32x4_convert_i32x4_u:
                    {
                        if (!aot_compile_simd_f32x4_convert_i32x4(
                                comp_ctx, func_ctx,
                                SIMD_f32x4_convert_i32x4_s == opcode))
                            return false;
                        break;
                    }

                    case SIMD_i32x4_trunc_sat_f64x2_s_zero:
                    case SIMD_i32x4_trunc_sat_f64x2_u_zero:
                    {
                        if (!aot_compile_simd_i32x4_trunc_sat_f64x2(
                                comp_ctx, func_ctx,
                                SIMD_i32x4_trunc_sat_f64x2_s_zero == opcode))
                            return false;
                        break;
                    }

                    case SIMD_f64x2_convert_low_i32x4_s:
                    case SIMD_f64x2_convert_low_i32x4_u:
                    {
                        if (!aot_compile_simd_f64x2_convert_i32x4(
                                comp_ctx, func_ctx,
                                SIMD_f64x2_convert_low_i32x4_s == opcode))
                            return false;
                        break;
                    }

                    default:
                        aot_set_last_error("unsupported SIMD opcode");
                        return false;
                }
                break;
            }
#endif /* end of WASM_ENABLE_SIMD */

            default:
                aot_set_last_error("unsupported opcode");
                return false;
        }
    }

    /* Move func_return block to the bottom */
    if (func_ctx->func_return_block) {
        LLVMBasicBlockRef last_block = LLVMGetLastBasicBlock(func_ctx->func);
        if (last_block != func_ctx->func_return_block)
            LLVMMoveBasicBlockAfter(func_ctx->func_return_block, last_block);
    }

    /* Move got_exception block to the bottom */
    if (func_ctx->got_exception_block) {
        LLVMBasicBlockRef last_block = LLVMGetLastBasicBlock(func_ctx->func);
        if (last_block != func_ctx->got_exception_block)
            LLVMMoveBasicBlockAfter(func_ctx->got_exception_block, last_block);
    }
    return true;

#if WASM_ENABLE_SIMD != 0
unsupport_simd:
    aot_set_last_error("SIMD instruction was found, "
                       "try removing --disable-simd option");
    return false;
#endif

#if WASM_ENABLE_REF_TYPES != 0
unsupport_ref_types:
    aot_set_last_error("reference type instruction was found, "
                       "try removing --disable-ref-types option");
    return false;
#endif

#if WASM_ENABLE_BULK_MEMORY != 0
unsupport_bulk_memory:
    aot_set_last_error("bulk memory instruction was found, "
                       "try removing --disable-bulk-memory option");
    return false;
#endif

fail:
    return false;
}

static bool
verify_module(AOTCompContext *comp_ctx)
{
    char *msg = NULL;
    bool ret;

    ret = LLVMVerifyModule(comp_ctx->module, LLVMPrintMessageAction, &msg);
    if (!ret && msg) {
        if (msg[0] != '\0') {
            aot_set_last_error(msg);
            LLVMDisposeMessage(msg);
            return false;
        }
        LLVMDisposeMessage(msg);
    }

    return true;
}

bool
aot_compile_wasm(AOTCompContext *comp_ctx)
{
    uint32 i;

    if (!aot_validate_wasm(comp_ctx)) {
        return false;
    }

    bh_print_time("Begin to compile WASM bytecode to LLVM IR");
    for (i = 0; i < comp_ctx->func_ctx_count; i++) {
        if (!aot_compile_func(comp_ctx, i)) {
            return false;
        }
    }

#if WASM_ENABLE_DEBUG_AOT != 0
    LLVMDIBuilderFinalize(comp_ctx->debug_builder);
#endif

    /* Disable LLVM module verification for jit mode to speedup
       the compilation process */
    if (!comp_ctx->is_jit_mode) {
        bh_print_time("Begin to verify LLVM module");
        if (!verify_module(comp_ctx)) {
            return false;
        }
    }

    // aot_apply_mvvm_pass(comp_ctx, comp_ctx->module);

    /* Run IR optimization before feeding in ORCJIT and AOT codegen */
    if (comp_ctx->optimize) {
        /* Run passes for AOT/JIT mode.
           TODO: Apply these passes in the do_ir_transform callback of
           TransformLayer when compiling each jit function, so as to
           speedup the launch process. Now there are two issues in the
           JIT: one is memory leak in do_ir_transform, the other is
           possible core dump. */
        bh_print_time("Begin to run llvm optimization passes");
        aot_apply_llvm_new_pass_manager(comp_ctx, comp_ctx->module);
        bh_print_time("Finish llvm optimization passes");
    }

#ifdef DUMP_MODULE
    LLVMDumpModule(comp_ctx->module);
    os_printf("\n");
#endif

    if (comp_ctx->is_jit_mode) {
        LLVMErrorRef err;
        LLVMOrcJITDylibRef orc_main_dylib;
        LLVMOrcThreadSafeModuleRef orc_thread_safe_module;

        orc_main_dylib = LLVMOrcLLLazyJITGetMainJITDylib(comp_ctx->orc_jit);
        if (!orc_main_dylib) {
            aot_set_last_error(
                "failed to get orc orc_jit main dynmaic library");
            return false;
        }

        orc_thread_safe_module = LLVMOrcCreateNewThreadSafeModule(
            comp_ctx->module, comp_ctx->orc_thread_safe_context);
        if (!orc_thread_safe_module) {
            aot_set_last_error("failed to create thread safe module");
            return false;
        }

        if ((err = LLVMOrcLLLazyJITAddLLVMIRModule(
                 comp_ctx->orc_jit, orc_main_dylib, orc_thread_safe_module))) {
            /* If adding the ThreadSafeModule fails then we need to clean it up
               by ourselves, otherwise the orc orc_jit will manage the memory.
             */
            LLVMOrcDisposeThreadSafeModule(orc_thread_safe_module);
            aot_handle_llvm_errmsg("failed to addIRModule", err);
            return false;
        }

        if (comp_ctx->stack_sizes != NULL) {
            LLVMOrcJITTargetAddress addr;
            if ((err = LLVMOrcLLLazyJITLookup(comp_ctx->orc_jit, &addr,
                                              aot_stack_sizes_alias_name))) {
                aot_handle_llvm_errmsg("failed to look up stack_sizes", err);
                return false;
            }
            comp_ctx->jit_stack_sizes = (uint32 *)addr;
        }
    }

    return true;
}

#if !(defined(_WIN32) || defined(_WIN32_))
char *
aot_generate_tempfile_name(const char *prefix, const char *extension,
                           char *buffer, uint32 len)
{
    int fd, name_len;

    name_len = snprintf(buffer, len, "%s-XXXXXX", prefix);

    if ((fd = mkstemp(buffer)) <= 0) {
        aot_set_last_error("make temp file failed.");
        return NULL;
    }

    /* close and remove temp file */
    close(fd);
    unlink(buffer);

    /* Check if buffer length is enough */
    /* name_len + '.' + extension + '\0' */
    if (name_len + 1 + strlen(extension) + 1 > len) {
        aot_set_last_error("temp file name too long.");
        return NULL;
    }

    snprintf(buffer + name_len, len - name_len, ".%s", extension);
    return buffer;
}
#else

errno_t
_mktemp_s(char *nameTemplate, size_t sizeInChars);

char *
aot_generate_tempfile_name(const char *prefix, const char *extension,
                           char *buffer, uint32 len)
{
    int name_len;

    name_len = snprintf(buffer, len, "%s-XXXXXX", prefix);

    if (_mktemp_s(buffer, name_len + 1) != 0) {
        return NULL;
    }

    /* Check if buffer length is enough */
    /* name_len + '.' + extension + '\0' */
    if (name_len + 1 + strlen(extension) + 1 > len) {
        aot_set_last_error("temp file name too long.");
        return NULL;
    }

    snprintf(buffer + name_len, len - name_len, ".%s", extension);
    return buffer;
}
#endif /* end of !(defined(_WIN32) || defined(_WIN32_)) */

bool
aot_emit_llvm_file(AOTCompContext *comp_ctx, const char *file_name)
{
    char *err = NULL;

    bh_print_time("Begin to emit LLVM IR file");

    if (LLVMPrintModuleToFile(comp_ctx->module, file_name, &err) != 0) {
        if (err) {
            LLVMDisposeMessage(err);
            err = NULL;
        }
        aot_set_last_error("emit llvm ir to file failed.");
        return false;
    }

    return true;
}

static bool
aot_move_file(const char *dest, const char *src)
{
    FILE *dfp = fopen(dest, "w");
    FILE *sfp = fopen(src, "r");
    size_t rsz;
    char buf[128];
    bool success = false;

    if (dfp == NULL || sfp == NULL) {
        LOG_DEBUG("open error %s %s", dest, src);
        goto fail;
    }
    do {
        rsz = fread(buf, 1, sizeof(buf), sfp);
        if (rsz > 0) {
            size_t wsz = fwrite(buf, 1, rsz, dfp);
            if (wsz < rsz) {
                LOG_DEBUG("write error");
                goto fail;
            }
        }
        if (rsz < sizeof(buf)) {
            if (ferror(sfp)) {
                LOG_DEBUG("read error");
                goto fail;
            }
        }
    } while (rsz > 0);
    success = true;
fail:
    if (dfp != NULL) {
        if (fclose(dfp)) {
            LOG_DEBUG("close error");
            success = false;
        }
        if (!success) {
            (void)unlink(dest);
        }
    }
    if (sfp != NULL) {
        (void)fclose(sfp);
    }
    if (success) {
        (void)unlink(src);
    }
    return success;
}

bool
aot_emit_object_file(AOTCompContext *comp_ctx, char *file_name)
{
    char *err = NULL;
    LLVMCodeGenFileType file_type = LLVMObjectFile;
    LLVMTargetRef target = LLVMGetTargetMachineTarget(comp_ctx->target_machine);

    bh_print_time("Begin to emit object file");

#if !(defined(_WIN32) || defined(_WIN32_))
    if (comp_ctx->external_llc_compiler || comp_ctx->external_asm_compiler) {
        char cmd[1024];
        int ret;

        if (comp_ctx->external_llc_compiler) {
            const char *stack_usage_flag = "";
            char bc_file_name[64];
            char su_file_name[65]; /* See the comment below */

            if (comp_ctx->stack_usage_file != NULL) {
                /*
                 * Note: we know the caller uses 64 byte buffer for
                 * file_name. It will get 1 byte longer because we
                 * replace ".o" with ".su".
                 */
                size_t len = strlen(file_name);
                bh_assert(len + 1 <= sizeof(su_file_name));
                bh_assert(len > 3);
                bh_assert(file_name[len - 2] == '.');
                bh_assert(file_name[len - 1] == 'o');
                snprintf(su_file_name, sizeof(su_file_name), "%.*s.su",
                         (int)(len - 2), file_name);
                stack_usage_flag = " -fstack-usage";
            }

            if (!aot_generate_tempfile_name("wamrc-bc", "bc", bc_file_name,
                                            sizeof(bc_file_name))) {
                return false;
            }

            if (LLVMWriteBitcodeToFile(comp_ctx->module, bc_file_name) != 0) {
                aot_set_last_error("emit llvm bitcode file failed.");
                return false;
            }

            snprintf(cmd, sizeof(cmd), "%s%s %s -o %s %s",
                     comp_ctx->external_llc_compiler, stack_usage_flag,
                     comp_ctx->llc_compiler_flags ? comp_ctx->llc_compiler_flags
                                                  : "-O3 -c",
                     file_name, bc_file_name);
            LOG_VERBOSE("invoking external LLC compiler:\n\t%s", cmd);

            ret = system(cmd);
            /* remove temp bitcode file */
            unlink(bc_file_name);

            if (ret != 0) {
                aot_set_last_error("failed to compile LLVM bitcode to obj file "
                                   "with external LLC compiler.");
                return false;
            }
            if (comp_ctx->stack_usage_file != NULL) {
                /*
                 * move the temporary .su file to the specified location.
                 *
                 * Note: the former is automatimally inferred from the output
                 * filename (file_name here) by clang.
                 *
                 * Note: the latter might be user-specified.
                 * (wamrc --stack-usage=<file>)
                 */
                if (!aot_move_file(comp_ctx->stack_usage_file, su_file_name)) {
                    aot_set_last_error("failed to move su file.");
                    (void)unlink(su_file_name);
                    return false;
                }
            }
        }
        else if (comp_ctx->external_asm_compiler) {
            char asm_file_name[64];

            if (!aot_generate_tempfile_name("wamrc-asm", "s", asm_file_name,
                                            sizeof(asm_file_name))) {
                return false;
            }

            if (LLVMTargetMachineEmitToFile(comp_ctx->target_machine,
                                            comp_ctx->module, asm_file_name,
                                            LLVMAssemblyFile, &err)
                != 0) {
                if (err) {
                    LLVMDisposeMessage(err);
                    err = NULL;
                }
                aot_set_last_error("emit elf to assembly file failed.");
                return false;
            }

            snprintf(cmd, sizeof(cmd), "%s %s -o %s %s",
                     comp_ctx->external_asm_compiler,
                     comp_ctx->asm_compiler_flags ? comp_ctx->asm_compiler_flags
                                                  : "-O3 -c",
                     file_name, asm_file_name);
            LOG_VERBOSE("invoking external ASM compiler:\n\t%s", cmd);

            ret = system(cmd);
            /* remove temp assembly file */
            unlink(asm_file_name);

            if (ret != 0) {
                aot_set_last_error("failed to compile Assembly file to obj "
                                   "file with external ASM compiler.");
                return false;
            }
        }

        return true;
    }
#endif /* end of !(defined(_WIN32) || defined(_WIN32_)) */

    if (!strncmp(LLVMGetTargetName(target), "arc", 3))
        /* Emit to assmelby file instead for arc target
           as it cannot emit to object file */
        file_type = LLVMAssemblyFile;

    if (LLVMTargetMachineEmitToFile(comp_ctx->target_machine, comp_ctx->module,
                                    file_name, file_type, &err)
        != 0) {
        if (err) {
            LLVMDisposeMessage(err);
            err = NULL;
        }
        aot_set_last_error("emit elf to object file failed.");
        return false;
    }

    return true;
}