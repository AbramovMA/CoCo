#define DEBUG_TYPE "ADCEPass"
#include "utils.h"

bool is_trivially_live(const Instruction *const I);
bool is_trivially_dead(const Instruction *const I);

namespace{
    class ADCEPass: public FunctionPass{
        public:
            static char ID;
            ADCEPass() : FunctionPass(ID){}
            virtual bool runOnFunction(Function &F) override;
    };
}

bool ADCEPass::runOnFunction(Function &F){
    auto reachable_blocks = df_iterator_default_set<BasicBlock *>();
    depth_first_ext(&F, reachable_blocks);

    auto trivially_dead_instructions = SmallVector<Instruction *, 100>();
    auto worklist_live = SmallVector<Instruction *, 100>();
    auto set_live = DenseSet<Instruction *>();

    for (auto *BB : reachable_blocks)
        for (auto &I : *BB)
            if (is_trivially_live(&I)){
                worklist_live.insert(worklist_live.begin(), &I);
                set_live.insert(&I);
            }else if (is_trivially_dead(&I))
                trivially_dead_instructions.push_back(&I);

    for (auto *I : trivially_dead_instructions){
        I->dropAllReferences();
        I->eraseFromParent();
    }

    bool is_modified = !trivially_dead_instructions.empty();
    trivially_dead_instructions.clear();

    while (!worklist_live.empty()){
        auto *I = worklist_live.pop_back_val();
        for (auto &arg : I->operands())
            if (auto *subI = dyn_cast<Instruction>(&arg))
                if (!set_live.count(subI)){
                    worklist_live.push_back(subI);
                    set_live.insert(subI);
                }
    }

    auto dead_instructions = SmallVector<Instruction *, 100>();

    for (auto *BB : reachable_blocks)
        for (auto &I : *BB)
            if (!set_live.count(&I))
                dead_instructions.push_back(&I);

    is_modified = is_modified || !dead_instructions.empty();

    for (auto *I : dead_instructions)
        I->dropAllReferences();

    for (auto *I : dead_instructions)
        I->eraseFromParent();

    return is_modified;
}

bool is_trivially_live(const Instruction *const I){
    bool is_terminator = I->isTerminator();
    bool is_volatile_load = false;
    if (auto *IL = dyn_cast<LoadInst>(I))
        is_volatile_load = IL->isVolatile();

    return is_terminator     ||
           is_volatile_load  ||
           isa<StoreInst>(I) ||
           isa<CallInst>(I)  ||
           I->mayHaveSideEffects();
}

bool is_trivially_dead(const Instruction *const I){
    return I->use_empty();
}


char ADCEPass::ID = 0;
static RegisterPass<ADCEPass> X("coco-adce", "Agressive dead code elimination pass");
