/* Note: this file is not compiled if configured with --without-llvm. */
#include "Python.h"

#include "Python/global_llvm_data.h"
#include "Util/PyAliasAnalysis.h"
#include "Util/SingleFunctionInliner.h"
#include "Util/Stats.h"
#include "_llvmfunctionobject.h"

#include "llvm/Analysis/DebugInfo.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/CallingConv.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/Function.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Module.h"
#include "llvm/ModuleProvider.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/ValueHandle.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetSelect.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Scalar.h"

// Declare the function from initial_llvm_module.cc.
llvm::Module* FillInitialGlobalModule(llvm::Module*);

using llvm::FunctionPassManager;
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
    : module_(new Module("<main>", this->context())),
      module_provider_(new llvm::ExistingModuleProvider(module_)),
      debug_info_(Py_GenerateDebugInfoFlag ? new llvm::DIFactory(*module_)
                  : NULL),
      optimizations_(4, (FunctionPassManager*)NULL),
      num_globals_after_last_gc_(0)
{
    std::string error;
    llvm::InitializeNativeTarget();
    engine_ = llvm::ExecutionEngine::create(
        module_provider_,
        // Don't force the interpreter (use JIT if possible).
        false,
        &error,
        // JIT slowly, to produce better machine code.  TODO: We'll
        // almost certainly want to make this configurable per
        // function.
        llvm::CodeGenOpt::Default,
        // Allocate GlobalVariables separately from code.
        false);
    if (engine_ == NULL) {
        Py_FatalError(error.c_str());
    }

    this->module_->setDataLayout(
        this->engine_->getTargetData()->getStringRepresentation());

    // When we ask to JIT a function, we should also JIT other
    // functions that function depends on.  This lets us JIT in a
    // background thread to avoid blocking the main thread during
    // codegen, and (once the GIL is gone) JITting lazily is
    // thread-unsafe anyway.
    engine_->DisableLazyCompilation();

    this->constant_mirror_.reset(new PyConstantMirror(this));

    this->InstallInitialModule();

    this->InitializeOptimizations();
    this->gc_.add(llvm::createGlobalDCEPass());
}

void
PyGlobalLlvmData::InstallInitialModule()
{
    FillInitialGlobalModule(this->module_);

    for (llvm::Module::iterator it = this->module_->begin();
         it != this->module_->end(); ++it) {
        if (it->getName().find("_PyLlvm_Fast") == 0) {
            it->setCallingConv(llvm::CallingConv::Fast);
        }
    }

    // Fill the ExecutionEngine with the addresses of known global variables.
    for (Module::global_iterator it = this->module_->global_begin();
         it != this->module_->global_end(); ++it) {
        this->engine_->getOrEmitGlobalVariable(it);
    }
}

void
PyGlobalLlvmData::InitializeOptimizations()
{
    optimizations_[0] = new FunctionPassManager(this->module_provider_);

    FunctionPassManager *quick =
        new FunctionPassManager(this->module_provider_);
    optimizations_[1] = quick;
    quick->add(new llvm::TargetData(*engine_->getTargetData()));
    quick->add(llvm::createPromoteMemoryToRegisterPass());
    quick->add(llvm::createInstructionCombiningPass());
    quick->add(llvm::createCFGSimplificationPass());
    quick->add(llvm::createVerifierPass());

    // This is the default optimization used by the JIT. Higher levels
    // are for experimentation.
    FunctionPassManager *O2 =
        new FunctionPassManager(this->module_provider_);
    optimizations_[2] = O2;
    O2->add(new llvm::TargetData(*engine_->getTargetData()));
    O2->add(llvm::createCFGSimplificationPass());
    O2->add(PyCreateSingleFunctionInliningPass());
    O2->add(llvm::createJumpThreadingPass());
    O2->add(llvm::createPromoteMemoryToRegisterPass());
    O2->add(llvm::createInstructionCombiningPass());
    O2->add(llvm::createCFGSimplificationPass());
    O2->add(llvm::createScalarReplAggregatesPass());
    O2->add(CreatePyAliasAnalysis(*this));
    O2->add(llvm::createLICMPass());
    O2->add(llvm::createCondPropagationPass());
    O2->add(CreatePyAliasAnalysis(*this));
    O2->add(llvm::createGVNPass());
    O2->add(llvm::createSCCPPass());
    O2->add(llvm::createAggressiveDCEPass());
    O2->add(llvm::createCFGSimplificationPass());
    O2->add(llvm::createVerifierPass());


    // This is the list used by LLVM's opt tool's -O3 option.
    FunctionPassManager *optO3 =
        new FunctionPassManager(this->module_provider_);
    optimizations_[3] = optO3;
    optO3->add(new llvm::TargetData(*engine_->getTargetData()));

    using namespace llvm;
    // Commented lines are SCC or ModulePasses, which means they can't
    // be added to our FunctionPassManager.  TODO: Figure out how to
    // run them on a function at a time anyway.
    optO3->add(createCFGSimplificationPass());
    optO3->add(createScalarReplAggregatesPass());
    optO3->add(createInstructionCombiningPass());
    //optO3->add(createRaiseAllocationsPass());   // call %malloc -> malloc inst
    optO3->add(createCFGSimplificationPass());       // Clean up disgusting code
    optO3->add(createPromoteMemoryToRegisterPass()); // Kill useless allocas
    //optO3->add(createGlobalOptimizerPass());       // OptLevel out global vars
    //optO3->add(createGlobalDCEPass());          // Remove unused fns and globs
    //optO3->add(createIPConstantPropagationPass()); // IP Constant Propagation
    //optO3->add(createDeadArgEliminationPass());   // Dead argument elimination
    optO3->add(createInstructionCombiningPass());   // Clean up after IPCP & DAE
    optO3->add(createCFGSimplificationPass());      // Clean up after IPCP & DAE
    //optO3->add(createPruneEHPass());               // Remove dead EH info
    //optO3->add(createFunctionAttrsPass());         // Deduce function attrs
    optO3->add(PyCreateSingleFunctionInliningPass());
    //optO3->add(createFunctionInliningPass());      // Inline small functions
    //optO3->add(createArgumentPromotionPass());  // Scalarize uninlined fn args
    optO3->add(createSimplifyLibCallsPass());    // Library Call Optimizations
    optO3->add(createInstructionCombiningPass());  // Cleanup for scalarrepl.
    optO3->add(createJumpThreadingPass());         // Thread jumps.
    optO3->add(createCFGSimplificationPass());     // Merge & remove BBs
    optO3->add(createScalarReplAggregatesPass());  // Break up aggregate allocas
    optO3->add(createInstructionCombiningPass());  // Combine silly seq's
    optO3->add(createCondPropagationPass());       // Propagate conditionals
    optO3->add(createTailCallEliminationPass());   // Eliminate tail calls
    optO3->add(createCFGSimplificationPass());     // Merge & remove BBs
    optO3->add(createReassociatePass());           // Reassociate expressions
    optO3->add(createLoopRotatePass());            // Rotate Loop
    optO3->add(CreatePyAliasAnalysis(*this));
    optO3->add(createLICMPass());                  // Hoist loop invariants
    optO3->add(createLoopUnswitchPass());
    optO3->add(createLoopIndexSplitPass());        // Split loop index
    optO3->add(createInstructionCombiningPass());
    optO3->add(createIndVarSimplifyPass());        // Canonicalize indvars
    optO3->add(createLoopDeletionPass());          // Delete dead loops
    optO3->add(createLoopUnrollPass());          // Unroll small loops
    optO3->add(createInstructionCombiningPass()); // Clean up after the unroller
    optO3->add(CreatePyAliasAnalysis(*this));
    optO3->add(createGVNPass());                   // Remove redundancies
    optO3->add(CreatePyAliasAnalysis(*this));
    optO3->add(createMemCpyOptPass());            // Remove memcpy / form memset
    optO3->add(createSCCPPass());                  // Constant prop with SCCP

    // Run instcombine after redundancy elimination to exploit opportunities
    // opened up by them.
    optO3->add(createInstructionCombiningPass());
    optO3->add(createCondPropagationPass());       // Propagate conditionals
    optO3->add(CreatePyAliasAnalysis(*this));
    optO3->add(createDeadStoreEliminationPass());  // Delete dead stores
    optO3->add(createAggressiveDCEPass());   // Delete dead instructions
    optO3->add(createCFGSimplificationPass());     // Merge & remove BBs

    //optO3->add(createStripDeadPrototypesPass()); // Get rid of dead prototypes
    //optO3->add(createDeadTypeEliminationPass());   // Eliminate dead types

    //optO3->add(createConstantMergePass());       // Merge dup global constants
    optO3->add(llvm::createVerifierPass());
}

PyGlobalLlvmData::~PyGlobalLlvmData()
{
    this->constant_mirror_->python_shutting_down_ = true;
    for (size_t i = 0; i < this->optimizations_.size(); ++i) {
        delete this->optimizations_[i];
    }
    delete this->engine_;
}

int
PyGlobalLlvmData::Optimize(llvm::Function &f, int level)
{
    if (level < 0 || (size_t)level >= this->optimizations_.size())
        return -1;
    FunctionPassManager *opts_pm = this->optimizations_[level];
    assert(opts_pm != NULL && "Optimization was NULL");
    assert(this->module_ == f.getParent() &&
           "We assume that all functions belong to the same module.");
    opts_pm->run(f);
    return 0;
}

int
PyGlobalLlvmData_Optimize(struct PyGlobalLlvmData *global_data,
                          _LlvmFunction *llvm_function,
                          int level)
{
    return _LlvmFunction_Optimize(global_data, llvm_function, level);
}

#ifdef Py_WITH_INSTRUMENTATION
// Collect statistics about the time it takes to collect unused globals.
class GlobalGCTimes : public DataVectorStats<int64_t> {
public:
    GlobalGCTimes()
        : DataVectorStats<int64_t>("Time for a globaldce run in ns") {}
};

class GlobalGCCollected : public DataVectorStats<int> {
public:
    GlobalGCCollected()
        : DataVectorStats<int>("Number of globals collected by globaldce") {}
};

static llvm::ManagedStatic<GlobalGCTimes> global_gc_times;
static llvm::ManagedStatic<GlobalGCCollected> global_gc_collected;

#endif  // Py_WITH_INSTRUMENTATION

void
PyGlobalLlvmData::MaybeCollectUnusedGlobals()
{
    unsigned num_globals = this->module_->getGlobalList().size() +
        this->module_->getFunctionList().size();
    // Don't incur the cost of collecting globals if there are too few
    // of them, or if doing so now would cost a quadratic amount of
    // time as we allocate more long-lived globals.  The thresholds
    // here are just guesses, not tuned numbers.
    if (num_globals < 20 ||
        num_globals < (this->num_globals_after_last_gc_ +
                       (this->num_globals_after_last_gc_ >> 2)))
        return;

    {
#if Py_WITH_INSTRUMENTATION
        Timer timer(*global_gc_times);
#endif
        this->gc_.run(*this->module_);
    }
    num_globals_after_last_gc_ = this->module_->getGlobalList().size() +
        this->module_->getFunctionList().size();
#if Py_WITH_INSTRUMENTATION
    global_gc_collected->RecordDataPoint(
        num_globals - num_globals_after_last_gc_);
#endif
}

llvm::Value *
PyGlobalLlvmData::GetGlobalStringPtr(const std::string &value)
{
    // Use operator[] because we want to insert a new value if one
    // wasn't already present.
    llvm::WeakVH& the_string = this->constant_strings_[value];
    if (the_string == NULL) {
        llvm::Constant *str_const = llvm::ConstantArray::get(this->context(),
                                                             value, true);
        the_string = new llvm::GlobalVariable(
            *this->module_,
            str_const->getType(),
            true,  // Is constant.
            llvm::GlobalValue::InternalLinkage,
            str_const,
            value,  // Name.
            false);  // Not thread-local.
    }

    // the_string is a [(value->size()+1) x i8]*. C functions
    // expecting string constants instead expect an i8* pointing to
    // the first element.  We use GEP instead of bitcasting to make
    // type safety more obvious.
    const llvm::Type *int64_type = llvm::Type::getInt64Ty(this->context());
    llvm::Constant *indices[] = {
        llvm::ConstantInt::get(int64_type, 0),
        llvm::ConstantInt::get(int64_type, 0)
    };
    return llvm::ConstantExpr::getGetElementPtr(
        llvm::cast<llvm::Constant>(the_string), indices, 2);
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
