#include "Util/PyAliasAnalysis.h"

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Module.h"

namespace {

using llvm::AliasAnalysis;
using llvm::Function;
using llvm::FunctionPass;
using llvm::GlobalVariable;
using llvm::Module;
using llvm::Pass;
using llvm::Value;
using llvm::dyn_cast;

class PyAliasAnalysis : public FunctionPass, public AliasAnalysis {
public:
    static char ID;
    PyAliasAnalysis() : FunctionPass(&ID), tracing_possible_(NULL) {}

    virtual void getAnalysisUsage(llvm::AnalysisUsage &usage) const {
        AliasAnalysis::getAnalysisUsage(usage);
        usage.setPreservesAll();
    }

    virtual bool doInitialization(Module&);
    virtual bool runOnFunction(Function&);

    virtual AliasResult alias(const Value *V1, unsigned V1Size,
                              const Value *V2, unsigned V2Size);

private:
    const GlobalVariable *tracing_possible_;
};

// The address of this variable identifies the pass.  See
// http://llvm.org/docs/WritingAnLLVMPass.html#basiccode.
char PyAliasAnalysis::ID = 0;

// Register this pass.
static llvm::RegisterPass<PyAliasAnalysis>
U("python-aa", "Python-specific Alias Analysis", false, true);

// Declare that we implement the AliasAnalysis interface.
static llvm::RegisterAnalysisGroup<AliasAnalysis> V(U);

bool
PyAliasAnalysis::doInitialization(Module& module)
{
    this->tracing_possible_ = module.getGlobalVariable("_Py_TracingPossible");
    return false;
}

bool
PyAliasAnalysis::runOnFunction(Function&)
{
    AliasAnalysis::InitializeAliasAnalysis(this);
    return false;
}

AliasAnalysis::AliasResult
PyAliasAnalysis::alias(const Value *V1, unsigned V1Size,
                       const Value *V2, unsigned V2Size)
{
    if (V1 == V2)
        return MustAlias;
    // No code copies the address of _Py_TracingPossible, so it can't
    // alias any other pointer.
    if (V1 == this->tracing_possible_ || V2 == this->tracing_possible_)
        return NoAlias;
    return AliasAnalysis::alias(V1, V1Size, V2, V2Size);
}

}  // anonymous namespace

Pass *CreatePyAliasAnalysis()
{
    return new PyAliasAnalysis();
}
