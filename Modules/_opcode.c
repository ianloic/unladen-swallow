#include "Python.h"

PyMODINIT_FUNC
init_opcode(void)
{
    PyObject *m;

    m = Py_InitModule3("_opcode", NULL, "Opcode definition module.");
    if (m != NULL) {
        PyObject *opcode_tuple, *superinstruction_table;
        opcode_tuple = _PyEval_GetOpcodeNames();
        if (opcode_tuple != NULL)
            PyModule_AddObject(m, "opcodes", opcode_tuple);

        superinstruction_table = _PyEval_GetSuperinstructionDefinitions();
        if (superinstruction_table != NULL)
            PyModule_AddObject(m, "superinstruction_table",
                               superinstruction_table);
    }
}
