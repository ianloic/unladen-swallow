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

namespace llvm {
class ExecutionEngine;
class ExistingModuleProvider;
class Module;
}

struct PyGlobalLlvmData {
public:
    // Retrieves the PyGlobalLlvmData out of the interpreter state.
    static PyGlobalLlvmData *Get();

    PyGlobalLlvmData();
    ~PyGlobalLlvmData();

    // Optimize f at a particular level. Currently, levels from 0 to 2
    // are valid. This function assumes that callers optimize any
    // particular function through each level in sequence.
    //
    // Returns 0 on success or -1 on failure (if level is out of
    // range, for example).
    int Optimize(llvm::Function &f, int level);

    llvm::ExecutionEngine *getExecutionEngine() { return this->engine_; }

    llvm::Module *module() { return this->module_; }

private:
    void InitializeOptimizations();

    // We have a single global module that holds all compiled code.
    // Any cached global object that function definitions use will be
    // stored in here.  These are owned by engine_.
    llvm::Module *const module_;
    llvm::ExistingModuleProvider *const module_provider_;

    llvm::ExecutionEngine *engine_;  // Not modified after the constructor.

    llvm::FunctionPassManager optimizations_0_;
    llvm::FunctionPassManager optimizations_1_;
    llvm::FunctionPassManager optimizations_2_;
};

#endif  /* PYTHON_GLOBAL_LLVM_DATA_H */
