/* Forward declares some functions using PyGlobalLlvmData so that C
   files can use them.  See global_llvm_data.h for the full C++
   interface. */
#ifndef PYTHON_GLOBAL_LLVM_DATA_FWD_H
#define PYTHON_GLOBAL_LLVM_DATA_FWD_H

#include "Python.h"

#ifdef __cplusplus
extern "C" {
#endif

struct PyGlobalLlvmData *PyGlobalLlvmData_New(void);
void PyGlobalLlvmData_Clear(struct PyGlobalLlvmData *);
void PyGlobalLlvmData_Free(struct PyGlobalLlvmData *);

PyAPI_FUNC(int) PyGlobalLlvmData_Optimize(struct PyGlobalLlvmData *,
                                          _LlvmFunction *, int);

/* Initializes LLVM and all of the LLVM wrapper types. */
int _PyLlvm_Init(void);

/* Finalizes LLVM. */
void _PyLlvm_Fini(void);

#ifdef __cplusplus
}
#endif
#endif  /* PYTHON_GLOBAL_LLVM_DATA_FWD_H */
