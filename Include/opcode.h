#ifndef Py_OPCODE_H
#define Py_OPCODE_H
#ifdef __cplusplus
extern "C" {
#endif


/* Instruction opcodes for compiled code */

enum _Py_opcode {
    STOP_CODE =		0,
    POP_TOP =		1,
    ROT_TWO =		2,
    ROT_THREE =		3,
    DUP_TOP =		4,
    ROT_FOUR =		5,

    NOP =		9,
    UNARY_POSITIVE =	10,
    UNARY_NEGATIVE =	11,
    UNARY_NOT =		12,
    UNARY_CONVERT =	13,

    UNARY_INVERT =	15,
    DUP_TOP_TWO =	16,
    DUP_TOP_THREE =	17,
    LIST_APPEND =	18,
    BINARY_POWER =	19,
    BINARY_MULTIPLY =	20,
    BINARY_DIVIDE =	21,
    BINARY_MODULO =	22,
    BINARY_ADD =	23,
    BINARY_SUBTRACT =	24,
    BINARY_SUBSCR =	25,
    BINARY_FLOOR_DIVIDE =	26,
    BINARY_TRUE_DIVIDE =	27,
    INPLACE_FLOOR_DIVIDE =	28,
    INPLACE_TRUE_DIVIDE =	29,
    /* SLICE_* must be contiguous and in this order */
    SLICE_NONE =	30,
    SLICE_LEFT =	31,
    SLICE_RIGHT =	32,
    SLICE_BOTH =	33,
    RAISE_VARARGS_ZERO =	34,
    RAISE_VARARGS_ONE =	35,
    RAISE_VARARGS_TWO =	36,
    RAISE_VARARGS_THREE =	37,
    BUILD_SLICE_TWO =	38,
    BUILD_SLICE_THREE =	39,
    /* STORE_SLICE_* must be contiguous and in this order */
    STORE_SLICE_NONE =	40,
    STORE_SLICE_LEFT =	41,
    STORE_SLICE_RIGHT =	42,
    STORE_SLICE_BOTH =	43,

    /* DELETE_SLICE_* must be contiguous and in this order */
    DELETE_SLICE_NONE =	50,
    DELETE_SLICE_LEFT =	51,
    DELETE_SLICE_RIGHT =	52,
    DELETE_SLICE_BOTH =	53,
    STORE_MAP =		54,
    INPLACE_ADD =	55,
    INPLACE_SUBTRACT =	56,
    INPLACE_MULTIPLY =	57,
    INPLACE_DIVIDE =	58,
    INPLACE_MODULO =	59,
    STORE_SUBSCR =	60,
    DELETE_SUBSCR =	61,
    BINARY_LSHIFT =	62,
    BINARY_RSHIFT =	63,
    BINARY_AND =	64,
    BINARY_XOR =	65,
    BINARY_OR =		66,
    INPLACE_POWER =	67,
    GET_ITER =		68,

/*  PRINT_EXPR =	70,	Replaced by #@displayhook builtin. */
/*  PRINT_ITEM =	71,	Other PRINT_* opcodes replaced by
    PRINT_NEWLINE =	72,	#@print_stmt builtin.
    PRINT_ITEM_TO =	73,
    PRINT_NEWLINE_TO =	74,	*/
    INPLACE_LSHIFT =	75,
    INPLACE_RSHIFT =	76,
    INPLACE_AND =	77,
    INPLACE_XOR =	78,
    INPLACE_OR =	79,
    BREAK_LOOP =	80,
    WITH_CLEANUP =	81,
/*  LOAD_LOCALS =	82,	Replaced by #@locals builtin. */
    RETURN_VALUE =	83,
/*  IMPORT_STAR =	84,	Replaced by #@import_star builtin. */
/*  EXEC_STMT =		85,	Replaced by #@exec builtin. */
    YIELD_VALUE =	86,
    POP_BLOCK =		87,
    END_FINALLY =	88,
/*  BUILD_CLASS =	xxx,	Replaced by #@buildclass builtin. */
    IMPORT_NAME =	89,

    HAVE_ARGUMENT =	90,	/* Opcodes from here have an argument: */

    STORE_NAME =	90,	/* Index in name list */
    DELETE_NAME =	91,	/* "" */
    UNPACK_SEQUENCE =	92,	/* Number of sequence items */
    FOR_ITER =		93,

    STORE_ATTR =	95,	/* Index in name list */
    DELETE_ATTR =	96,	/* "" */
    STORE_GLOBAL =	97,	/* "" */
    DELETE_GLOBAL =	98,	/* "" */

    LOAD_CONST =	100,	/* Index in const list */
    LOAD_NAME =		101,	/* Index in name list */
    BUILD_TUPLE =	102,	/* Number of tuple items */
    BUILD_LIST =	103,	/* Number of list items */
    BUILD_MAP =		104,	/* Always zero for now */
    LOAD_ATTR =		105,	/* Index in name list */
    COMPARE_OP =	106,	/* Comparison operator */
/*  IMPORT_FROM	=	108,	 Replaced by #@import_from builtin. */

    JUMP_FORWARD =	110,	/* Number of bytes to skip */
    JUMP_IF_FALSE_OR_POP =	111,  /* Target byte offset from beginning
                                    of code */
    JUMP_IF_TRUE_OR_POP =	112,	/* "" */
    JUMP_ABSOLUTE =	113,	/* "" */
    POP_JUMP_IF_FALSE =	114,	/* "" */
    POP_JUMP_IF_TRUE =	115,	/* "" */
    LOAD_GLOBAL =	116,	/* Index in name list */

    CONTINUE_LOOP =	119,	/* Start of loop (absolute) */
    SETUP_LOOP =	120,	/* Target address (relative) */
    SETUP_EXCEPT =	121,	/* "" */
    SETUP_FINALLY =	122,	/* "" */

    LOAD_FAST =		124,	/* Local variable number */
    STORE_FAST =	125,	/* Local variable number */
    DELETE_FAST =	126,	/* Local variable number */

/* CALL_FUNCTION_XXX opcodes defined below depend on this definition */
    CALL_FUNCTION =	131,	/* #args + (#kwargs<<8) */
/*  MAKE_FUNCTION =	132,	Replaced by #@make_function() builtin. */

    MAKE_CLOSURE =	134,      /* #free vars */
    LOAD_CLOSURE =	135,      /* Load free variable from closure */
    LOAD_DEREF =	136,      /* Load and dereference from closure cell */
    STORE_DEREF =	137,      /* Store into cell */

/* The next 3 opcodes must be contiguous and satisfy
   (CALL_FUNCTION_VAR - CALL_FUNCTION) & 3 == 1  */
    CALL_FUNCTION_VAR =	140,	/* #args + (#kwargs<<8) */
    CALL_FUNCTION_KW =	141,	/* #args + (#kwargs<<8) */
    CALL_FUNCTION_VAR_KW =	142,	/* #args + (#kwargs<<8) */

/* Support for opargs more than 16 bits long */
    EXTENDED_ARG =	143,
};

enum cmp_op {PyCmp_LT=Py_LT, PyCmp_LE=Py_LE, PyCmp_EQ=Py_EQ, PyCmp_NE=Py_NE, PyCmp_GT=Py_GT, PyCmp_GE=Py_GE,
	     PyCmp_IN, PyCmp_NOT_IN, PyCmp_IS, PyCmp_IS_NOT, PyCmp_EXC_MATCH, PyCmp_BAD};

#define HAS_ARG(op) ((op) >= HAVE_ARGUMENT)

#ifdef __cplusplus
}
#endif
#endif /* !Py_OPCODE_H */
