#ifndef PYTHON_LLVM_COMPILE_H
#define PYTHON_LLVM_COMPILE_H

#ifdef __cplusplus
extern "C" {
#endif


#ifdef WITH_LLVM
PyAPI_FUNC(_LlvmFunction *) _PyCode_ToLlvmIr(PyCodeObject *code);
#endif

#ifdef __cplusplus
}
#endif

#endif // PYTHON_LLVM_COMPILE_H
