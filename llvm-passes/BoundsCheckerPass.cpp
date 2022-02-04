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

        DenseMap<Value *, Value *> inst_to_total_offset;
        DenseMap<PHINode *, Value *> phi_to_size;
        // DenseMap<PHINode *, Value *> phi_to_offset;

        Function *boundsCheckerFunction = nullptr;
    };
}


bool BoundsCheckerPass::shouldCheck(Instruction *I){
    if (auto *gepI = dyn_cast<GetElementPtrInst>(I))
        return (gepI->getNumIndices() == 1) ;// || I->hasAllZeroIndices();
    return false;
}


/**
 * Returns the oldest array definition or phi node
 * Returns `nullptr` is not an array access
 **/
Value *BoundsCheckerPass::getArrayDefinition(Value *I){
    if (auto *gepI = dyn_cast<GetElementPtrInst>(I))
        return getArrayDefinition(gepI->getPointerOperand());
    // add PHI case
    else //if (!origin->getType()->getPointerElementType()->isStructTy())
        return I; // exclude Struct GEPs
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
        for (unsigned i = 0; i < phiDef->getNumIncomingValues(); ++i){
            if (phiDef->getIncomingValue(i) == phiDef)
                phiSize->addIncoming(phiSize, phiDef->getIncomingBlock(i));
            else{
                auto *size = getOrCreateArraySize(getArrayDefinition(phiDef->getIncomingValue(i)));
                phiSize->addIncoming(size, phiDef->getIncomingBlock(i));
            }
        }
        phi_to_size.insert(std::make_pair(phiDef, phiSize));
        return phiSize;
    }
    else if (auto *argumentDef [[maybe_unused]] = dyn_cast<Argument>(arrayDef))
        // find function argument [or argc]
        return nullptr;
    else if (auto *globalDef = dyn_cast<GlobalVariable>(arrayDef))
        return ConstantInt::get(int32_type, globalDef->getType()->getPointerElementType()->getArrayNumElements());
    else
        return nullptr;
}


Value *BoundsCheckerPass::getOrCreateTotalOffset(Value *I){
    // GEP is alrady processed
    if (inst_to_total_offset.count(I))
        return inst_to_total_offset.lookup(I);

    /* Add PHI case */
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
    }
    if (auto *phiI [[maybe_unused]] = dyn_cast<PHINode>(I)){
        return nullptr;
    }
    return nullptr;
}


bool BoundsCheckerPass::runOnModule(Module &M) {
    context = &M.getContext();

    IRBuilder<> IRB_ (*context);
    IRB = &IRB_;

    int32_type = Type::getInt32Ty(*context);
    auto *void_type = Type::getVoidTy(*context);

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
                // auto *gepI = cast<GetElementPtrInst>(&I);
                auto *offset = getOrCreateTotalOffset(&I);
                auto *size = getOrCreateArraySize(getArrayDefinition(&I));
                IRB->SetInsertPoint(&I);
                IRB->CreateCall(boundsCheckerFunction, {offset, size});
            }

    return !inst_to_total_offset.empty();
}


char BoundsCheckerPass::ID = 0;
static RegisterPass<BoundsCheckerPass> X("coco-boundscheck", "Bounds Check Pass");
