#ifndef Py_OPCODE_H
#define Py_OPCODE_H
#ifdef __cplusplus
extern "C" {
#endif

/* Instruction opcodes for compiled code */

#define INST_ADDR(x) x
enum PyOpcode {
#include "ceval-labels.i"
};
#undef INST_ADDR

enum cmp_op {PyCmp_LT=Py_LT, PyCmp_LE=Py_LE, PyCmp_EQ=Py_EQ, PyCmp_NE=Py_NE, PyCmp_GT=Py_GT, PyCmp_GE=Py_GE,
	     PyCmp_IN, PyCmp_NOT_IN, PyCmp_IS, PyCmp_IS_NOT, PyCmp_EXC_MATCH, PyCmp_BAD};

/* Helper macro for de-compiling superinstructions. */
#define HAS_ARG(op) ((op) == STORE_NAME || \
                     (op) == DELETE_NAME || \
                     (op) == UNPACK_SEQUENCE || \
                     (op) == FOR_ITER || \
                     (op) == STORE_ATTR || \
                     (op) == DELETE_ATTR || \
                     (op) == STORE_GLOBAL || \
                     (op) == DELETE_GLOBAL || \
                     (op) == LOAD_CONST || \
                     (op) == LOAD_NAME || \
                     (op) == BUILD_TUPLE || \
                     (op) == BUILD_LIST || \
                     (op) == BUILD_MAP || \
                     (op) == LOAD_ATTR || \
                     (op) == COMPARE_OP || \
                     (op) == JUMP_FORWARD || \
                     (op) == POP_JUMP_IF_FALSE || \
                     (op) == POP_JUMP_IF_TRUE || \
                     (op) == JUMP_IF_FALSE_OR_POP || \
                     (op) == JUMP_IF_TRUE_OR_POP || \
                     (op) == JUMP_ABSOLUTE || \
                     (op) == LOAD_GLOBAL || \
                     (op) == CONTINUE_LOOP || \
                     (op) == SETUP_LOOP || \
                     (op) == SETUP_EXCEPT || \
                     (op) == SETUP_FINALLY || \
                     (op) == LOAD_FAST || \
                     (op) == STORE_FAST || \
                     (op) == DELETE_FAST || \
                     (op) == CALL_FUNCTION || \
                     (op) == MAKE_CLOSURE || \
                     (op) == LOAD_CLOSURE || \
                     (op) == LOAD_DEREF || \
                     (op) == STORE_DEREF || \
                     (op) == CALL_FUNCTION_VAR_KW)

#ifdef __cplusplus
}
#endif
#endif /* !Py_OPCODE_H */
