# Tests for the opcode module.

from test.test_support import run_unittest
import contextlib
import opcode
import unittest


@contextlib.contextmanager
def temp_replace(obj, attr, temp_value):
    """When used in a with block, sets obj.attr to temp_value within
    the block, and then sets it back when the block exits.

    attr is the string name of the attribute."""
    old_value = getattr(obj, attr)
    setattr(obj, attr, temp_value)
    try:
        yield
    finally:
        setattr(obj, attr, old_value)


OP = opcode.opmap


class DecodeSuperinstructionTests(unittest.TestCase):
    def test_no_superinstructions(self):
        # Tests when the instruction stream doesn't contain
        # superinstructions.
        with temp_replace(opcode, "super2prim", {}):
            instructions = [opcode.make_opcode(OP['LOAD_CONST']),
                            opcode.make_argument(3),
                            opcode.make_opcode(OP['RETURN_VALUE']),
                            ]
            self.assertEquals(opcode.decode_superinstruction(instructions, 0),
                              (instructions[0:2], 2))
            self.assertEquals(opcode.decode_superinstruction(instructions, 2),
                              (instructions[2:3], 3))

    def test_first_prim_takes_arg(self):
        # Tests when the first primitive in a superinstruction takes
        # an argument.
        with temp_replace(opcode, "super2prim",
                          {OP['RETURN_CONST']: (OP['LOAD_CONST'],
                                                OP['RETURN_VALUE']),
                           }):
            instructions = [opcode.make_opcode(OP['RETURN_CONST']),
                            opcode.make_argument(3),
                            ]
            self.assertEquals(opcode.decode_superinstruction(instructions, 0),
                              ([opcode.make_opcode(OP['LOAD_CONST']),
                                opcode.make_argument(3),
                                opcode.make_opcode(OP['RETURN_VALUE']),
                                ], 2))

    def test_second_prim_takes_arg(self):
        # Tests when the second primitive in a superinstruction takes
        # an argument.
        with temp_replace(opcode, "super2prim",
                          {# Made up:
                           OP['RETURN_CONST']: (OP['RETURN_VALUE'],
                                                OP['LOAD_CONST']),
                           }):
            instructions = [opcode.make_opcode(OP['RETURN_CONST']),
                            opcode.make_argument(3),
                            ]
            self.assertEquals(opcode.decode_superinstruction(instructions, 0),
                              ([opcode.make_opcode(OP['RETURN_VALUE']),
                                opcode.make_opcode(OP['LOAD_CONST']),
                                opcode.make_argument(3),
                                ], 2))


    def test_super_sequence(self):
        # Tests a superinstruction that has to be expanded twice.
        with temp_replace(opcode, "super2prim",
                          {OP['POP_LOAD_FAST']: (OP['POP_TOP'],
                                                OP['LOAD_FAST']),
                           # Made up:
                           OP['C_STORE_MAP']: (OP['POP_LOAD_FAST'],
                                                OP['LOAD_CONST']),
                           }):
            instructions = [opcode.make_opcode(OP['C_STORE_MAP']),
                            opcode.make_argument(3),
                            opcode.make_argument(4),
                            ]
            self.assertEquals(opcode.decode_superinstruction(instructions, 0),
                              ([opcode.make_opcode(OP['POP_TOP']),
                                opcode.make_opcode(OP['LOAD_FAST']),
                                opcode.make_argument(3),
                                opcode.make_opcode(OP['LOAD_CONST']),
                                opcode.make_argument(4),
                                ], 3))


def test_main():
    run_unittest(DecodeSuperinstructionTests)


if __name__ == '__main__':
    test_main()
