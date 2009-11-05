
/* Method object interface */

#ifndef Py_METHODOBJECT_H
#define Py_METHODOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif

/* This is about the type 'builtin_function_or_method',
   not Python methods in user-defined classes.  See classobject.h
   for the latter. */

PyAPI_DATA(PyTypeObject) PyCFunction_Type;

#define PyCFunction_Check(op) (Py_TYPE(op) == &PyCFunction_Type)

/* PyCFunction works for METH_FIXED when arity==0 or arity==1. */
typedef PyObject *(*PyCFunction)(PyObject *, PyObject *);
typedef PyObject *(*PyCFunctionWithKeywords)(PyObject *, PyObject *,
					     PyObject *);
typedef PyObject *(*PyNoArgsFunction)(PyObject *);

/* Support for METH_FIXED with arity of two or three. */
typedef PyObject *(*PyCFunctionTwoArgs)(PyObject *, PyObject *, PyObject *);
typedef PyObject *(*PyCFunctionThreeArgs)(PyObject *, PyObject *,
                                          PyObject *, PyObject *);

PyAPI_FUNC(PyCFunction) PyCFunction_GetFunction(PyObject *);
PyAPI_FUNC(PyObject *) PyCFunction_GetSelf(PyObject *);
PyAPI_FUNC(int) PyCFunction_GetFlags(PyObject *);

/* Macros for direct access to these values. Type checks are *not*
   done, so use with care. */
#define PyCFunction_GET_FUNCTION(func) \
        (((PyCFunctionObject *)func) -> m_ml -> ml_meth)
#define PyCFunction_GET_SELF(func) \
	(((PyCFunctionObject *)func) -> m_self)
#define PyCFunction_GET_FLAGS(func) \
	(((PyCFunctionObject *)func) -> m_ml -> ml_flags)
/* GET_ARITY only makes sense for METH_FIXED functions. */
#define PyCFunction_GET_ARITY(func) \
	(assert(PyCFunction_GET_FLAGS(func) & METH_FIXED), \
	(((PyCFunctionObject *)func) -> m_ml -> ml_arity))
#define PyCFunction_GET_METHODDEF(func) \
	(((PyCFunctionObject *)func) -> m_ml)
PyAPI_FUNC(PyObject *) PyCFunction_Call(PyObject *, PyObject *, PyObject *);

typedef struct PyMethodDef {
    const char	*ml_name;	/* The name of the built-in function/method */
    PyCFunction  ml_meth;	/* The C function that implements it */
    int		 ml_flags;	/* Combination of METH_xxx flags, which mostly
				   describe the args expected by the C func */
    const char	*ml_doc;	/* The __doc__ attribute, or NULL */
    int          ml_arity;      /* Number of parameters for METH_FIXED funcs. */
} PyMethodDef;

PyAPI_FUNC(PyObject *) Py_FindMethod(PyMethodDef[], PyObject *, const char *);

#define PyCFunction_New(ML, SELF) PyCFunction_NewEx((ML), (SELF), NULL)
PyAPI_FUNC(PyObject *) PyCFunction_NewEx(PyMethodDef *, PyObject *,
					 PyObject *);

/* Flag passed to newmethodobject. These values are spaced out to leave room
   for future expansion without necessarily breaking ABI compatibility. */
#define METH_OLDARGS  0x0000
#define METH_VARARGS  0x0001
#define METH_KEYWORDS 0x0002
/* METH_NOARGS, METH_O and METH_FIXED must not be combined with the flags above.
   METH_FIXED supersedes METHO_O and METH_NOARGS. */
#define METH_O        0x0010     /* Function arity = 1 */
#define METH_FIXED    0x0020     /* Function arity = constant */
#define METH_NOARGS   METH_FIXED /* Arity = 0; backwards compatibility. */

/* METH_CLASS and METH_STATIC are a little different; these control
   the construction of methods for a class.  These cannot be used for
   functions in modules. */
#define METH_CLASS    0x0100
#define METH_STATIC   0x0200

/* METH_COEXIST allows a method to be entered eventhough a slot has
   already filled the entry.  When defined, the flag allows a separate
   method, "__contains__" for example, to coexist with a defined 
   slot like sq_contains. */

#define METH_COEXIST   0x1000

/* Maximum value for ml_arity. */
#define PY_MAX_FIXED_ARITY 3

typedef struct PyMethodChain {
    PyMethodDef *methods;		/* Methods of this type */
    struct PyMethodChain *link;	/* NULL or base type */
} PyMethodChain;

PyAPI_FUNC(PyObject *) Py_FindMethodInChain(PyMethodChain *, PyObject *,
                                            const char *);

/* Keep this in sync with Util/PyTypeBuilder.h. */
typedef struct PyCFunctionObject {
    PyObject_HEAD
    PyMethodDef *m_ml; /* Description of the C function to call */
    PyObject    *m_self; /* Passed as 'self' arg to the C func, can be NULL */
    PyObject    *m_module; /* The __module__ attribute, can be anything */
} PyCFunctionObject;

PyAPI_FUNC(int) PyCFunction_ClearFreeList(void);

#ifdef __cplusplus
}
#endif
#endif /* !Py_METHODOBJECT_H */
