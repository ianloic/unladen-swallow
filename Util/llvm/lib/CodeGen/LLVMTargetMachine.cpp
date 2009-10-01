//===-- LLVMTargetMachine.cpp - Implement the LLVMTargetMachine class -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the LLVMTargetMachine class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Target/TargetMachine.h"
#include "llvm/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Assembly/PrintModulePass.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/GCStrategy.h"
#include "llvm/CodeGen/MachineFunctionAnalysis.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/Target/TargetRegistry.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FormattedStream.h"
using namespace llvm;

namespace llvm {
  bool EnableFastISel;
}

static cl::opt<bool> PrintLSR("print-lsr-output", cl::Hidden,
    cl::desc("Print LLVM IR produced by the loop-reduce pass"));
static cl::opt<bool> PrintISelInput("print-isel-input", cl::Hidden,
    cl::desc("Print LLVM IR input to isel pass"));
static cl::opt<bool> PrintEmittedAsm("print-emitted-asm", cl::Hidden,
    cl::desc("Dump emitter generated instructions as assembly"));
static cl::opt<bool> PrintGCInfo("print-gc", cl::Hidden,
    cl::desc("Dump garbage collector data"));
static cl::opt<bool> HoistConstants("hoist-constants", cl::Hidden,
    cl::desc("Hoist constants out of loops"));
static cl::opt<bool> VerifyMachineCode("verify-machineinstrs", cl::Hidden,
    cl::desc("Verify generated machine code"),
    cl::init(getenv("LLVM_VERIFY_MACHINEINSTRS")!=NULL));

// Enable or disable FastISel. Both options are needed, because
// FastISel is enabled by default with -fast, and we wish to be
// able to enable or disable fast-isel independently from -O0.
static cl::opt<cl::boolOrDefault>
EnableFastISelOption("fast-isel", cl::Hidden,
  cl::desc("Enable the \"fast\" instruction selector"));


LLVMTargetMachine::LLVMTargetMachine(const Target &T,
                                     const std::string &TargetTriple)
  : TargetMachine(T) {
  AsmInfo = T.createAsmInfo(TargetTriple);
}



FileModel::Model
LLVMTargetMachine::addPassesToEmitFile(PassManagerBase &PM,
                                       formatted_raw_ostream &Out,
                                       CodeGenFileType FileType,
                                       CodeGenOpt::Level OptLevel) {
  // Add common CodeGen passes.
  if (addCommonCodeGenPasses(PM, OptLevel))
    return FileModel::Error;

  // Fold redundant debug labels.
  PM.add(createDebugLabelFoldingPass());

  if (PrintMachineCode)
    PM.add(createMachineFunctionPrinterPass(errs()));

  if (addPreEmitPass(PM, OptLevel) && PrintMachineCode)
    PM.add(createMachineFunctionPrinterPass(errs()));

  if (OptLevel != CodeGenOpt::None)
    PM.add(createCodePlacementOptPass());

  switch (FileType) {
  default:
    break;
  case TargetMachine::AssemblyFile:
    if (addAssemblyEmitter(PM, OptLevel, getAsmVerbosityDefault(), Out))
      return FileModel::Error;
    return FileModel::AsmFile;
  case TargetMachine::ObjectFile:
    if (getMachOWriterInfo())
      return FileModel::MachOFile;
    else if (getELFWriterInfo())
      return FileModel::ElfFile;
  }

  return FileModel::Error;
}

bool LLVMTargetMachine::addAssemblyEmitter(PassManagerBase &PM,
                                           CodeGenOpt::Level OptLevel,
                                           bool Verbose,
                                           formatted_raw_ostream &Out) {
  FunctionPass *Printer =
    getTarget().createAsmPrinter(Out, *this, getMCAsmInfo(), Verbose);
  if (!Printer)
    return true;

  PM.add(Printer);
  return false;
}

/// addPassesToEmitFileFinish - If the passes to emit the specified file had to
/// be split up (e.g., to add an object writer pass), this method can be used to
/// finish up adding passes to emit the file, if necessary.
bool LLVMTargetMachine::addPassesToEmitFileFinish(PassManagerBase &PM,
                                                  MachineCodeEmitter *MCE,
                                                  CodeGenOpt::Level OptLevel) {
  if (MCE)
    addSimpleCodeEmitter(PM, OptLevel, *MCE);
  if (PrintEmittedAsm)
    addAssemblyEmitter(PM, OptLevel, true, ferrs());

  PM.add(createGCInfoDeleter());

  return false; // success!
}

/// addPassesToEmitFileFinish - If the passes to emit the specified file had to
/// be split up (e.g., to add an object writer pass), this method can be used to
/// finish up adding passes to emit the file, if necessary.
bool LLVMTargetMachine::addPassesToEmitFileFinish(PassManagerBase &PM,
                                                  JITCodeEmitter *JCE,
                                                  CodeGenOpt::Level OptLevel) {
  if (JCE)
    addSimpleCodeEmitter(PM, OptLevel, *JCE);
  if (PrintEmittedAsm)
    addAssemblyEmitter(PM, OptLevel, true, ferrs());

  PM.add(createGCInfoDeleter());

  return false; // success!
}

/// addPassesToEmitFileFinish - If the passes to emit the specified file had to
/// be split up (e.g., to add an object writer pass), this method can be used to
/// finish up adding passes to emit the file, if necessary.
bool LLVMTargetMachine::addPassesToEmitFileFinish(PassManagerBase &PM,
                                                  ObjectCodeEmitter *OCE,
                                                  CodeGenOpt::Level OptLevel) {
  if (OCE)
    addSimpleCodeEmitter(PM, OptLevel, *OCE);
  if (PrintEmittedAsm)
    addAssemblyEmitter(PM, OptLevel, true, ferrs());

  PM.add(createGCInfoDeleter());

  return false; // success!
}

/// addPassesToEmitMachineCode - Add passes to the specified pass manager to
/// get machine code emitted.  This uses a MachineCodeEmitter object to handle
/// actually outputting the machine code and resolving things like the address
/// of functions.  This method should returns true if machine code emission is
/// not supported.
///
bool LLVMTargetMachine::addPassesToEmitMachineCode(PassManagerBase &PM,
                                                   MachineCodeEmitter &MCE,
                                                   CodeGenOpt::Level OptLevel) {
  // Add common CodeGen passes.
  if (addCommonCodeGenPasses(PM, OptLevel))
    return true;

  if (addPreEmitPass(PM, OptLevel) && PrintMachineCode)
    PM.add(createMachineFunctionPrinterPass(errs()));

  addCodeEmitter(PM, OptLevel, MCE);
  if (PrintEmittedAsm)
    addAssemblyEmitter(PM, OptLevel, true, ferrs());

  PM.add(createGCInfoDeleter());

  return false; // success!
}

/// addPassesToEmitMachineCode - Add passes to the specified pass manager to
/// get machine code emitted.  This uses a MachineCodeEmitter object to handle
/// actually outputting the machine code and resolving things like the address
/// of functions.  This method should returns true if machine code emission is
/// not supported.
///
bool LLVMTargetMachine::addPassesToEmitMachineCode(PassManagerBase &PM,
                                                   JITCodeEmitter &JCE,
                                                   CodeGenOpt::Level OptLevel) {
  // Add common CodeGen passes.
  if (addCommonCodeGenPasses(PM, OptLevel))
    return true;

  if (addPreEmitPass(PM, OptLevel) && PrintMachineCode)
    PM.add(createMachineFunctionPrinterPass(errs()));

  addCodeEmitter(PM, OptLevel, JCE);
  if (PrintEmittedAsm)
    addAssemblyEmitter(PM, OptLevel, true, ferrs());

  PM.add(createGCInfoDeleter());

  return false; // success!
}

static void printAndVerify(PassManagerBase &PM,
                           bool allowDoubleDefs = false) {
  if (PrintMachineCode)
    PM.add(createMachineFunctionPrinterPass(errs()));

  if (VerifyMachineCode)
    PM.add(createMachineVerifierPass(allowDoubleDefs));
}

/// addCommonCodeGenPasses - Add standard LLVM codegen passes used for both
/// emitting to assembly files or machine code output.
///
bool LLVMTargetMachine::addCommonCodeGenPasses(PassManagerBase &PM,
                                               CodeGenOpt::Level OptLevel) {
  // Standard LLVM-Level Passes.

  // Run loop strength reduction before anything else.
  if (OptLevel != CodeGenOpt::None) {
    PM.add(createLoopStrengthReducePass(getTargetLowering()));
    if (PrintLSR)
      PM.add(createPrintFunctionPass("\n\n*** Code after LSR ***\n", &errs()));
  }

  // Turn exception handling constructs into something the code generators can
  // handle.
  switch (getMCAsmInfo()->getExceptionHandlingType())
  {
  case ExceptionHandling::SjLj:
    // SjLj piggy-backs on dwarf for this bit. The cleanups done apply to both
    PM.add(createDwarfEHPass(getTargetLowering(), OptLevel==CodeGenOpt::None));
    PM.add(createSjLjEHPass(getTargetLowering()));
    break;
  case ExceptionHandling::Dwarf:
    PM.add(createDwarfEHPass(getTargetLowering(), OptLevel==CodeGenOpt::None));
    break;
  case ExceptionHandling::None:
    PM.add(createLowerInvokePass(getTargetLowering()));
    break;
  }

  PM.add(createGCLoweringPass());

  // Make sure that no unreachable blocks are instruction selected.
  PM.add(createUnreachableBlockEliminationPass());

  if (OptLevel != CodeGenOpt::None) {
    if (HoistConstants)
      PM.add(createCodeGenLICMPass());
    PM.add(createCodeGenPreparePass(getTargetLowering()));
  }

  PM.add(createStackProtectorPass(getTargetLowering()));

  if (PrintISelInput)
    PM.add(createPrintFunctionPass("\n\n"
                                   "*** Final LLVM Code input to ISel ***\n",
                                   &errs()));

  // Standard Lower-Level Passes.

  // Set up a MachineFunction for the rest of CodeGen to work on.
  PM.add(new MachineFunctionAnalysis(*this, OptLevel));

  // Enable FastISel with -fast, but allow that to be overridden.
  if (EnableFastISelOption == cl::BOU_TRUE ||
      (OptLevel == CodeGenOpt::None && EnableFastISelOption != cl::BOU_FALSE))
    EnableFastISel = true;

  // Ask the target for an isel.
  if (addInstSelector(PM, OptLevel))
    return true;

  // Print the instruction selected machine code...
  printAndVerify(PM, /* allowDoubleDefs= */ true);

  if (OptLevel != CodeGenOpt::None) {
    PM.add(createMachineLICMPass());
    PM.add(createMachineSinkingPass());
    printAndVerify(PM, /* allowDoubleDefs= */ true);
  }

  // Run pre-ra passes.
  if (addPreRegAlloc(PM, OptLevel))
    printAndVerify(PM, /* allowDoubleDefs= */ true);

  // Perform register allocation.
  PM.add(createRegisterAllocator());

  // Perform stack slot coloring.
  if (OptLevel != CodeGenOpt::None)
    // FIXME: Re-enable coloring with register when it's capable of adding
    // kill markers.
    PM.add(createStackSlotColoringPass(false));

  printAndVerify(PM);           // Print the register-allocated code

  // Run post-ra passes.
  if (addPostRegAlloc(PM, OptLevel))
    printAndVerify(PM);

  PM.add(createLowerSubregsPass());
  printAndVerify(PM);

  // Insert prolog/epilog code.  Eliminate abstract frame index references...
  PM.add(createPrologEpilogCodeInserter());
  printAndVerify(PM);

  // Run pre-sched2 passes.
  if (addPreSched2(PM, OptLevel))
    printAndVerify(PM);

  // Second pass scheduler.
  if (OptLevel != CodeGenOpt::None) {
    PM.add(createPostRAScheduler());
    printAndVerify(PM);
  }

  // Branch folding must be run after regalloc and prolog/epilog insertion.
  if (OptLevel != CodeGenOpt::None) {
    PM.add(createBranchFoldingPass(getEnableTailMergeDefault()));
    printAndVerify(PM);
  }

  PM.add(createGCMachineCodeAnalysisPass());
  printAndVerify(PM);

  if (PrintGCInfo)
    PM.add(createGCInfoPrinter(errs()));

  return false;
}
