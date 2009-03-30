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

    void OptimizeQuickly(llvm::Function &f);

    llvm::ExecutionEngine *getExecutionEngine() { return engine_; }

private:
    void InitializeQuickOptimizations();

    llvm::ExecutionEngine *engine_;  // Not modified after the constructor.

    llvm::FunctionPassManager quick_optimizations_;
};

#endif  /* PYTHON_GLOBAL_LLVM_DATA_H */
