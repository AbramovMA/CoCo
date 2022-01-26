#define DEBUG_TYPE "LICMPass"
#include "utils.h"

namespace {
    class LICMPass : public LoopPass {
    public:
        static char ID;
        LICMPass() : LoopPass(ID) {}
        virtual bool runOnLoop(Loop *L, LPPassManager &LPM) override;
    };
}


bool LICMPass::runOnLoop(Loop *L, LPPassManager &LPM){
    ;
    return false;
}


char LICMPass::ID = 0;
static RegisterPass<LICMPass> X("coco-licm", "Loop invariant code motion.");
