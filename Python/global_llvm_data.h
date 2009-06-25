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
#include "llvm/ADT/StringMap.h"

#include <string>

namespace llvm {
class ExecutionEngine;
class ExistingModuleProvider;
class GlobalVariable;
class Module;
class Value;
}

struct PyGlobalLlvmData {
public:
    // Retrieves the PyGlobalLlvmData out of the interpreter state.
    static PyGlobalLlvmData *Get();

    PyGlobalLlvmData();
    ~PyGlobalLlvmData();

    // Optimize f to a particular level. Currently, levels from 0 to 3
    // are valid.
    //
    // Returns 0 on success or -1 on failure (if level is out of
    // range, for example).
    int Optimize(llvm::Function &f, int level);

    llvm::ExecutionEngine *getExecutionEngine() { return this->engine_; }

    llvm::Module *module() { return this->module_; }

    // Helper functions for building functions in IR.

    // Returns an i8* pointing to a 0-terminated C string holding the
    // characters from value.  If two such strings have the same
    // value, only one global constant will be created in the Module.
    llvm::Value *GetGlobalStringPtr(const std::string &value);

private:
    // We use Clang to compile a number of C functions to LLVM IR. Install
    // those functions and set up any special calling conventions or attributes
    // we may want.
    void InstallInitialModule();

    void InitializeOptimizations();

    // We have a single global module that holds all compiled code.
    // Any cached global object that function definitions use will be
    // stored in here.  These are owned by engine_.
    llvm::Module *const module_;
    llvm::ExistingModuleProvider *const module_provider_;

    llvm::ExecutionEngine *engine_;  // Not modified after the constructor.

    std::vector<llvm::FunctionPassManager *> optimizations_;

    // Cached data in module_.  TODO(jyasskin): Make this hold WeakVHs
    // or other ValueHandles when we import them from LLVM trunk.
    llvm::StringMap<llvm::GlobalVariable *> constant_strings_;
};

#endif  /* PYTHON_GLOBAL_LLVM_DATA_H */
