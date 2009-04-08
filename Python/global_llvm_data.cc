#include "Python/global_llvm_data.h"

#include "Python.h"

#include "_llvmfunctionobject.h"
#include "_llvmmoduleobject.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/Function.h"
#include "llvm/Module.h"
#include "llvm/ModuleProvider.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Transforms/Scalar.h"

namespace py {

/// Temporarily replaces the ModuleProvider for a particular
/// FunctionPassManager so that it can operate on an arbitrary
/// function.  Unlike ExistingModuleProvider, does not take ownership
/// of the Module.
class TempModuleProvider : public llvm::ModuleProvider {
public:
    TempModuleProvider(llvm::FunctionPassManager &fpm, llvm::Module *m)
        : fpm_(fpm) {
        TheModule = m;
        fpm_.setModuleProvider(this);
    }
    ~TempModuleProvider() {
        fpm_.setModuleProvider(NULL);
        // Stop ~ModuleProvider from deleting the module.
        TheModule = NULL;
    }
    bool materializeFunction(llvm::Function *, std::string * = 0) {
        return false;
    }
    llvm::Module* materializeModule(std::string * = 0) { return TheModule; }

private:
    llvm::FunctionPassManager &fpm_;
};

}  // namespace py

PyGlobalLlvmData *
PyGlobalLlvmData_New()
{
    return new PyGlobalLlvmData;
}

void
PyGlobalLlvmData_Clear(PyGlobalLlvmData *)
{
    // So far, do nothing.
}

void
PyGlobalLlvmData_Free(PyGlobalLlvmData * global_data)
{
    delete global_data;
}

PyGlobalLlvmData::PyGlobalLlvmData()
    : optimizations_0_(NULL),
      optimizations_1_(NULL),
      optimizations_2_(NULL)
{
    std::string error;
    engine_ = llvm::ExecutionEngine::create(
        new llvm::ExistingModuleProvider(new llvm::Module("<dummy>")),
        // Don't force the interpreter (use JIT if possible).
        false,
        &error,
        // JIT slowly, to produce better machine code.  TODO: We'll
        // almost certainly want to make this configurable per
        // function.
        false);
    if (engine_ == NULL) {
        Py_FatalError(error.c_str());
    }

    InitializeOptimizations();
}

void
PyGlobalLlvmData::InitializeOptimizations()
{
    optimizations_0_.add(new llvm::TargetData(*engine_->getTargetData()));
    optimizations_1_.add(new llvm::TargetData(*engine_->getTargetData()));
    optimizations_2_.add(new llvm::TargetData(*engine_->getTargetData()));

    // optimizations_0_ only consists of optimizations that speed up
    // a function that only runs once.

    // Lw: ...1; br Lx ; Lx: ...2  --> Lw: ...1 ...2
    optimizations_0_.add(llvm::createCFGSimplificationPass());

    // optimizations_1_ consists of optimizations that speed up a
    // function that runs a few times but don't take too long
    // themselves.

    // Lw: br %cond Lx, Ly ; Lx: br %cond Lz, Lv  --> Lw: br %cond Lz, Ly
    optimizations_1_.add(llvm::createJumpThreadingPass());
    // -> SSA form.
    optimizations_1_.add(llvm::createPromoteMemoryToRegisterPass());
    optimizations_1_.add(llvm::createInstructionCombiningPass());

    // optimizations_2_ consists of all optimizations that improve
    // the code at all.  We don't yet use any profiling data for this,
    // though.
    optimizations_2_.add(llvm::createScalarReplAggregatesPass());
    optimizations_2_.add(llvm::createLICMPass());
    optimizations_2_.add(llvm::createCondPropagationPass());
    optimizations_2_.add(llvm::createGVNPass());
    optimizations_2_.add(llvm::createSCCPPass());
    optimizations_2_.add(llvm::createAggressiveDCEPass());
    optimizations_2_.add(llvm::createCFGSimplificationPass());

    // TODO(jyasskin): Figure out how to run Module passes over a
    // single function at a time.
    //
    // optimizations_2_.add(llvm::createConstantMergePass());
    // optimizations_2_.add(llvm::createGlobalOptimizerPass());
    // optimizations_2_.add(llvm::createFunctionInliningPass());

    // Make sure the output is still good, for every optimization level.
    optimizations_0_.add(llvm::createVerifierPass());
    optimizations_1_.add(llvm::createVerifierPass());
    optimizations_2_.add(llvm::createVerifierPass());
}

PyGlobalLlvmData::~PyGlobalLlvmData()
{
    delete engine_;
}

int
PyGlobalLlvmData::Optimize(llvm::Function &f, int level)
{
    if (level < 0 || level > 2)
        return -1;
    llvm::FunctionPassManager *optimizations;
    switch (level) {
    case 0:
        optimizations = &this->optimizations_0_;
        break;
    case 1:
        optimizations = &this->optimizations_1_;
        break;
    case 2:
        optimizations = &this->optimizations_2_;
        break;
    }
    // TODO: Lock this.
    py::TempModuleProvider mp(*optimizations, f.getParent());
    optimizations->run(f);
    return 0;
}

int
PyGlobalLlvmData_Optimize(struct PyGlobalLlvmData *global_data,
                          PyObject *llvm_function, int level)
{
    if (!PyLlvmFunction_Check(llvm_function)) {
        PyErr_Format(PyExc_TypeError, "Expected LLVM Function object; got %s.",
                     llvm_function->ob_type->tp_name);
        return -1;
    }
    PyLlvmFunctionObject *function = (PyLlvmFunctionObject *)llvm_function;
    return global_data->Optimize(*(llvm::Function *)function->the_function,
                                 level);
}

int
_PyLlvm_Init()
{
    if (PyType_Ready(&PyLlvmModule_Type) < 0)
        return 0;
    if (PyType_Ready(&PyLlvmFunction_Type) < 0)
        return 0;

    llvm::cl::ParseEnvironmentOptions("python", "PYTHONLLVMFLAGS", "", true);

    return 1;
}

void
_PyLlvm_Fini()
{
    llvm::llvm_shutdown();
}
