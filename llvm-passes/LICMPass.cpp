#define DEBUG_TYPE "LICMPass"
#include "utils.h"

bool safe_to_hoist(const Instruction *const I, const Loop *const L, const DominatorTree *const dom);
bool is_loop_invariant(const Instruction *const I, const Loop *const L);
bool loop_invariant_operands(const Instruction *const I, const Loop *const L);

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
    auto *loop_analyzer = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();

    auto *loop_header = L->getHeader();
    auto *loop_preheader = L->getLoopPreheader();

    auto dominated_blocks = SmallVector<BasicBlock *, 100>();
    dom->getDescendants(loop_header, dominated_blocks);

    auto dominated_loop_blocks = SmallVector<BasicBlock *, 100>();

    for (auto *BB : dominated_blocks)
        if (L->contains(BB) && loop_analyzer->getLoopFor(BB) == L)
            dominated_loop_blocks.push_back(BB);

    bool is_modified = false;
    bool able_to_move = true;

    while (able_to_move){
        auto movable_instructions = SmallVector<Instruction*, 100>();

        for (auto *BB : dominated_loop_blocks)
            for (auto &I : *BB)
                if (is_loop_invariant(&I, L) && safe_to_hoist(&I, L, dom))
                    movable_instructions.push_back(&I);

        able_to_move = !movable_instructions.empty();
        is_modified = is_modified || able_to_move;

        for (auto *I : movable_instructions){
            I->moveBefore(&loop_preheader->back());
        }
    }

    return is_modified;
}

bool safe_to_hoist(const Instruction *const I, const Loop *const L, const DominatorTree *const dom){
    bool has_no_side_effects = !I->mayHaveSideEffects();

    auto *parent_block = I->getParent();
    auto exit_blocks = SmallVector<BasicBlock*, 100>();
    L->getExitBlocks(exit_blocks);

    return has_no_side_effects ||
           all_of(exit_blocks, [&parent_block, &dom](BasicBlock *BB){return dom->dominates(parent_block, BB);});
}

bool is_loop_invariant(const Instruction *const I, const Loop *const L){
    bool possible_type = isa<BinaryOperator>(I) ||
                         isa<SelectInst>(I)     ||
                         isa<CastInst>(I)       ||
                         isa<GetElementPtrInst>(I);
    return possible_type && loop_invariant_operands(I, L);
}

bool loop_invariant_operands(const Instruction *const I, const Loop *const L){
    auto invariant_operand = [&L](Value *v){
        if (const Instruction *I = dyn_cast<Instruction>(v))
            return !L->contains(I);
        else
            return true;
    };
    return all_of(I->operands(), invariant_operand);
}


char LICMPass::ID = 0;
static RegisterPass<LICMPass> X("coco-licm", "Loop invariant code motion.");
