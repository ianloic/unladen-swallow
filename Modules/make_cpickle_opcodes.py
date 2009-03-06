#! /usr/bin/env python
"""Generate C code for the jump table of the cPickle unpickling interpreter
(for compilers supporting computed gotos or "labels-as-values", such as gcc).

This needs to be run when you change the pickle protocol, which you shouldn't
be doing.
"""

import imp
import os
import sys


def find_module(modname):
    """Finds and returns a module in the local dist/checkout."""
    modpath = os.path.join(
        os.path.dirname(os.path.dirname(__file__)), "Lib")
    return imp.load_module(modname, *imp.find_module(modname, [modpath]))

def write_contents(f):
    """Write C code contents to the target file object."""
    pickletools = find_module("pickletools")
    targets = ['TARGET_NULLBYTE'] + (['_unknown_opcode'] * 255)
    for opcode in pickletools.opcodes:
        targets[ord(opcode.code)] = "TARGET_%s" % opcode.name
    f.write("static void *opcode_targets[256] = {\n")
    f.write(",\n".join("\t&&%s" % s for s in targets))
    f.write("\n};\n")


if __name__ == "__main__":
    assert len(sys.argv) < 3, "Too many arguments"
    if len(sys.argv) == 2:
        target = sys.argv[1]
    else:
        target = "Modules/cPickle_opcodes.h"
    f = open(target, "w")
    try:
        write_contents(f)
    finally:
        f.close()
