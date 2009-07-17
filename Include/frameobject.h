
/* Frame object interface */

#ifndef Py_FRAMEOBJECT_H
#define Py_FRAMEOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct PyTryBlock {
    int b_type;			/* what kind of block this is */
    int b_handler;		/* where to jump to find handler */
    int b_level;		/* value stack level to pop to */
} PyTryBlock;

/* Keep this in sync with Util/PyTypeBuilder.h. */
typedef struct _frame {
    PyObject_VAR_HEAD
    struct _frame *f_back;	/* previous frame, or NULL */
    PyCodeObject *f_code;	/* code segment */
    PyObject *f_builtins;	/* builtin symbol table (PyDictObject) */
    PyObject *f_globals;	/* global symbol table (PyDictObject) */
    PyObject *f_locals;		/* local symbol table (any mapping) */
    PyObject **f_valuestack;	/* points after the last local */
    /* Next free slot in f_valuestack.  Frame creation sets to f_valuestack.
       Frame evaluation usually NULLs it, but a frame that yields sets it
       to the current stack top. */
    PyObject **f_stacktop;
    PyObject *f_trace;		/* Trace function */

    /* If an exception is raised in this frame, the next three are used to
     * record the exception info (if any) originally in the thread state.  See
     * comments before set_exc_info() -- it's not obvious.
     * Invariant:  if _type is NULL, then so are _value and _traceback.
     * Desired invariant:  all three are NULL, or all three are non-NULL.  That
     * one isn't currently true, but "should be".
     */
    PyObject *f_exc_type, *f_exc_value, *f_exc_traceback;

    PyThreadState *f_tstate;
    /* When running through the bytecode interpreter, f_lasti records
       the index of the most recent instruction to start executing.
       It's used to resume generators, to let tracers change the
       current line number, and to look up the current line number
       through co_lnotab (See PyCode_Addr2Line).  Under LLVM, f_lasti
       is a negative number used to index basic blocks that we might
       want to resume from (for generators and line changes within
       tracing functions).  When f_lasti is negative (and hence
       meaningless), f_lineno is correct.  f_lasti is initialized to
       -1 when a function is first entered. */
    int f_lasti;

#ifdef WITH_LLVM
    /* At frame creation-time, we snapshot the state of f_code->co_use_llvm.
       Without this, Python code can cause active generators to flip between
       LLVM and the interpreter at will, which is a bad idea. */
    int f_use_llvm;
#endif

    /* Call PyFrame_GetLineNumber() instead of reading this field
       directly.  As of Unladen Swallow 2009Q2 f_lineno is valid when
       tracing is active (i.e. when f_trace is set) and when f_lasti
       is negative.  At other times we use PyCode_Addr2Line to
       calculate the line from the current bytecode index. */
    int f_lineno;		/* Current line number */
    unsigned char f_throwflag;	/* true if generator.throw() was called */
    unsigned char f_iblock;	/* index in f_blockstack */

#ifdef WITH_LLVM
    /* Holds a value from _PyFrameBailReason describing if we've
       bailed back from JITted code to the interpreter loop and
       why. */
    unsigned char f_bailed_from_llvm;
#endif

    PyTryBlock f_blockstack[CO_MAXBLOCKS]; /* for try and loop blocks */
    PyObject *f_localsplus[1];	/* locals+stack, dynamically sized */
} PyFrameObject;

enum _PyFrameBailReason {
    _PYFRAME_NO_BAIL,
    _PYFRAME_TRACE_ON_ENTRY,
    _PYFRAME_LINE_TRACE,
    _PYFRAME_BACKEDGE_TRACE,
};

/* Standard object interface */

PyAPI_DATA(PyTypeObject) PyFrame_Type;

#define PyFrame_Check(op) ((op)->ob_type == &PyFrame_Type)
#define PyFrame_IsRestricted(f) \
	((f)->f_builtins != (f)->f_tstate->interp->builtins)

PyAPI_FUNC(PyFrameObject *) PyFrame_New(PyThreadState *, PyCodeObject *,
                                       PyObject *, PyObject *);


/* The rest of the interface is specific for frame objects */

/* Block management functions */

PyAPI_FUNC(void) PyFrame_BlockSetup(PyFrameObject *, int, int, int);
PyAPI_FUNC(PyTryBlock *) PyFrame_BlockPop(PyFrameObject *);

/* Extend the value stack */

PyAPI_FUNC(PyObject **) PyFrame_ExtendStack(PyFrameObject *, int, int);

/* Conversions between "fast locals" and locals in dictionary */

PyAPI_FUNC(void) PyFrame_LocalsToFast(PyFrameObject *, int);
PyAPI_FUNC(void) PyFrame_FastToLocals(PyFrameObject *);

PyAPI_FUNC(int) PyFrame_ClearFreeList(void);

/* Return the line of code the frame is currently executing. */
PyAPI_FUNC(int) PyFrame_GetLineNumber(PyFrameObject *);

#ifdef __cplusplus
}
#endif
#endif /* !Py_FRAMEOBJECT_H */
