from util import ASTTransformer
from ast import Type, Operator, VarDef, ArrayDef, Assignment, Modification, \
        If, Block, VarUse, BinaryOp, IntConst, Return, While, DoWhile


class Desugarer(ASTTransformer):
    def __init__(self):
        self.varcache_stack = [{}]

    def makevar(self, name):
        # Generate a variable starting with an underscore (which is not allowed
        # in the language itself, so should be unique). To make the variable
        # unique, add a counter value if there are multiple generated variables
        # of the same name within the current scope.
        # A variable can be tagged as 'ssa' which means it is only assigned once
        # at its definition.
        name = '_' + name
        varcache = self.varcache_stack[-1]
        occurrences = varcache.setdefault(name, 0)
        varcache[name] += 1
        return name if not occurrences else name + str(occurrences + 1)

    def visitFunDef(self, node):
        self.varcache_stack.append({})
        self.visit_children(node)
        self.varcache_stack.pop()

    def visitModification(self, m):
        # from: lhs op= rhs
        # to:   lhs = lhs op rhs
        self.visit_children(m)
        return Assignment(m.ref, BinaryOp(m.ref, m.op, m.value)).at(m)

    def visitFor(self, node):
        # from:
        #     for (T X = min to max){
        #         Body;
        #     }
        # to:
        #     {
        #         T X = min
        #         T end = max
        #         while (X < end){
        #             {
        #                 Body;
        #             }
        #             X += 1;
        #         }
        #     }
        self.visit_children(node)

        # evaluate start and end 

        var = self.makevar('endVar')
        
        createEndVar = VarDef(Type.get('int'), var, node.end)
        createIndVar = VarDef(node._type, node.name, node.start)
        
        # the while condition
        cond = BinaryOp(VarUse(node.name), Operator.get('<'), VarUse(var))

        # induction variable incrementation
        incr = Modification(VarUse(node.name), Operator.get('+'), IntConst(1))
        whileBody = Block([node.body, incr])

        whileLoop = While(cond, whileBody)
        whileLoop.is_for = True

        # complete for -> while rewrite
        forLoopBlock = Block([createIndVar, createEndVar, whileLoop])
        
        self.visit(forLoopBlock)

        return forLoopBlock.at(node)

    


