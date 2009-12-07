// -*- C++ -*-
//
// Defines PyGlobalLlvmData, the per-interpreter state that LLVM needs
// to JIT-compile and optimize code.
#ifndef PYTHON_GLOBAL_LLVM_DATA_H
#define PYTHON_GLOBAL_LLVM_DATA_H

#ifndef __cplusplus
#error This header expects to be included only in C++ source
#endif

#ifdef WITH_LLVM
#include "Python/global_llvm_data_fwd.h"

#include "llvm/LLVMContext.h"
#include "llvm/PassManager.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/ValueHandle.h"

#include <string>

namespace llvm {
class DIFactory;
class ExecutionEngine;
class GlobalValue;
class GlobalVariable;
class Module;
class ModuleProvider;
class Value;
}

class PyConstantMirror;

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

    // Use this accessor for the LLVMContext rather than
    // getGlobalContext() directly so that we can more easily add new
    // contexts later.
    llvm::LLVMContext &context() const { return llvm::getGlobalContext(); }

    llvm::Module *module() { return this->module_; }
    llvm::ModuleProvider *module_provider() {
        return this->module_provider_; }

    PyConstantMirror &constant_mirror() { return *this->constant_mirror_; }

    /// This will be NULL if debug info generation is turned off.
    llvm::DIFactory *DebugInfo() { return this->debug_info_.get(); }

    // Runs globaldce to remove unreferenced global variables.
    // Globals still used in machine code must be referenced from IR
    // or this pass will delete them and crash.  This function uses
    // the same strategy as Python's gc to avoid running the
    // collection "too often"; see long_lived_pending and
    // long_lived_total in Modules/gcmodule.c for details.  Running
    // MaybeCollectUnusedGlobals() for the second time in a row with
    // no allocation in between should be a no-op.
    void MaybeCollectUnusedGlobals();

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
    llvm::ModuleProvider *module_provider_;
    llvm::Module *module_;
    llvm::OwningPtr<llvm::DIFactory> debug_info_;

    llvm::ExecutionEngine *engine_;  // Not modified after the constructor.

    std::vector<llvm::FunctionPassManager *> optimizations_;
    llvm::PassManager gc_;

    // Cached data in module_.  The WeakVH should only hold GlobalVariables.
    llvm::StringMap<llvm::WeakVH> constant_strings_;

    // All the GlobalValues that are backed by the stdlib bitcode file.  We're
    // not allowed to delete these.
    llvm::DenseSet<llvm::AssertingVH<const llvm::GlobalValue> > bitcode_gvs_;

    llvm::OwningPtr<PyConstantMirror> constant_mirror_;

    unsigned num_globals_after_last_gc_;
};
#endif  /* WITH_LLVM */

#endif  /* PYTHON_GLOBAL_LLVM_DATA_H */
