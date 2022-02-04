#define DEBUG_TYPE "BoundsCheckerPass"
#include "utils.h"

namespace {
    class BoundsCheckerPass : public ModulePass {
    public:
        static char ID;
        BoundsCheckerPass() : ModulePass(ID) {}
        virtual bool runOnModule(Module &M) override;

    private:
        LLVMContext *context = nullptr;
        IntegerType *int32_type = nullptr;
        IRBuilder<> *IRB = nullptr;
        Value *getArrayDefinition(Value *I);
        Value *getOrCreateArraySize(Value *arrayDef);
        bool shouldCheck(Instruction *I);
        Value *getOrCreateTotalOffset(Value *I);

        Function *createSmartFunction(Function *F);

        DenseMap<Value *, Value *> inst_to_total_offset;
        DenseMap<PHINode *, Value *> phi_to_size;

        DenseMap<Argument *, Argument *> array_to_size_arg;

        Function *boundsCheckerFunction = nullptr;
    };
}


bool BoundsCheckerPass::shouldCheck(Instruction *I){
    if (auto *gepI = dyn_cast<GetElementPtrInst>(I))
        return (gepI->getNumIndices() == 1) ;
    else
        return false;
}

/**
 * Returns the oldest array definition or phi node
 **/
Value *BoundsCheckerPass::getArrayDefinition(Value *I){
    if (auto *gepI = dyn_cast<GetElementPtrInst>(I))
        return getArrayDefinition(gepI->getPointerOperand());
    else
        return I;
}

// constant value or variable
Value *BoundsCheckerPass::getOrCreateArraySize(Value *arrayDef){
    if (auto *allocaDef = dyn_cast<AllocaInst>(arrayDef))
        return allocaDef->getArraySize();
    else if (auto *phiDef = dyn_cast<PHINode>(arrayDef)){
        if (phi_to_size.count(phiDef))
            return phi_to_size.lookup(phiDef);
        IRB->SetInsertPoint(phiDef);
        auto *phiSize = IRB->CreatePHI(int32_type, phiDef->getNumIncomingValues());
        phi_to_size.insert(std::make_pair(phiDef, phiSize));
        for (unsigned i = 0; i < phiDef->getNumIncomingValues(); ++i){
            if (phiDef->getIncomingValue(i) == phiDef)
                phiSize->addIncoming(phiSize, phiDef->getIncomingBlock(i));
            else{
                auto *size = getOrCreateArraySize(getArrayDefinition(phiDef->getIncomingValue(i)));
                phiSize->addIncoming(size, phiDef->getIncomingBlock(i));
            }
        }
        return phiSize;
    }
    else if (auto *argumentDef [[maybe_unused]] = dyn_cast<Argument>(arrayDef))
        return array_to_size_arg.lookup(argumentDef);
    else if (auto *globalDef = dyn_cast<GlobalVariable>(arrayDef))
        return ConstantInt::get(int32_type, globalDef->getType()->getPointerElementType()->getArrayNumElements());
    else
        return nullptr;
}

Value *BoundsCheckerPass::getOrCreateTotalOffset(Value *I){
    // Instruction (or definition) is alrady processed
    if (inst_to_total_offset.count(I))
        return inst_to_total_offset.lookup(I);

    if (auto *gepI = dyn_cast<GetElementPtrInst>(I)){        
        // GEP accesses the array directly
        if (gepI->getPointerOperand() == getArrayDefinition(gepI)){
            inst_to_total_offset.insert(std::make_pair(gepI, gepI->getOperand(1)));
            return gepI->getOperand(1);
        }

        // GEP accesses an indirect pointer
        auto *previous_offset = getOrCreateTotalOffset(gepI->getPointerOperand());
        auto *additional_offset = gepI->getOperand(1);
        if (auto *const_previous_offset = dyn_cast<ConstantInt>(previous_offset))
            if (auto *const_additional_offset = dyn_cast<ConstantInt>(additional_offset)){
                // offset in constant and can be calculated at compile-time
                auto APtotal_offset = const_previous_offset->getValue() + const_additional_offset->getValue();
                auto *total_offset = ConstantInt::get(int32_type, APtotal_offset);
                inst_to_total_offset.insert(std::make_pair(gepI, total_offset));
                return total_offset;
            }
        
        // offset must be calculated at run-time
        IRB->SetInsertPoint(gepI);
        auto *total_offset = IRB->CreateAdd(previous_offset, additional_offset);
        inst_to_total_offset.insert(std::make_pair(gepI, total_offset));
        return total_offset;
    }else if (auto *phiI [[maybe_unused]] = dyn_cast<PHINode>(I)){
        // skip if all values are oririnal arrays (have no offset)
        if (all_of(phiI->incoming_values(), [&, this](Use &u){
                auto *phiValue = u.get();
                if (phiValue == phiI)
                    return true;
                else if (isa<PHINode>(phiValue)) // avoid infinite recursion, even if the real answer is 0
                    return false;
                else if (auto *constantOffset = dyn_cast<ConstantInt>(this->getOrCreateTotalOffset(phiValue)))
                    return constantOffset->getValue() == 0;
                else
                    return false;
                })){
            auto no_offset = ConstantInt::get(int32_type, 0);
            inst_to_total_offset.insert(std::make_pair(phiI, no_offset));
            return no_offset;
        }else{
            IRB->SetInsertPoint(phiI);
            auto *phiOffset = IRB->CreatePHI(int32_type, phiI->getNumIncomingValues());
            inst_to_total_offset.insert(std::make_pair(phiI, phiOffset));
            for (unsigned i = 0; i < phiI->getNumIncomingValues(); ++i){
                if (phiI->getIncomingValue(i) == phiI)
                    phiOffset->addIncoming(phiOffset, phiI->getIncomingBlock(i));
                else{
                    auto *offset = getOrCreateTotalOffset(phiI->getIncomingValue(i));
                    phiOffset->addIncoming(offset, phiI->getIncomingBlock(i));
                }
            }
            return phiOffset;
        }
    }else
        // Already some kind of array definition
        return ConstantInt::get(int32_type, 0);
}

bool isStandardMainFunction(Function *F){
    /* i32 main(i32, i8**) */
    if (F->getName() != "main" || F->arg_size() != 2 || !F->getReturnType()->isIntegerTy())
        return false;
    auto *arg1 = F->getArg(0);
    auto *arg2 = F->getArg(1);
    if (!arg1->getType()->isIntegerTy() ||
        !arg2->getType()->isPointerTy() ||
        !arg2->getType()->getPointerElementType()->isPointerTy() ||
        !arg2->getType()->getPointerElementType()->getPointerElementType()->isIntegerTy())
        return false;
    else
        return true;
}

/**
 * Returns a new function with size arguments and registers array-size associations
 * Returns `nullptr` if no additional size arguments are needed
 **/
Function *BoundsCheckerPass::createSmartFunction(Function *F){
    if (isStandardMainFunction(F)){
        array_to_size_arg.insert(std::make_pair(F->getArg(1), F->getArg(0)));
        return nullptr;
    }

    size_t array_argument_count = count_if(F->getFunctionType()->params(), [](Type *argT){return argT->isPointerTy();});
    if (array_argument_count == 0)
        return nullptr;
    auto size_argument_types = SmallVector<Type*, 10>(array_argument_count, int32_type);
    auto new_arguments = SmallVector<Argument *, 10>();
    auto smart_function = addParamsToFunction(F, size_argument_types, new_arguments);
    size_t arg_index = 0;
    for (auto &arg : smart_function->args())
        if (arg.getType()->isPointerTy())
            array_to_size_arg.insert(std::make_pair(&arg, new_arguments[arg_index++]));
    return smart_function;
}

bool BoundsCheckerPass::runOnModule(Module &M) {
    context = &M.getContext();

    IRBuilder<> IRB_ (*context);
    IRB = &IRB_;

    int32_type = Type::getInt32Ty(*context);
    auto *void_type = Type::getVoidTy(*context);

    auto checkerCallee = M.getOrInsertFunction("__coco_check_bounds", void_type, int32_type, int32_type);
    boundsCheckerFunction = dyn_cast<Function>(checkerCallee.getCallee());

    // create functions with new "smart" signatures
    auto function_replacements = DenseMap<Function *, Function *>();
    auto smart_functions = DenseSet<Function *>();
    for (auto &F : M){
        if (!shouldInstrument(&F))
            continue;
        if (auto smart_function = createSmartFunction(&F)){
            function_replacements.insert(std::make_pair(&F, smart_function));
            smart_functions.insert(smart_function);
        }else
            smart_functions.insert(&F);
    }

    bool is_changed = false;

    // add bound checking
    auto unchecked_geps = SmallVector<Instruction *, 100>();
    for (auto *F : smart_functions)
        for (auto &I : instructions(*F))
            if (shouldCheck(&I))
                unchecked_geps.append(1, &I);
    
    for (auto *check_point : unchecked_geps){
        // first do offset, then size to pass automated tests
        auto *offset = getOrCreateTotalOffset(check_point);
        auto *size = getOrCreateArraySize(getArrayDefinition(check_point));
        IRB->SetInsertPoint(check_point);
        IRB->CreateCall(boundsCheckerFunction, {offset, size});
        is_changed = true;
    }
    unchecked_geps.clear();
    inst_to_total_offset.clear();

    // update function calls
    auto old_calls = SmallVector<CallInst *, 100>();
    for (auto *F : smart_functions)
        for (auto &I : instructions(*F))
            if (auto *callI = dyn_cast<CallInst>(&I))
                if (function_replacements.count(callI->getCalledFunction()))
                    old_calls.append(1, callI);
    smart_functions.clear();

    for (auto *callI : old_calls){
        auto *smartFunction = function_replacements.lookup(callI->getCalledFunction());
        auto new_arguments = SmallVector<Value *, 10>(callI->args());
        for (auto *arg : new_arguments)
            if (arg->getType()->isPointerTy())
                new_arguments.append(1, getOrCreateArraySize(getArrayDefinition(arg)));
        IRB->SetInsertPoint(callI);
        auto new_call = IRB->CreateCall(smartFunction, new_arguments);
        callI->replaceAllUsesWith(new_call);
        is_changed = true;
    }
    function_replacements.clear();
    phi_to_size.clear();
    array_to_size_arg.clear();


    for (auto *callI : old_calls)
        callI->dropAllReferences();
    for (auto *callI : old_calls)
        callI->eraseFromParent();
    old_calls.clear();

    return is_changed;
}


char BoundsCheckerPass::ID = 0;
static RegisterPass<BoundsCheckerPass> X("coco-boundscheck", "Bounds Check Pass");
