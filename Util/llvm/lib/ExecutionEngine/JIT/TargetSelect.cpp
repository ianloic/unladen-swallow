//===-- TargetSelect.cpp - Target Chooser Code ----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This just asks the TargetRegistry for the appropriate JIT to use, and allows
// the user to specify a specific one on the commandline with -march=x. Clients
// should initialize targets prior to calling createJIT.
//
//===----------------------------------------------------------------------===//

#include "JIT.h"
#include "llvm/Module.h"
#include "llvm/ModuleProvider.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/System/Host.h"
#include "llvm/Target/SubtargetFeature.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetRegistry.h"
using namespace llvm;

static cl::opt<std::string>
MArch("march", cl::desc("Architecture to generate assembly for (see --version)"));

static cl::opt<std::string>
MCPU("mcpu",
  cl::desc("Target a specific cpu type (-mcpu=help for details)"),
  cl::value_desc("cpu-name"),
  cl::init(""));

static cl::list<std::string>
MAttrs("mattr",
  cl::CommaSeparated,
  cl::desc("Target specific attributes (-mattr=help for details)"),
  cl::value_desc("a1,+a2,-a3,..."));

/// selectTarget - Pick a target either via -march or by guessing the native
/// arch.  Add any CPU features specified via -mcpu or -mattr.
TargetMachine *JIT::selectTarget(ModuleProvider *MP, std::string *ErrorStr) {
  Triple TheTriple(sys::getHostTriple());

  // Adjust the triple to match what the user requested.
  if (!MArch.empty())
    TheTriple.setArch(Triple::getArchTypeForLLVMName(MArch));

  std::string Error;
  const Target *TheTarget =
    TargetRegistry::lookupTarget(TheTriple.getTriple(), Error);
  if (TheTarget == 0) {
    if (ErrorStr)
      *ErrorStr = Error;
    return 0;
  }

  if (!TheTarget->hasJIT()) {
    errs() << "WARNING: This target JIT is not designed for the host you are"
           << " running.  If bad things happen, please choose a different "
           << "-march switch.\n";
  }

  // Package up features to be passed to target/subtarget
  std::string FeaturesStr;
  if (!MCPU.empty() || !MAttrs.empty()) {
    SubtargetFeatures Features;
    Features.setCPU(MCPU);
    for (unsigned i = 0; i != MAttrs.size(); ++i)
      Features.AddFeature(MAttrs[i]);
    FeaturesStr = Features.getString();
  }

  // Allocate a target...
  TargetMachine *Target = 
    TheTarget->createTargetMachine(TheTriple.getTriple(), FeaturesStr);
  assert(Target && "Could not allocate target machine!");
  return Target;
}
