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
        Value *getArrayDefinition(GetElementPtrInst *I);
        Value *getArraySize(Value *arrayDef);
        bool shouldCheck(Instruction *I);
        Value *getOrCreateTotalOffset(GetElementPtrInst *I);

        DenseMap<GetElementPtrInst *, Value *> gep_to_total_offset;

        Function *boundsCheckerFunction = nullptr;
    };
}


[[maybe_unused]] bool BoundsCheckerPass::shouldCheck(Instruction *I){
    if (auto gepI = dyn_cast<GetElementPtrInst>(I))
        return (gepI->getNumIndices() == 1) ;// || I->hasAllZeroIndices();
    return false;
}


/**
 * Returns the oldest array definition or phi node
 * Returns `nullptr` is not an array access
 **/
Value *BoundsCheckerPass::getArrayDefinition(GetElementPtrInst *I){
    auto *origin = I->getPointerOperand();
    if (auto *parentGEP = dyn_cast<GetElementPtrInst>(origin))
        return getArrayDefinition(parentGEP);
    // add PHI case
    else //if (!origin->getType()->getPointerElementType()->isStructTy())
        return origin; // exclude Struct GEPs
}

// constant value or variable
Value *BoundsCheckerPass::getArraySize(Value *arrayDef){
    if (auto *allocaDef = dyn_cast<AllocaInst>(arrayDef))
        return allocaDef->getArraySize();
    else if (auto *argumentDef [[maybe_unused]] = dyn_cast<Argument>(arrayDef))
        // find function argument [or argc]
        return nullptr;
    else if (auto *phiDef [[maybe_unused]] = dyn_cast<PHINode>(arrayDef))
        return nullptr;
    else if (auto *globalDef [[maybe_unused]] = dyn_cast<GlobalVariable>(arrayDef))
        return ConstantInt::get(int32_type, globalDef->getType()->getPointerElementType()->getArrayNumElements());
    else
        return nullptr;
}


Value *BoundsCheckerPass::getOrCreateTotalOffset(GetElementPtrInst *I){
    // GEP is alrady processed
    if (gep_to_total_offset.count(I))
        return gep_to_total_offset.lookup(I);

    // GEP accesses the array directly
        /* Add PHI case */
    if (I->getPointerOperand() == getArrayDefinition(I)){
        gep_to_total_offset.insert(std::make_pair(I, I->getOperand(1)));
        return I->getOperand(1);
    }

    // GEP accesses an indirect pointer
    auto previous_offset = getOrCreateTotalOffset(dyn_cast<GetElementPtrInst>(I->getPointerOperand()));
    auto additional_offset = I->getOperand(1);
    if (auto const_previous_offset = dyn_cast<ConstantInt>(previous_offset))
        if (auto const_additional_offset = dyn_cast<ConstantInt>(additional_offset)){
            // offset in constant and can be calculated at compile-time
            auto APtotal_offset = const_previous_offset->getValue() + const_additional_offset->getValue();
            auto total_offset = ConstantInt::get(int32_type, APtotal_offset);
            gep_to_total_offset.insert(std::make_pair(I, total_offset));
            return total_offset;
        }
    
    // offset must be calculated at run-time
    IRB->SetInsertPoint(I);
    auto total_offset = IRB->CreateAdd(previous_offset, additional_offset);
    gep_to_total_offset.insert(std::make_pair(I, total_offset));
    return total_offset;
}


bool BoundsCheckerPass::runOnModule(Module &M) {
    context = &M.getContext();

    IRBuilder<> IRB_ (*context);
    IRB = &IRB_;

    int32_type = Type::getInt32Ty(*context);
    auto void_type = Type::getVoidTy(*context);

    auto checkerCallee = M.getOrInsertFunction("__coco_check_bounds", void_type, int32_type, int32_type);
    boundsCheckerFunction = dyn_cast<Function>(checkerCallee.getCallee());

    for (auto &F [[maybe_unused]] : M){
        /*
        Add array sizes to function definitions
        Replace function uses
        */
    }

    for (auto &F : M)
        for (auto &I : instructions(F))
            if (shouldCheck(&I)){
                auto *gepI = cast<GetElementPtrInst>(&I);
                auto *offset = getOrCreateTotalOffset(gepI);
                auto *size = getArraySize(getArrayDefinition(gepI));
                IRB->SetInsertPoint(gepI);
                IRB->CreateCall(boundsCheckerFunction, {offset, size});
            }

    return !gep_to_total_offset.empty();
}


char BoundsCheckerPass::ID = 0;
static RegisterPass<BoundsCheckerPass> X("coco-boundscheck", "Bounds Check Pass");
