#ifndef PYTHON_LLVM_COMPILE_H
#define PYTHON_LLVM_COMPILE_H

#ifdef __cplusplus
extern "C" {
#endif


PyAPI_FUNC(_LlvmFunction *) _PyCode_To_Llvm(PyCodeObject *code);

#ifdef __cplusplus
}
#endif

#endif // PYTHON_LLVM_COMPILE_H
