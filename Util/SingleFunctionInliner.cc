#include "Util/SingleFunctionInliner.h"

#include "llvm/Attributes.h"
#include "llvm/BasicBlock.h"
#include "llvm/Function.h"
#include "llvm/Instructions.h"
#include "llvm/Transforms/Utils/Cloning.h"

namespace {

namespace Attribute = llvm::Attribute;
using llvm::BasicBlock;
using llvm::CallInst;
using llvm::Function;
using llvm::FunctionPass;
using llvm::InvokeInst;
using llvm::dyn_cast;
using llvm::isa;

// This class is derived from llvm's Inliner pass, but is tweaked to
// work one function at a time and to inline things according to
// Python-specific rules.
class SingleFunctionInliner : public FunctionPass {
public:
    static char ID;
    SingleFunctionInliner() : FunctionPass(&ID) {}

    virtual bool runOnFunction(Function &f)
    {
        bool changed = false;
        // Scan through and identify all call sites ahead of time so
        // that we only inline call sites in the original functions,
        // not call sites that result from inlining other functions.
        std::vector<CallInst*> call_sites;
        for (Function::iterator bb = f.begin(), e = f.end(); bb != e; ++bb) {
            for (BasicBlock::iterator inst = bb->begin(); inst != bb->end();
                 ++inst) {
                assert(!isa<InvokeInst>(inst) &&
                       "We don't expect any invoke instructions in Python.");
                CallInst *call = dyn_cast<CallInst>(inst);
                if (call == NULL)
                    continue;
                // This may miss inlining indirect calls that become
                // direct after inlining something else.
                Function *called_function = call->getCalledFunction();
                if (called_function != NULL &&
                    !called_function->isDeclaration() &&
                    called_function->hasFnAttr(Attribute::AlwaysInline)) {
                    call_sites.push_back(call);
                }
            }
        }

        // Actually inline the calls we found.
        for (int i = 0; i != call_sites.size(); ++i) {
            changed |= InlineFunction(call_sites[i]);
        }
        return changed;
    }
};

// The address of this variable identifies the pass.  See
// http://llvm.org/docs/WritingAnLLVMPass.html#basiccode.
char SingleFunctionInliner::ID = 0;

}  // anonymous namespace

FunctionPass *PyCreateSingleFunctionInliningPass()
{
    return new SingleFunctionInliner();
}
