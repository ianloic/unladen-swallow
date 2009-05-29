#include "Python/global_llvm_data.h"

#include "Python.h"

#include "Util/SingleFunctionInliner.h"
#include "_llvmfunctionobject.h"

#include "llvm/Analysis/Verifier.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/Function.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Module.h"
#include "llvm/ModuleProvider.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Transforms/Scalar.h"

// Declare the function from initial_llvm_module.cc.
llvm::Module* FillInitialGlobalModule(llvm::Module*);

using llvm::Module;

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

PyGlobalLlvmData *
PyGlobalLlvmData::Get()
{
    return PyThreadState_GET()->interp->global_llvm_data;
}

PyGlobalLlvmData::PyGlobalLlvmData()
    : module_(new Module("<main>")),
      module_provider_(new llvm::ExistingModuleProvider(module_)),
      optimizations_0_(module_provider_),
      optimizations_1_(module_provider_),
      optimizations_2_(module_provider_)
{
    std::string error;
    engine_ = llvm::ExecutionEngine::create(
        module_provider_,
        // Don't force the interpreter (use JIT if possible).
        false,
        &error,
        // JIT slowly, to produce better machine code.  TODO: We'll
        // almost certainly want to make this configurable per
        // function.
        llvm::CodeGenOpt::Default);
    if (engine_ == NULL) {
        Py_FatalError(error.c_str());
    }

    FillInitialGlobalModule(this->module_);

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

    optimizations_1_.add(PyCreateSingleFunctionInliningPass());
    // Lw: br %cond Lx, Ly ; Lx: br %cond Lz, Lv  --> Lw: br %cond Lz, Ly
    optimizations_1_.add(llvm::createJumpThreadingPass());
    // -> SSA form.
    optimizations_1_.add(llvm::createPromoteMemoryToRegisterPass());
    optimizations_1_.add(llvm::createInstructionCombiningPass());
    // Add CFG Simplification again because inlining produces
    // superfluous blocks.
    optimizations_1_.add(llvm::createCFGSimplificationPass());

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
    assert(module_ == f.getParent() &&
           "We assume that all functions belong to the same module.");
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
    return global_data->Optimize(
        *(llvm::Function *)_PyLlvmFunction_GetFunction(function),
        level);
}

llvm::Value *
PyGlobalLlvmData::GetGlobalStringPtr(const std::string &value)
{
    // Use operator[] because we want to insert a new value if one
    // wasn't already present.
    llvm::GlobalVariable *& the_string = this->constant_strings_[value];
    if (the_string == NULL) {
        llvm::Constant *str_const = llvm::ConstantArray::get(value, true);
        the_string = new llvm::GlobalVariable(
            str_const->getType(),
            true,  // Is constant.
            llvm::GlobalValue::InternalLinkage,
            str_const,
            value,  // Name.
            this->module_,
            false);  // Not thread-local.
    }

    // the_string is a [(value->size()+1) x i8]*. C functions
    // expecting string constants instead expect an i8* pointing to
    // the first element.  We use GEP instead of bitcasting to make
    // type safety more obvious.
    llvm::Constant *indices[] = {
        llvm::ConstantInt::get(llvm::Type::Int64Ty, 0),
        llvm::ConstantInt::get(llvm::Type::Int64Ty, 0)
    };
    return llvm::ConstantExpr::getGetElementPtr(the_string, indices, 2);
}

int
_PyLlvm_Init()
{
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
