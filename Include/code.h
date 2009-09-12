/* Definitions for bytecode */

#ifndef Py_CODE_H
#define Py_CODE_H

#include "_llvmfunctionobject.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bytecode object.  Keep this in sync with Util/PyTypeBuilder.h. */
typedef struct PyCodeObject {
    PyObject_HEAD
    int co_argcount;		/* #arguments, except *args */
    int co_nlocals;		/* #local variables */
    int co_stacksize;		/* #entries needed for evaluation stack */
    int co_flags;		/* CO_..., see below */
    PyObject *co_code;		/* instruction opcodes */
    PyObject *co_consts;	/* list (constants used) */
    PyObject *co_names;		/* list of strings (names used) */
    PyObject *co_varnames;	/* tuple of strings (local variable names) */
    PyObject *co_freevars;	/* tuple of strings (free variable names) */
    PyObject *co_cellvars;      /* tuple of strings (cell variable names) */
    /* The rest doesn't count for hash/cmp */
    PyObject *co_filename;	/* string (where it was loaded from) */
    PyObject *co_name;		/* string (name, for reference) */
    int co_firstlineno;		/* first source line number */
    PyObject *co_lnotab;	/* string (encoding addr<->lineno mapping) See
				   Objects/lnotab_notes.txt for details. */
    void *co_zombieframe;     /* for optimization only (see frameobject.c) */
#ifdef WITH_LLVM
    /* See
       http://code.google.com/p/unladen-swallow/wiki/FunctionCallingConvention
       for the calling convention. */
    _LlvmFunction *co_llvm_function;
    PyEvalFrameFunction co_native_function;
    struct PyFeedbackMap *co_runtime_feedback;
    /* True if interpretation will be done through the LLVM JIT. This exists
       only for ease of testing; the flag that matters is f_use_llvm on the
       frame object, which is influenced by co_use_llvm. */
    char co_use_llvm;
    /* Stores which optimizations have been applied to this code
       object.  Each level corresponds to an argument to
       PyGlobalLlvmData::Optimize().  Starts at -1 for unoptimized
       code. */
    int co_optimization;
    /* Number of times this code has been executed. This is used to decide
       which code objects are worth sending through LLVM. */
    int co_callcount;
    /* There are two kinds of guard failures: fatal failures (machine code is
       invalid, requires recompilation) and non-fatal failures (unexpected
       branch taken, machine code is still valid). If fatal guards are failing
       repeatedly in the same code object, we shouldn't waste time repeatedly
       recompiling this code. */
    int co_fatalbailcount;
    /* Because the globals dict is set on the frame, we record *which* globals
       dict we're assuming. */
    PyObject *co_assumed_globals;
    /* Because the builtins dict is set on the frame, we record *which* builtins
       dict we're assuming. */
    PyObject *co_assumed_builtins;
#endif
} PyCodeObject;

/* If co_fatalbailcount >= PY_MAX_FATAL_BAIL_COUNT, force this code to use the
   eval loop forever after. See the comment on the co_fatalbailcount field
   for more details. */
#define PY_MAX_FATALBAILCOUNT 1

/* Masks for co_flags above */
#define CO_OPTIMIZED    (1 << 0)
#define CO_NEWLOCALS    (1 << 1)
#define CO_VARARGS      (1 << 2)
#define CO_VARKEYWORDS  (1 << 3)
/* Is this a nested function? */
#define CO_NESTED       (1 << 4)
/* Is this function a generator? (aka, does it have a yield?) */
#define CO_GENERATOR    (1 << 5)
/* The CO_NOFREE flag is set if there are no free or cell variables.
   This information is redundant, but it allows a single flag test
   to determine whether there is any extra work to be done when the
   call frame is setup.
*/
#define CO_NOFREE       (1 << 6)
/* The CO_BLOCKSTACK flag is set if there are try/except blocks or with stmts.
   If there aren't any of these constructs, we can omit all block stack
   operations, which saves on codesize and JITting time. LLVM's optimizers can
   usually eliminate all of this code if it's not needed, but we'd like to
   avoid even generating the LLVM IR if possible.
*/
#define CO_BLOCKSTACK   (1 << 7)
/* The following CO_FDO_* flags control individual feedback-directed
   optimizations. These are aggregated into CO_ALL_FDO_OPTS. These optimizations
   are only triggered if we have data to support them, i.e., code compiled by
   setting co_optimization won't benefit from this. */
/* CO_FDO_GLOBALS: make assumptions about builtins/globals, for great justice */
#define CO_FDO_GLOBALS  (1 << 8)
#define CO_ALL_FDO_OPTS  (CO_FDO_GLOBALS)

#if 0
/* This is no longer used.  Stopped defining in 2.5, do not re-use. */
#define CO_GENERATOR_ALLOWED       (1 << 12)
#endif
#define CO_FUTURE_DIVISION    	   (1 << 13)
#define CO_FUTURE_ABSOLUTE_IMPORT  (1 << 14) /* do absolute imports by default */
#define CO_FUTURE_WITH_STATEMENT   (1 << 15)
#define CO_FUTURE_PRINT_FUNCTION   (1 << 16)
#define CO_FUTURE_UNICODE_LITERALS (1 << 17)

/* This should be defined if a future statement modifies the syntax.
   For example, when a keyword is added.
*/
#if 1
#define PY_PARSER_REQUIRES_FUTURE_KEYWORD
#endif

#define CO_MAXBLOCKS 20 /* Max static block nesting within a function */

PyAPI_DATA(PyTypeObject) PyCode_Type;

#define PyCode_Check(op) (Py_TYPE(op) == &PyCode_Type)
#define PyCode_GetNumFree(op) (PyTuple_GET_SIZE((op)->co_freevars))

/* Public interface */
PyAPI_FUNC(PyCodeObject *) PyCode_New(
	int, int, int, int, PyObject *, PyObject *, PyObject *, PyObject *,
	PyObject *, PyObject *, PyObject *, PyObject *, int, PyObject *);
        /* same as struct above */

/* Return the line number associated with the specified bytecode index
   in this code object.  Unless you want to be tied to the bytecode
   format, try to use PyFrame_GetLineNumber() instead. */
PyAPI_FUNC(int) PyCode_Addr2Line(PyCodeObject *, int);

/* for internal use only */
#define _PyCode_GETCODEPTR(co, pp) \
	((*Py_TYPE((co)->co_code)->tp_as_buffer->bf_getreadbuffer) \
	 ((co)->co_code, 0, (void **)(pp)))

typedef struct _addr_pair {
        int ap_lower;
        int ap_upper;
} PyAddrPair;

/* Update *bounds to describe the first and one-past-the-last instructions in the
   same line as lasti.  Return the number of that line.
*/
PyAPI_FUNC(int) _PyCode_CheckLineNumber(PyCodeObject* co,
                                        int lasti, PyAddrPair *bounds);

PyAPI_FUNC(PyObject*) PyCode_Optimize(PyObject *code, PyObject* consts,
                                      PyObject *names, PyObject *lineno_obj);

#ifdef WITH_LLVM
/* Compile a given function to LLVM IR, and apply a set of optimization passes.
   Returns -1 on error, 0 on success.

   You can use _PyCode_WatchGlobals() before calling this to advise the code
   object that it should make assumptions about globals/builtins.

   This should eventually be able to *re*compile bytecode to LLVM IR. See
   http://code.google.com/p/unladen-swallow/issues/detail?id=41. */
PyAPI_FUNC(int) _PyCode_ToOptimizedLlvmIr(PyCodeObject *code, int opt_level);

/* Register a code object to receive updates if its globals or builtins change.
   If the globals or builtins change, co_use_llvm will be set to 0; this causes
   the machine code to bail back to the interpreter to continue execution.

   This also adds CO_FDO_GLOBALS to the code object's co_flags bit array on
   success.

   Returns 0 on success, -1 on serious failure. "Serious failure" here means
   something that we absolutely cannot recover from (out-of-memory is the big
   one) and needs to be conveyed to the user. There are recoverable failure
   modes (globals == NULL, builtins == NULL, etc) that merely disable the
   optimization and return 0. */
PyAPI_FUNC(int) _PyCode_WatchGlobals(PyCodeObject *code,
                                     PyObject *globals, PyObject *builtins);


/* Perform any steps needed to mark a function's machine code as invalid.
   Individual fatal guard failures may need to do extra work on their own to
   clean up any special references/data they may have created, but calling this
   function will ensure that `code`'s machine code equivalent will not be
   called again. */
PyAPI_FUNC(void) _PyCode_InvalidateMachineCode(PyCodeObject *code);
#endif

#ifdef __cplusplus
}
#endif
#endif /* !Py_CODE_H */
