// Opt-in DWARF native continuation lowering. Kept separate from LLDB's
// source-level debug mode: continuation slots must survive without input DWARF.
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Transforms/Utils/Local.h>
#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/Transforms/Utils/PromoteMemToReg.h>
#include <llvm/IR/Dominators.h>

static cl::opt<bool> MVVMDwarfResume("mvvm-dwarf-resume", cl::init(false));
static cl::opt<bool> MVVMDwarfOptimize("mvvm-dwarf-optimize", cl::init(false));

extern "C" void
aot_mvvm_mark_body(AOTCompContext *ctx, AOTFuncContext *fc)
{
    if (!MVVMDwarfResume)
        return;
    auto *m = unwrap(fc->module);
    auto &c = m->getContext();
    auto hook = m->getOrInsertFunction("__mvvm_body_marker", Type::getVoidTy(c));
    unwrap(ctx->builder)->CreateCall(hook);
}

extern "C" void
aot_mvvm_mark_call(AOTCompContext *ctx, AOTFuncContext *fc, uint32 target)
{
    if (!MVVMDwarfResume) return;
    auto *m = unwrap(fc->module);
    auto *i32 = Type::getInt32Ty(m->getContext());
    auto hook = m->getOrInsertFunction("__mvvm_call_marker", Type::getVoidTy(m->getContext()), i32);
    unwrap(ctx->builder)->CreateCall(hook, {ConstantInt::get(i32, target)});
}

extern "C" bool
aot_mvvm_lower_resume(AOTCompContext *ctx)
{
    if (!MVVMDwarfResume)
        return true;
    if (ctx->is_jit_mode || ctx->enable_gc) {
        aot_set_last_error("DWARF resume currently requires non-GC AOT");
        return false;
    }
    auto &m = *unwrap(ctx->module);
    auto &c = m.getContext();
    auto *i32 = Type::getInt32Ty(c);
    DIBuilder di(m);
    auto *file = di.createFile("wamr-continuation", ".");
    di.createCompileUnit(dwarf::DW_LANG_C, file, "WAMR MVVM DWARF resume v1", false, "", 0);
    m.addModuleFlag(Module::Warning, "Debug Info Version", DEBUG_METADATA_VERSION);
    m.addModuleFlag(Module::Warning, "Dwarf Version", 4);
    auto enter = m.getOrInsertFunction("mvvm_dwarf_enter", i32, PointerType::getUnqual(c), i32, i32);
    auto leave = m.getOrInsertFunction("mvvm_dwarf_leave", Type::getVoidTy(c));
    auto pending = m.getOrInsertFunction("mvvm_dwarf_pending", i32);
    GlobalVariable *state = nullptr;
    if (MVVMDwarfOptimize) {
        state = new GlobalVariable(m, i32, false, GlobalValue::ExternalLinkage,
            nullptr, "mvvm_dwarf_state", nullptr, GlobalVariable::InitialExecTLSModel);
    }
    uint64_t schema = 14695981039346656037ULL;
    auto hash_text = [&](StringRef text) {
        for (unsigned char byte : text.bytes()) { schema ^= byte; schema *= 1099511628211ULL; }
        schema ^= 0xff; schema *= 1099511628211ULL;
    };
    hash_text(MVVMDwarfOptimize ? "mvvm-dwarf-optimized-v2" : "mvvm-dwarf-baseline-v2");

    // A native import can suspend; an indirect call is conservatively treated
    // as suspending. Close that set over direct Wasm calls before lowering.
    std::vector<bool> suspends(ctx->func_ctx_count, !MVVMDwarfOptimize);
    auto target_suspends = [&](uint64_t target) {
        return target == UINT32_MAX || target < ctx->comp_data->import_func_count
            || suspends.at(target - ctx->comp_data->import_func_count);
    };
    DenseMap<Function *, unsigned> function_ids;
    for (unsigned fi = 0; fi < ctx->func_ctx_count; ++fi)
        function_ids[unwrap<Function>(ctx->func_ctxes[fi]->func)] = fi;
    bool changed;
    do {
        changed = false;
        for (unsigned fi = 0; fi < ctx->func_ctx_count; ++fi) {
            if (suspends[fi]) continue;
            auto *f = unwrap<Function>(ctx->func_ctxes[fi]->func);
            for (auto &bb : *f) for (auto &ins : bb)
                if (auto *call = dyn_cast<CallInst>(&ins))
                    if (auto *callee = call->getCalledFunction())
                        if (callee->getName() == "__mvvm_call_marker"
                            && target_suspends(cast<ConstantInt>(call->getArgOperand(0))->getZExtValue()))
                            suspends[fi] = changed = true;
        }
    } while (changed);

    for (unsigned fi = 0; fi < ctx->func_ctx_count; ++fi) {
        auto *f = unwrap<Function>(ctx->func_ctxes[fi]->func);
        CallInst *marker = nullptr;
        SmallVector<CallInst *, 16> call_markers;
        SmallVector<CallInst *, 16> calls;
        for (auto &bb : *f)
            for (auto &ins : bb)
                if (auto *call = dyn_cast<CallInst>(&ins)) {
                    auto *callee = call->getCalledFunction();
                    if (callee && callee->getName() == "__mvvm_body_marker")
                        marker = call;
                    else if (callee && callee->getName() == "__mvvm_call_marker")
                        call_markers.push_back(call);
                    else if (!callee && call->arg_size() && call->getArgOperand(0)==f->getArg(0))
                        calls.push_back(call);
                    else if (callee && (callee->getName().starts_with(AOT_FUNC_PREFIX)
                                        || callee->getName() == "aot_invoke_native"
                                        || callee->getName() == "aot_call_indirect"))
                        calls.push_back(call);
                }
        if (!marker) {
            aot_set_last_error("missing DWARF resume body marker"); return false;
        }
        if (!suspends[fi]) {
            marker->eraseFromParent();
            for (auto *mark : call_markers) mark->eraseFromParent();
            continue;
        }
        if (MVVMDwarfOptimize) {
            llvm::erase_if(call_markers, [&](CallInst *mark) {
                if (target_suspends(cast<ConstantInt>(mark->getArgOperand(0))->getZExtValue())) return false;
                mark->eraseFromParent(); return true;
            });
            llvm::erase_if(calls, [&](CallInst *call) {
                auto found = function_ids.find(call->getCalledFunction());
                return found != function_ids.end() && !suspends[found->second];
            });
        }
        auto &entry = f->getEntryBlock();
        if (MVVMDwarfOptimize) {
            // Recover SSA before choosing checkpoint spills. Most Wasm locals
            // are temporary values that need no native stack home at a call.
            SmallVector<AllocaInst *, 32> promotable;
            for (auto &ins : entry)
                if (auto *a = dyn_cast<AllocaInst>(&ins))
                    if (isAllocaPromotable(a)) promotable.push_back(a);
            DominatorTree original(*f);
            PromoteMemToReg(promotable, original);
        }
        auto *body = entry.splitBasicBlock(marker->getIterator(), "mvvm_body");
        marker->eraseFromParent();
        // Each call gets a stable pre-optimization continuation ID. Splitting
        // before spilling puts operand reloads inside the re-entry block.
        SmallVector<BasicBlock *,16> sites;
        hash_text(std::to_string(fi));
        for (auto *mark : call_markers) {
            hash_text(std::to_string(cast<ConstantInt>(mark->getArgOperand(0))->getZExtValue()));
            sites.push_back(mark->getParent()->splitBasicBlock(mark->getIterator(), "mvvm_call"));
            mark->eraseFromParent();
        }

        // Ask dominance which SSA definitions would become unavailable after
        // adding resume edges. Spill those values only, rather than every
        // value crossing an ordinary basic-block boundary.
        std::unique_ptr<DominatorTree> resumed_dom;
        if (MVVMDwarfOptimize) {
            auto *branch = entry.getTerminator();
            auto *test = SwitchInst::Create(UndefValue::get(i32), body, sites.size(), branch);
            for (unsigned si=0; si<sites.size(); ++si)
                test->addCase(ConstantInt::get(i32,si+1),sites[si]);
            branch->eraseFromParent();
            resumed_dom = std::make_unique<DominatorTree>(*f);
        }

        SmallVector<Instruction *,128> regs;
        SmallVector<PHINode *,32> phis;
        for (auto &bb : *f) {
            if (&bb == &entry) continue;
            for (auto &ins : bb) {
                bool spill = ins.isUsedOutsideOfBlock(&bb);
                if (resumed_dom) {
                    spill = false;
                    for (const Use &use : ins.uses())
                        if (!resumed_dom->dominates(&ins, use)) { spill = true; break; }
                }
                if (auto *phi = dyn_cast<PHINode>(&ins)) {
                    if (!MVVMDwarfOptimize || spill) phis.push_back(phi);
                }
                else if (spill && (ins.getType()->isIntegerTy() || ins.getType()->isFloatingPointTy()))
                    regs.push_back(&ins);
            }
        }
        for (auto *phi : phis) {
            if (!phi->getType()->isIntegerTy() && !phi->getType()->isFloatingPointTy()) {
                aot_set_last_error("DWARF resume does not support reference PHIs"); return false;
            }
            if (MVVMDwarfOptimize)
                DemoteRegToStack(*phi, false, entry.getTerminator()->getIterator());
            else
                DemotePHIToStack(phi, entry.getTerminator()->getIterator());
        }
        for (auto *ins : regs)
            DemoteRegToStack(*ins, false, entry.getTerminator()->getIterator());

        auto *sp = di.createFunction(file, f->getName(), f->getName(), file, 1,
            di.createSubroutineType(di.getOrCreateTypeArray({})), 1,
            DINode::FlagPrototyped, DISubprogram::SPFlagDefinition);
        f->setSubprogram(sp);
        f->addFnAttr(Attribute::NoInline);
        if (!MVVMDwarfOptimize) f->addFnAttr(Attribute::OptimizeNone);
        f->addFnAttr("frame-pointer", "all");
        f->addFnAttr(Attribute::UWTable);
        IRBuilder<> at(entry.getTerminator());
        auto *pc = at.CreateAlloca(i32, nullptr, "mvvm_pc");
        at.CreateStore(at.getInt32(0), pc)->setVolatile(true);
        SmallVector<AllocaInst *,128> slots;
        for (auto &ins : entry)
            if (auto *a = dyn_cast<AllocaInst>(&ins))
                if (a->getAllocatedType()->isIntegerTy() || a->getAllocatedType()->isFloatingPointTy())
                    slots.push_back(a);
        unsigned slot_id = 0;
        for (auto *a : slots) {
            auto *t = a->getAllocatedType();
            auto bits = m.getDataLayout().getTypeStoreSize(t) * 8;
            auto name = a == pc ? std::string("mvvm_pc") : "mvvm_slot_" + std::to_string(slot_id++) + ":" + a->getName().str();
            hash_text(name); hash_text(std::to_string(bits));
            auto *dt = di.createBasicType(t->isFloatingPointTy() ? "float" : "bits", bits,
                         t->isFloatingPointTy() ? dwarf::DW_ATE_float : dwarf::DW_ATE_unsigned);
            auto *var = di.createAutoVariable(sp, name, file, 1, dt, true);
            di.insertDeclare(a, var, di.createExpression(), DILocation::get(c,1,0,sp), entry.getTerminator()->getIterator());
            // Initialize before any original parameter/default stores. This
            // also makes currently dead spill slots deterministic in snapshots.
            if (a != pc) {
                IRBuilder<> init(a->getNextNode());
                init.CreateStore(Constant::getNullValue(t), a)->setVolatile(true);
            }
            for (auto *user : a->users()) {
                if (auto *load = dyn_cast<LoadInst>(user)) load->setVolatile(true);
                if (auto *store = dyn_cast<StoreInst>(user)) store->setVolatile(true);
            }
        }
        SwitchInst *sw;
        if (MVVMDwarfOptimize) {
            auto *restore = BasicBlock::Create(c,"mvvm_restore_entry",f);
            auto *mode = at.CreateLoad(i32,state); mode->setVolatile(true);
            at.CreateCondBr(at.CreateICmpEQ(mode,at.getInt32(2)),restore,body);
            entry.getTerminator()->eraseFromParent();
            IRBuilder<> b(restore);
            auto *dispatch=b.CreateCall(enter,{f->getArg(0),b.getInt32(fi),b.getInt32(sites.size())});
            sw=b.CreateSwitch(dispatch,body,sites.size());
        } else {
            auto *dispatch = at.CreateCall(enter, {f->getArg(0), at.getInt32(fi), at.getInt32(sites.size())});
            sw = at.CreateSwitch(dispatch, body, sites.size());
            entry.getTerminator()->eraseFromParent();
        }
        for (unsigned si=0; si<sites.size(); ++si) {
            auto *site=sites[si];
            sw->addCase(ConstantInt::get(i32,si+1),site);
            IRBuilder<> set(&*site->getFirstInsertionPt());
            set.CreateStore(set.getInt32(si+1),pc)->setVolatile(true);
        }
        for (auto *call : calls) {
            call->setTailCall(false);
            auto *after=call->getParent()->splitBasicBlock(call->getNextNode()->getIterator(),"mvvm_after");
            auto *before=call->getParent();
            before->getTerminator()->eraseFromParent();
            IRBuilder<> check(before);
            auto *stop=BasicBlock::Create(c,"mvvm_suspend",f);
            Value *should_stop;
            if (MVVMDwarfOptimize) {
                auto *mode=check.CreateLoad(i32,state); mode->setVolatile(true);
                should_stop=check.CreateICmpEQ(mode,check.getInt32(1));
            } else should_stop=check.CreateICmpNE(check.CreateCall(pending),check.getInt32(0));
            check.CreateCondBr(should_stop,stop,after);
            IRBuilder<> stopb(stop);
            if (!MVVMDwarfOptimize) stopb.CreateCall(leave);
            if(f->getReturnType()->isVoidTy()) stopb.CreateRetVoid();
            else stopb.CreateRet(Constant::getNullValue(f->getReturnType()));
        }
        for(auto &bb:*f)
            if(auto *ret=dyn_cast<ReturnInst>(bb.getTerminator())) {
                if(bb.getName().starts_with("mvvm_suspend")) continue;
                IRBuilder<> b(ret);
                if (!MVVMDwarfOptimize) b.CreateCall(leave);
            }
        for(auto &bb:*f) for(auto &ins:bb)
            if(!ins.getDebugLoc()) ins.setDebugLoc(DILocation::get(c,1,0,sp));
    }
    new GlobalVariable(m, Type::getInt64Ty(c), true, GlobalValue::ExternalLinkage,
        ConstantInt::get(Type::getInt64Ty(c),schema), "mvvm_dwarf_schema_id");
    di.finalize();
    if (verifyModule(m, &errs())) {
        aot_set_last_error("DWARF continuation IR verification failed (unsupported live value)");
        return false;
    }
    return true;
}
