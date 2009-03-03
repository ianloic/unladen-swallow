
"""
opcode module - potentially shared between dis and other modules which
operate on bytecodes (e.g. peephole optimizers).
"""

__all__ = ["cmp_op", "hasconst", "hasname", "hasjrel", "hasjabs",
           "haslocal", "hascompare", "hasfree", "opname", "opmap", "argdesc",
           "is_argument", "get_opcode", "get_argument",
           "make_opcode", "make_argument",
           "prim2super", "super2prim",
           "decode_superinstruction"]

import _opcode

cmp_op = ('<', '<=', '==', '!=', '>', '>=', 'in', 'not in', 'is',
        'is not', 'exception match', 'BAD')

hasconst = []
hasname = []
hasjrel = []
hasjabs = []
haslocal = []
hascompare = []
hasfree = []

opmap = {}
opname = [''] * 256
for op in range(256): opname[op] = '<%r>' % (op,)
del op

for i, name in enumerate(_opcode.opcodes):
    opname[i] = name
    opmap[name] = i
del i, name

prim2super = _opcode.superinstruction_table
super2prim = dict((super,prims) for prims, super in prim2super.iteritems())

argdesc = {}

def arg_op(name, description):
    argdesc[opmap[name]] = description

def name_op(name, description):
    op = opmap[name]
    hasname.append(op)
    argdesc[op] = description

def jrel_op(name, description):
    op = opmap[name]
    hasjrel.append(op)
    argdesc[op] = description

def jabs_op(name, description):
    op = opmap[name]
    hasjabs.append(op)
    argdesc[op] = description

# Instruction opcodes for compiled code

name_op('STORE_NAME', 'Index in name list')
name_op('DELETE_NAME', 'Index in name list')
arg_op('UNPACK_SEQUENCE', 'Number of tuple items')
jrel_op('FOR_ITER', '???')

name_op('STORE_ATTR', 'Index in name list')
name_op('DELETE_ATTR', 'Index in name list')
name_op('STORE_GLOBAL', 'Index in name list')
name_op('DELETE_GLOBAL', 'Index in name list')

arg_op('LOAD_CONST', 'Index in const list')
hasconst.append(opmap['LOAD_CONST'])
name_op('LOAD_NAME', 'Index in name list')
arg_op('BUILD_TUPLE', 'Number of tuple items')
arg_op('BUILD_LIST', 'Number of list items')
arg_op('BUILD_MAP', 'Number of dict entries (upto 255)')
name_op('LOAD_ATTR', 'Index in name list')
arg_op('COMPARE_OP', 'Comparison operator')
hascompare.append(opmap['COMPARE_OP'])
# name_op('IMPORT_NAME', 'Index in name list')   # Replaced by #@import_name.
# name_op('IMPORT_FROM', 'Index in name list')   # Replaced by #@import_from.

jrel_op('JUMP_FORWARD', 'Number of bytes to skip')
jabs_op('POP_JUMP_IF_FALSE', 'Target byte offset from beginning of code')
jabs_op('POP_JUMP_IF_TRUE', 'Target byte offset from beginning of code')
jabs_op('JUMP_IF_FALSE_OR_POP', 'Target byte offset from beginning of code')
jabs_op('JUMP_IF_TRUE_OR_POP', 'Target byte offset from beginning of code')
jabs_op('JUMP_ABSOLUTE', 'Target byte offset from beginning of code')

name_op('LOAD_GLOBAL', 'Index in name list')

jabs_op('CONTINUE_LOOP', 'Target address')
jrel_op('SETUP_LOOP', 'Distance to target address')
jrel_op('SETUP_EXCEPT', 'Distance to target address')
jrel_op('SETUP_FINALLY', 'Distance to target address')

arg_op('LOAD_FAST', 'Local variable number')
haslocal.append(opmap['LOAD_FAST'])
arg_op('STORE_FAST', 'Local variable number')
haslocal.append(opmap['STORE_FAST'])
arg_op('DELETE_FAST', 'Local variable number')
haslocal.append(opmap['DELETE_FAST'])

arg_op('CALL_FUNCTION', '#args + (#kwargs << 8)')
# arg_op('MAKE_FUNCTION', 'Number of args with default values')  Replaced by #@make_function calls.
arg_op('MAKE_CLOSURE', '???')
arg_op('LOAD_CLOSURE', '???')
hasfree.append(opmap['LOAD_CLOSURE'])
arg_op('LOAD_DEREF', '???')
hasfree.append(opmap['LOAD_DEREF'])
arg_op('STORE_DEREF', '???')
hasfree.append(opmap['STORE_DEREF'])

arg_op('CALL_FUNCTION_VAR_KW',
       '((#args + (#kwargs << 8)) << 16) + code;'
       ' where code&1 is true if there\'s a *args parameter,'
       ' and code&2 is true if there\'s a **kwargs parameter.')

del arg_op, name_op, jrel_op, jabs_op

# The following functions help Python code manipulate encoded
# instructions. When instructions are exposed to Python code, they get
# encoded as integers. The lowest bit is 1 is the instruction is an
# argument and 0 if the instruction is an opcode. The higher bits hold
# the value of the argument or opcode.
def is_argument(instruction):
    return bool(instruction & 1)

def get_opcode(instruction):
    assert not is_argument(instruction), '%d is argument, not opcode' % instruction
    return instruction >> 1

def get_argument(instruction):
    assert is_argument(instruction), '%d is opcode, not argument' % instruction
    return instruction >> 1

def make_opcode(op):
    return op << 1

def make_argument(arg):
    return (arg << 1) | 1

def decode_superinstruction(instructions, index):
    """Given a sequence of instructions, decodes the superinstruction
    at the beginning and returns the list of primitive instructions
    and arguments that it executes, and the instruction just after the
    superinstruction's arguments.

    This function does not change argument values that refer to
    instruction indices, so it alone can't convert the whole
    instruction sequence back into primitive instructions.
    """
    ops = [get_opcode(instructions[index])]
    while ops[0] in super2prim:
        ops[0:1] = super2prim[ops[0]]
    result = []
    next_arg = index + 1
    for op in ops:
        result.append(make_opcode(op))
        if op in argdesc:
            # The primitive takes an argument, so pull it off of the
            # instruction stream.
            assert is_argument(instructions[next_arg])
            result.append(instructions[next_arg])
            next_arg += 1
    assert (next_arg == len(instructions) or
            not is_argument(instructions[next_arg]))
    return result, next_arg
