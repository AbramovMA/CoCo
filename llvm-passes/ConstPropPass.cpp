#define DEBUG_TYPE "ConstPropPass"
#include "utils.h"

bool isConstBinaryIntOp(const Instruction &I);
void simplify_int_instruction_and_find_users(Instruction *const I, SmallVector<Instruction*, 100> &worklist);
APInt perform_int_evaluation(const APInt arg1, const APInt arg2, const unsigned opcode);

bool isConstBinaryFloatOp(const Instruction &I);
void simplify_float_instruction_and_find_users(Instruction *const I, SmallVector<Instruction*, 100> &worklist);
APFloat perform_float_evaluation(const APFloat arg1, const APFloat arg2, const unsigned opcode);

namespace{
    class ConstPropPass : public FunctionPass{
        public:
            static char ID;
            ConstPropPass() : FunctionPass(ID){}
            virtual bool runOnFunction(Function &F) override;
    };
}

bool ConstPropPass::runOnFunction(Function &F){
    auto worklist = SmallVector<Instruction*, 100>();

    for (auto &BB : F)
        for (auto &I : BB)
            if (isConstBinaryIntOp(I) || isConstBinaryFloatOp(I))
                worklist.insert(worklist.begin(), &I);

    bool has_changed = !worklist.empty();

    while (!worklist.empty()){
        auto I = worklist.pop_back_val();
        if (isConstBinaryIntOp(*I))
            simplify_int_instruction_and_find_users(I, worklist);
        else if (isConstBinaryFloatOp(*I))
            simplify_float_instruction_and_find_users(I, worklist);
    }

    return has_changed;
}

bool isConstBinaryIntOp(const Instruction &I){
    if (auto *binOp = dyn_cast<BinaryOperator>(&I))
        if (isa<ConstantInt>(binOp->getOperand(0)) && isa<ConstantInt>(binOp->getOperand(1)))
                return true;
    return false;
}

void simplify_int_instruction_and_find_users(Instruction *const I, SmallVector<Instruction*, 100> &worklist){
    if (auto *binOp = dyn_cast<BinaryOperator>(I))
        if (auto *arg1 = dyn_cast<ConstantInt>(binOp->getOperand(0)))
            if (auto *arg2 = dyn_cast<ConstantInt>(binOp->getOperand(1))){
                auto opcode = binOp->getOpcode();
                auto arg1_value = arg1->getValue();
                auto arg2_value = arg2->getValue();
                auto result_value = perform_int_evaluation(arg1_value, arg2_value, opcode);
                auto result = ConstantInt::get(I->getContext(), result_value);

                for (auto &u : I->uses())
                    if (auto *i = dyn_cast<Instruction>(u.getUser()))
                        worklist.insert(worklist.begin(), i);
                I->replaceAllUsesWith(result);
            }
}

APInt perform_int_evaluation(const APInt arg1, const APInt arg2, const unsigned opcode){
    switch (opcode){
        case Instruction::Add:
            return arg1 + arg2;
        case Instruction::Sub:
            return arg1 - arg2;
        case Instruction::Mul:
            return arg1 * arg2;
        case Instruction::SDiv:
            return arg1.sdiv(arg2);
        case Instruction::UDiv:
            return arg1.udiv(arg2);
        case Instruction::SRem:
            return arg1.srem(arg2);
        case Instruction::URem:
            return arg1.urem(arg2);
        case Instruction::LShr:
            return arg1.lshr(arg2);
        case Instruction::AShr:
            return arg1.ashr(arg2);
        case Instruction::Shl:
            return arg1 << arg2;
        default:
            assert(0);
    }
}

bool isConstBinaryFloatOp(const Instruction &I){
    if (auto *binOp = dyn_cast<BinaryOperator>(&I))
        if (isa<ConstantFP>(binOp->getOperand(0)) && isa<ConstantFP>(binOp->getOperand(1)))
                return true;
    return false;
}

void simplify_float_instruction_and_find_users(Instruction *const I, SmallVector<Instruction*, 100> &worklist){
    if (auto *binOp = dyn_cast<BinaryOperator>(I))
        if (auto *arg1 = dyn_cast<ConstantFP>(binOp->getOperand(0)))
            if (auto *arg2 = dyn_cast<ConstantFP>(binOp->getOperand(1))){
                auto opcode = binOp->getOpcode();
                auto arg1_value = arg1->getValueAPF();
                auto arg2_value = arg2->getValueAPF();
                auto result_value = perform_float_evaluation(arg1_value, arg2_value, opcode);
                auto result = ConstantFP::get(I->getContext(), result_value);

                for (auto &u : I->uses())
                    if (auto *i = dyn_cast<Instruction>(u.getUser()))
                        worklist.insert(worklist.begin(), i);
                I->replaceAllUsesWith(result);
            }
}

APFloat perform_float_evaluation(const APFloat arg1, const APFloat arg2, const unsigned opcode){
    switch (opcode){
        case Instruction::Add:
            return arg1 + arg2;
        case Instruction::Sub:
            return arg1 - arg2;
        case Instruction::Mul:
            return arg1 * arg2;
        case Instruction::FDiv:
            return arg1 / arg2;
        case Instruction::FRem:
            {
                APFloat result(arg1);
                (void)result.remainder(arg2);
                return result;
            }
            break;
        default:
            assert(0);
    }
}

char ConstPropPass::ID = 0;
static RegisterPass<ConstPropPass> X("coco-constprop", "Constant propagation pass.");
