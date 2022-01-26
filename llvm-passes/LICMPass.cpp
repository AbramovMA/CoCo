#define DEBUG_TYPE "LICMPass"
#include "utils.h"

bool is_loop_invariant(const Instruction *const I, const Loop *const L);
bool safe_to_hoist(const Instruction *const I, const Loop *const L, const DominatorTree *const dom);

namespace {
    class LICMPass : public LoopPass {
        public:
            static char ID;
            LICMPass() : LoopPass(ID) {}
            virtual bool runOnLoop(Loop *L, LPPassManager &LPM) override;
            void getAnalysisUsage(AnalysisUsage &AU) const override;
    };
}

void LICMPass::getAnalysisUsage(AnalysisUsage &AU) const{
    getLoopAnalysisUsage(AU);
}


bool LICMPass::runOnLoop(Loop *L, LPPassManager &LPM){
    auto *dom = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    // DominatorTree *dom = nullptr;
    auto *loop_analyzer = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    // LoopInfo *loop_analyzer = nullptr;

    auto *loop_header = L->getHeader();
    auto dominated_blocks = SmallVector<BasicBlock *, 100>();
    dom->getDescendants(loop_header, dominated_blocks);

    //.isNotAlreadyContainedIn(nullptr, L)

    for (auto *BB : dominated_blocks)
        if (loop_analyzer->isNotAlreadyContainedIn(loop_analyzer->getLoopFor(BB), L)){
            LOG_LINE("  oops!");
            return false;
        }

        // for (auto *BB : subloop->blocks())
        //     if (is_contained(dominated_blocks, BB))
        //         return false;


    auto movable_instructions = SmallVector<Instruction*, 100>();

    for (auto *BB : L->blocks())
        for (auto &I : *BB)
            if (is_loop_invariant(&I, L) && safe_to_hoist(&I, L, dom))
                movable_instructions.push_back(&I);

    for (auto *I : movable_instructions){
        LOG_LINE(":    " << I->getName());
        I->moveBefore(&loop_header->back());
    }

    LOG_LINE(":    " << !movable_instructions.empty());

    return !movable_instructions.empty();
}

bool is_loop_invariant(const Instruction *const I, const Loop *const L){
    bool possible_type = isa<BinaryOperator>(I) ||
                         isa<SelectInst>(I)     ||
                         isa<CastInst>(I)       ||
                         isa<GetElementPtrInst>(I);
    return possible_type && L->hasLoopInvariantOperands(I); // REFACTOR
}

bool safe_to_hoist(const Instruction *const I, const Loop *const L, const DominatorTree *const dom){
    bool has_no_side_effects = !I->mayHaveSideEffects();

    auto *parent_block = I->getParent();
    auto exit_blocks = SmallVector<BasicBlock*, 100>();
    L->getExitBlocks(exit_blocks);

    return has_no_side_effects &&
           all_of(exit_blocks, [&parent_block, &dom](BasicBlock *BB){return dom->dominates(parent_block, BB);});
}



char LICMPass::ID = 0;
static RegisterPass<LICMPass> X("coco-licm", "Loop invariant code motion.");
