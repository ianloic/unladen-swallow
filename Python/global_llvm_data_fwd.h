/* Forward declares some functions using PyGlobalLlvmData so that C
   files can use them.  See global_llvm_data.h for the full C++
   interface. */
#ifndef PYTHON_GLOBAL_LLVM_DATA_FWD_H
#define PYTHON_GLOBAL_LLVM_DATA_FWD_H
#ifdef __cplusplus
extern "C" {
#endif

struct PyGlobalLlvmData *PyGlobalLlvmData_New();
void PyGlobalLlvmData_Clear(struct PyGlobalLlvmData *);
void PyGlobalLlvmData_Free(struct PyGlobalLlvmData *);

#ifdef __cplusplus
}
#endif
#endif  /* PYTHON_GLOBAL_LLVM_DATA_FWD_H */
