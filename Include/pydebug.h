
#ifndef Py_PYDEBUG_H
#define Py_PYDEBUG_H
#ifdef __cplusplus
extern "C" {
#endif

PyAPI_DATA(int) Py_DebugFlag;
PyAPI_DATA(int) Py_VerboseFlag;
PyAPI_DATA(int) Py_InteractiveFlag;
PyAPI_DATA(int) Py_InspectFlag;
PyAPI_DATA(int) Py_OptimizeFlag;
PyAPI_DATA(int) Py_NoSiteFlag;
PyAPI_DATA(int) Py_BytesWarningFlag;
PyAPI_DATA(int) Py_UseClassExceptionsFlag;
PyAPI_DATA(int) Py_FrozenFlag;
PyAPI_DATA(int) Py_TabcheckFlag;
PyAPI_DATA(int) Py_UnicodeFlag;
PyAPI_DATA(int) Py_IgnoreEnvironmentFlag;
PyAPI_DATA(int) Py_DivisionWarningFlag;
PyAPI_DATA(int) Py_DontWriteBytecodeFlag;
PyAPI_DATA(int) Py_NoUserSiteDirectory;
/* _XXX Py_QnewFlag should go away in 3.0.  It's true iff -Qnew is passed,
  on the command line, and is used in 2.2 by eval.cc to make all "/" divisions
  true divisions (which they will be in 3.0). */
PyAPI_DATA(int) _Py_QnewFlag;
/* Warn about 3.x issues */
PyAPI_DATA(int) Py_Py3kWarningFlag;
/* Show the total reference count after each execution. */
PyAPI_DATA(int) Py_ShowRefcountFlag;

/* Control when/how to JIT-compile Python functions to machine code. */
typedef enum {
    PY_JIT_NEVER,    /* Force use of the eval loop for all functions. */
    PY_JIT_WHENHOT,  /* JIT-compile hot function, optimizing heavily. */
    PY_JIT_ALWAYS,   /* Force use of LLVM for all functions. */
} Py_JitOpts;

/* Defaults to PY_JIT_WHENHOT. */
PyAPI_DATA(Py_JitOpts) Py_JitControl;

/* this is a wrapper around getenv() that pays attention to
   Py_IgnoreEnvironmentFlag.  It should be used for getting variables like
   PYTHONPATH and PYTHONHOME from the environment */
#define Py_GETENV(s) (Py_IgnoreEnvironmentFlag ? NULL : getenv(s))

PyAPI_FUNC(void) Py_FatalError(const char *message);

#ifdef __cplusplus
}
#endif
#endif /* !Py_PYDEBUG_H */
