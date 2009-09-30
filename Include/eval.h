
/* Interface to execute compiled code. This also includes private functions
   and structs shared between the bytecode VM and the LLVM compiler. */

#ifndef Py_EVAL_H
#define Py_EVAL_H
#ifdef __cplusplus
extern "C" {
#endif

PyAPI_FUNC(PyObject *) PyEval_CallObjectWithKeywords(
	PyObject *, PyObject *, PyObject *);

/* DLL-level Backwards compatibility: */
#undef PyEval_CallObject
PyAPI_FUNC(PyObject *) PyEval_CallObject(PyObject *, PyObject *);

/* Inline this */
#define PyEval_CallObject(func,arg) \
        PyEval_CallObjectWithKeywords(func, arg, (PyObject *)NULL)

PyAPI_FUNC(PyObject *) PyEval_CallFunction(PyObject *obj,
                                           const char *format, ...);
PyAPI_FUNC(PyObject *) PyEval_CallMethod(PyObject *obj,
                                         const char *methodname,
                                         const char *format, ...);

PyAPI_FUNC(void) PyEval_SetProfile(Py_tracefunc, PyObject *);
PyAPI_FUNC(void) PyEval_SetTrace(Py_tracefunc, PyObject *);

struct _frame; /* Avoid including frameobject.h */

PyAPI_FUNC(PyObject *) PyEval_GetBuiltins(void);
PyAPI_FUNC(PyObject *) PyEval_GetGlobals(void);
PyAPI_FUNC(PyObject *) PyEval_GetLocals(void);
PyAPI_FUNC(struct _frame *) PyEval_GetFrame(void);
PyAPI_FUNC(int) PyEval_GetRestricted(void);

/* Look at the current frame's (if any) code's co_flags, and turn on
   the corresponding compiler flags in cf->cf_flags.  Return 1 if any
   flag was set, else return 0. */
PyAPI_FUNC(int) PyEval_MergeCompilerFlags(PyCompilerFlags *cf);

PyAPI_FUNC(int) Py_FlushLine(void);

PyAPI_FUNC(int) Py_AddPendingCall(int (*func)(void *), void *arg);
PyAPI_FUNC(int) Py_MakePendingCalls(void);

/* Protection against deeply nested recursive calls */
PyAPI_FUNC(void) Py_SetRecursionLimit(int);
PyAPI_FUNC(int) Py_GetRecursionLimit(void);

#define Py_EnterRecursiveCall(where)                                    \
	    (_Py_MakeRecCheck(PyThreadState_GET()->recursion_depth) &&  \
	     _Py_CheckRecursiveCall(where))
#define Py_LeaveRecursiveCall()				\
	    (--PyThreadState_GET()->recursion_depth)
PyAPI_FUNC(int) _Py_CheckRecursiveCall(char *where);
PyAPI_DATA(int) _Py_CheckRecursionLimit;
#ifdef USE_STACKCHECK
#  define _Py_MakeRecCheck(x)  (++(x) > --_Py_CheckRecursionLimit)
#else
#  define _Py_MakeRecCheck(x)  (++(x) > _Py_CheckRecursionLimit)
#endif

PyAPI_FUNC(const char *) PyEval_GetFuncName(PyObject *);
PyAPI_FUNC(const char *) PyEval_GetFuncDesc(PyObject *);

PyAPI_FUNC(PyObject *) PyEval_GetCallStats(PyObject *);
PyAPI_FUNC(PyObject *) PyEval_EvalFrame(struct _frame *);
PyAPI_FUNC(PyObject *) PyEval_EvalFrameEx(struct _frame *f, int exc);

/* this used to be handled on a per-thread basis - now just two globals */
PyAPI_DATA(volatile int) _Py_Ticker;
PyAPI_DATA(int) _Py_CheckInterval;

#ifdef WITH_LLVM
/* Useful for debugging LLVM: if true, raise an exception if we bail from native
   code back to the interpreter. */
PyAPI_DATA(int) _Py_BailError;
#endif

/* Interface for threads.

   A module that plans to do a blocking system call (or something else
   that lasts a long time and doesn't touch Python data) can allow other
   threads to run as follows:

	...preparations here...
	Py_BEGIN_ALLOW_THREADS
	...blocking system call here...
	Py_END_ALLOW_THREADS
	...interpret result here...

   The Py_BEGIN_ALLOW_THREADS/Py_END_ALLOW_THREADS pair expands to a
   {}-surrounded block.
   To leave the block in the middle (e.g., with return), you must insert
   a line containing Py_BLOCK_THREADS before the return, e.g.

	if (...premature_exit...) {
		Py_BLOCK_THREADS
		PyErr_SetFromErrno(PyExc_IOError);
		return NULL;
	}

   An alternative is:

	Py_BLOCK_THREADS
	if (...premature_exit...) {
		PyErr_SetFromErrno(PyExc_IOError);
		return NULL;
	}
	Py_UNBLOCK_THREADS

   For convenience, that the value of 'errno' is restored across
   Py_END_ALLOW_THREADS and Py_BLOCK_THREADS.

   WARNING: NEVER NEST CALLS TO Py_BEGIN_ALLOW_THREADS AND
   Py_END_ALLOW_THREADS!!!

   The function PyEval_InitThreads() should be called only from
   initthread() in "threadmodule.c".

   Note that not yet all candidates have been converted to use this
   mechanism!
*/

PyAPI_FUNC(PyThreadState *) PyEval_SaveThread(void);
PyAPI_FUNC(void) PyEval_RestoreThread(PyThreadState *);

#ifdef WITH_THREAD

extern long _PyEval_main_thread;

PyAPI_FUNC(int)  PyEval_ThreadsInitialized(void);
PyAPI_FUNC(void) PyEval_InitThreads(void);
PyAPI_FUNC(void) PyEval_AcquireLock(void);
PyAPI_FUNC(void) PyEval_ReleaseLock(void);
PyAPI_FUNC(void) PyEval_AcquireThread(PyThreadState *tstate);
PyAPI_FUNC(void) PyEval_ReleaseThread(PyThreadState *tstate);
PyAPI_FUNC(void) PyEval_ReInitThreads(void);

#define Py_BEGIN_ALLOW_THREADS { \
			PyThreadState *_save; \
			_save = PyEval_SaveThread();
#define Py_BLOCK_THREADS	PyEval_RestoreThread(_save);
#define Py_UNBLOCK_THREADS	_save = PyEval_SaveThread();
#define Py_END_ALLOW_THREADS	PyEval_RestoreThread(_save); \
		 }

#else /* !WITH_THREAD */

#define Py_BEGIN_ALLOW_THREADS {
#define Py_BLOCK_THREADS
#define Py_UNBLOCK_THREADS
#define Py_END_ALLOW_THREADS }

#endif /* !WITH_THREAD */

PyAPI_FUNC(PyObject *) PyEval_EvalCode(PyCodeObject *, PyObject *, PyObject *);

PyAPI_FUNC(PyObject *) PyEval_EvalCodeEx(PyCodeObject *co,
					PyObject *globals,
					PyObject *locals,
					PyObject **args, int argc,
					PyObject **kwds, int kwdc,
					PyObject **defs, int defc,
					PyObject *closure);

PyAPI_FUNC(PyObject *) _PyEval_CallTracing(PyObject *func, PyObject *args);

/* Instrumentation functions. */

#ifdef Py_WITH_INSTRUMENTATION
/* Record that a given code object failed a fatal guard, thus invalidating its
   machine code function. We use this to analyze how many functions have their
   machine code invalidated, but continue to be called, which can negatively
   impact performance. */
PyAPI_FUNC(void) _PyEval_RecordFatalBail(PyCodeObject *code);

/* Record how many watchers a given dict has. This is used to track how many
   watchers the globals/builtins dicts are accumulating. */
PyAPI_FUNC(void) _PyEval_RecordWatcherCount(size_t watcher_count);
#else
#define _PyEval_RecordFatalBail(code)
#define _PyEval_RecordWatcherCount(watcher_count)
#endif  /* Py_WITH_INSTRUMENTATION */


/* Helper functions and objects shared by the bytecode and LLVM
   implementations. */

/* Status code to indicate why the Python stack is being unwound. */
enum _PyUnwindReason {
    UNWIND_NOUNWIND  = 0x0001,	/* No error */
    UNWIND_EXCEPTION = 0x0002,	/* Exception occurred */
    UNWIND_RERAISE   = 0x0004,	/* Exception re-raised by 'finally' */
    UNWIND_RETURN    = 0x0008,	/* 'return' statement */
    UNWIND_BREAK     = 0x0010,	/* 'break' statement */
    UNWIND_CONTINUE  = 0x0020,	/* 'continue' statement */
    UNWIND_YIELD     = 0x0040	/* 'yield' operator */
};

PyAPI_FUNC(void) _PyEval_SetExcInfo(PyThreadState *tstate, PyObject *type,
				    PyObject *value, PyObject *tb);
PyAPI_FUNC(void) _PyEval_ResetExcInfo(PyThreadState *);

PyAPI_FUNC(void) _PyEval_RaiseForUnboundLocal(struct _frame *frame,
                                              int var_index);
PyAPI_FUNC(int) _PyEval_CheckedExceptionMatches(PyObject *exc,
                                                PyObject *exc_type);

PyAPI_FUNC(int) _PyEval_SliceIndex(PyObject *, Py_ssize_t *);

PyAPI_FUNC(void) _PyEval_RaiseForGlobalNameError(PyObject *name);
PyAPI_FUNC(void) _PyEval_RaiseForUnboundFreeVar(struct _frame *, int);

PyAPI_FUNC(PyObject *) _PyEval_CallFunction(PyObject **, int, int);
PyAPI_FUNC(PyObject *) _PyEval_CallFunctionVarKw(PyObject **, int, int, int);

PyAPI_FUNC(PyObject *) _PyEval_ApplySlice(PyObject *, PyObject *, PyObject *);
PyAPI_FUNC(int) _PyEval_AssignSlice(PyObject *, PyObject *,
                                    PyObject *, PyObject *);

PyAPI_FUNC(enum _PyUnwindReason) _PyEval_DoRaise(PyObject *type, PyObject *val,
                                                 PyObject *tb);

PyAPI_FUNC(int) _PyEval_UnpackIterable(PyObject *, int, PyObject **);

PyAPI_FUNC(PyObject *) _PyEval_LoadName(struct _frame *, int);
PyAPI_FUNC(int) _PyEval_StoreName(struct _frame *, int, PyObject *);
PyAPI_FUNC(int) _PyEval_DeleteName(struct _frame *, int);

PyAPI_FUNC(int) _PyEval_HandlePyTickerExpired(PyThreadState *tstate);

/* Records whether tracing is on for any thread.  Counts the number of
 * threads for which tstate->c_tracefunc is non-NULL, so if the value
 * is 0, we know we don't have to check this thread's c_tracefunc.
 * This speeds up the if statement in PyEval_EvalFrame() after
 * fast_next_opcode. */
PyAPI_DATA(int) _Py_TracingPossible;

/* Records whether profiling is on for any thread.  Counts the number of
 * threads for which tstate->c_profilefunc is non-NULL, so if the value
 * is 0, we know we don't have to check this thread's c_profilefunc. */
PyAPI_DATA(int) _Py_ProfilingPossible;

PyAPI_FUNC(int) _PyEval_CallTrace(Py_tracefunc, PyObject *, struct _frame *,
                                  int, PyObject *);
PyAPI_FUNC(void) _PyEval_CallExcTrace(PyThreadState *, struct _frame *);
PyAPI_FUNC(int) _PyEval_TraceEnterFunction(PyThreadState *, struct _frame *);
PyAPI_FUNC(int) _PyEval_TraceLeaveFunction(PyThreadState *, struct _frame *,
                                           PyObject *, char, char);

#ifdef __cplusplus
}
#endif
#endif /* !Py_EVAL_H */
