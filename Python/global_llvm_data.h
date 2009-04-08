// -*- C++ -*-
//
// Defines PyGlobalLlvmData, the per-interpreter state that LLVM needs
// to JIT-compile and optimize code.
#ifndef PYTHON_GLOBAL_LLVM_DATA_H
#define PYTHON_GLOBAL_LLVM_DATA_H

#ifndef __cplusplus
#error This header expects to be included only in C++ source
#endif

#include "Python/global_llvm_data_fwd.h"

#include "llvm/PassManager.h"

namespace llvm { class ExecutionEngine; }

struct PyGlobalLlvmData {
public:
    PyGlobalLlvmData();
    ~PyGlobalLlvmData();

    // Optimize f at a particular level. Currently, levels from 0 to 2
    // are valid. This function assumes that callers optimize any
    // particular function through each level in sequence.
    //
    // Returns 0 on success or -1 on failure (if level is out of
    // range, for example).
    int Optimize(llvm::Function &f, int level);

    llvm::ExecutionEngine *getExecutionEngine() { return engine_; }

private:
    void InitializeOptimizations();

    llvm::ExecutionEngine *engine_;  // Not modified after the constructor.

    llvm::FunctionPassManager optimizations_0_;
    llvm::FunctionPassManager optimizations_1_;
    llvm::FunctionPassManager optimizations_2_;
};

#endif  /* PYTHON_GLOBAL_LLVM_DATA_H */
