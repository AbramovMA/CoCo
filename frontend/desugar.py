
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
        self.visit_children(node)

        #evaluate start and end 
        createEndVar = VarDef(Type('int'), '_endVar', node.end)
        createIndVar = VarDef(node._type, node.name, node.start)
        
        #cond
        cond = BinaryOp(VarUse(node.name, None), Operator.get('<'), VarUse('_endVar', None))

        #instructions for the body block
        incr = Assignment(VarUse(node.name, None), BinaryOp(VarUse(node.name, None), Operator.get('+'), IntConst(1)))
        whileBody = Block([node.body, incr])

        whileLoop = While(cond, whileBody)

        forLoopBlock = Block([createIndVar, createEndVar, whileLoop])

        self.visit_children(forLoopBlock)

        return forLoopBlock.at(node)

    


