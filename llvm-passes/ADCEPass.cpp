#define DEBUG_TYPE "DummyDTPass"
#include "utils.h"

namespace{
    class ADCEPass: public FunctionPass{
        public:
            static char ID;
            ADCEPass() : FunctionPass(ID){}
            virtual bool runOnFunction(Function &F) override;
    };
}

bool ADCEPass::runOnFunction(Function &F){
    bool is_changed = false;
    auto liveSet = DenseSet<Instruction*>();
    auto deadSet = DenseSet<Instruction*>();

    auto reachableBlocks = df_iterator_default_set<BasicBlock*>();

    for (auto &BB : depth_first_ext(&F, reachableBlocks)){
        for (auto &I : *BB){
            if (I.mayHaveSideEffects()){
                liveSet.insert(&I);
            }else if (I.use_empty()){
                deadSet.insert(&I);
            }
        }
    }

    for (auto &I : deadSet){
        is_changed = true;
        I->eraseFromParent();
    }

    auto workList = SmallVector<Instruction*, 1000>(liveSet.begin(), liveSet.end());
    
    while (!workList.empty()){
        auto I = workList.pop_back_val();
        if (reachableBlocks.count(I->getParent()))/*basic block containig I is reachable*/
            for (auto &op : I->operands())
                if (isa<Instruction>(op)){
                    // mark live (I)
                    workList.insert(workList.begin(), I);
                }
    }

    for (auto &I : workList){
        liveSet.insert(I);
    }

    // delete some magic
    for (auto BB : reachableBlocks){
        // for each non-live I in BB
        for (auto &I : *BB)
            if (!liveSet.count(&I)/*I is not live in BB*/)
                I.dropAllReferences();
    }

    for (auto BB : reachableBlocks){
        // for each non-live I in BB
        for (auto &I : *BB)
            if (!liveSet.count(&I)/*I is not live in BB*/){
                // remove I from BB
                I.eraseFromParent();
                is_changed = true;
            }
    }

    return is_changed;
}

char ADCEPass::ID = 0;
RegisterPass<ADCEPass> X("coco-adce", "Does magic.");
