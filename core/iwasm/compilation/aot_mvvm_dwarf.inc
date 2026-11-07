// Opt-in DWARF native continuation lowering. Kept separate from LLDB's
// source-level debug mode: continuation slots must survive without input DWARF.
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Transforms/Utils/Local.h>
#include <llvm/BinaryFormat/Dwarf.h>

static cl::opt<bool> MVVMDwarfResume("mvvm-dwarf-resume", cl::init(false));

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
aot_mvvm_mark_call(AOTCompContext *ctx, AOTFuncContext *fc)
{
    if (!MVVMDwarfResume) return;
    auto *m = unwrap(fc->module);
    auto hook = m->getOrInsertFunction("__mvvm_call_marker", Type::getVoidTy(m->getContext()));
    unwrap(ctx->builder)->CreateCall(hook);
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
        auto &entry = f->getEntryBlock();
        auto *body = entry.splitBasicBlock(marker->getIterator(), "mvvm_body");
        marker->eraseFromParent();
        // Each call gets a stable pre-optimization continuation ID. Splitting
        // before spilling puts operand reloads inside the re-entry block.
        SmallVector<BasicBlock *,16> sites;
        for (auto *mark : call_markers) {
            sites.push_back(mark->getParent()->splitBasicBlock(mark->getIterator(), "mvvm_call"));
            mark->eraseFromParent();
        }

        SmallVector<Instruction *,128> regs;
        SmallVector<PHINode *,32> phis;
        for (auto &bb : *f) {
            if (&bb == &entry) continue;
            for (auto &ins : bb) {
                if (auto *phi = dyn_cast<PHINode>(&ins)) phis.push_back(phi);
                else if (ins.isUsedOutsideOfBlock(&bb) && (ins.getType()->isIntegerTy() || ins.getType()->isFloatingPointTy()))
                    regs.push_back(&ins);
            }
        }
        for (auto *phi : phis) {
            if (!phi->getType()->isIntegerTy() && !phi->getType()->isFloatingPointTy()) {
                aot_set_last_error("DWARF resume does not support reference PHIs"); return false;
            }
            DemotePHIToStack(phi, entry.getTerminator()->getIterator());
        }
        for (auto *ins : regs)
            DemoteRegToStack(*ins, false, entry.getTerminator()->getIterator());

        auto *sp = di.createFunction(file, f->getName(), f->getName(), file, 1,
            di.createSubroutineType(di.getOrCreateTypeArray({})), 1,
            DINode::FlagPrototyped, DISubprogram::SPFlagDefinition);
        f->setSubprogram(sp);
        f->addFnAttr(Attribute::NoInline);
        f->addFnAttr(Attribute::OptimizeNone);
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
        auto *dispatch = at.CreateCall(enter, {f->getArg(0), at.getInt32(fi), at.getInt32(sites.size())});
        auto *sw = at.CreateSwitch(dispatch, body, sites.size());
        entry.getTerminator()->eraseFromParent(); // remove original branch
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
            check.CreateCondBr(check.CreateICmpNE(check.CreateCall(pending),check.getInt32(0)),stop,after);
            IRBuilder<> stopb(stop); stopb.CreateCall(leave);
            if(f->getReturnType()->isVoidTy()) stopb.CreateRetVoid();
            else stopb.CreateRet(Constant::getNullValue(f->getReturnType()));
        }
        for(auto &bb:*f)
            if(auto *ret=dyn_cast<ReturnInst>(bb.getTerminator())) {
                if(bb.getName().starts_with("mvvm_suspend")) continue;
                IRBuilder<> b(ret); b.CreateCall(leave);
            }
        for(auto &bb:*f) for(auto &ins:bb)
            if(!ins.getDebugLoc()) ins.setDebugLoc(DILocation::get(c,1,0,sp));
    }
    di.finalize();
    if (verifyModule(m, &errs())) {
        aot_set_last_error("DWARF continuation IR verification failed (unsupported live value)");
        return false;
    }
    return true;
}
