#define DEBUG_TYPE "DummyDTPass"
#include "utils.h"

namespace{
    class ADCEPass: public FunctionPass{
    static char ID;
    ADCEPass():FunctionPass(ID){}
    virtual bool runOnFunction(Function &F)override;
    };
}

bool ADCEPass::runOnFunction(Function &F){
    //Initial pass to markt rivially live and trivially
    //dead instructions. Perform this pass in depth-first
    //order on the CFG so that we never visit blocks that
    //are unreachable: those are trivially dead.
    auto LiveSet = DenseSet<Instruction>();
    // for (each BB in F in depth-first order)
    //     for (each instruction I in BB)
    //         if (isTriviallyLive(I))
    //             markLive(I)
    //         else if(I.use_empty())
    //             remove I from BB;
    // //Worklist to find new live instructions
    // while(WorkList is not empty){
    //     I = get instruction at head of worklist;
    //     if (basic block containing I is reachable)
    //         for (all operands op of I)
    //             if (operand op is an instruction)
    //                 markLive(op)
    // }
    // //Delete all instructions not in LiveSet. Since you
    // //may be deleting multiple instructions that may be
    // //in a def−use cycle, you must call I.dropAllReferences()
    // //on all of them before deleting any of them
    // //because you cannot delete a Value that has users.
    // for (each BB in F in any order)
    //     if (BB is reachable)
    //         for (each non-live instruction I in BB)
    //             I.dropAllReferences();
    // for (each BB in F in any order)
    //     if (BB is reachable)
    //         for(each non-live instruction I in BB)
    //             erase I from BB;
}

char ADCEPass::ID = 0;
RegisterPass<ADCEPass> X("coco-adce", "Does magic.");
