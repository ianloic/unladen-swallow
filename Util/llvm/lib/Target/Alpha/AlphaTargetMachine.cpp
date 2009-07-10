//===-- AlphaTargetMachine.cpp - Define TargetMachine for Alpha -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "Alpha.h"
#include "AlphaJITInfo.h"
#include "AlphaTargetAsmInfo.h"
#include "AlphaTargetMachine.h"
#include "llvm/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Target/TargetMachineRegistry.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

// Register the targets
static RegisterTarget<AlphaTargetMachine> X("alpha", "Alpha [experimental]");

// No assembler printer by default
AlphaTargetMachine::AsmPrinterCtorFn AlphaTargetMachine::AsmPrinterCtor = 0;

// Force static initialization.
extern "C" void LLVMInitializeAlphaTarget() { }

const TargetAsmInfo *AlphaTargetMachine::createTargetAsmInfo() const {
  return new AlphaTargetAsmInfo(*this);
}

unsigned AlphaTargetMachine::getModuleMatchQuality(const Module &M) {
  // We strongly match "alpha*".
  std::string TT = M.getTargetTriple();
  if (TT.size() >= 5 && TT[0] == 'a' && TT[1] == 'l' && TT[2] == 'p' &&
      TT[3] == 'h' && TT[4] == 'a')
    return 20;
  // If the target triple is something non-alpha, we don't match.
  if (!TT.empty()) return 0;

  if (M.getEndianness()  == Module::LittleEndian &&
      M.getPointerSize() == Module::Pointer64)
    return 10;                                   // Weak match
  else if (M.getEndianness() != Module::AnyEndianness ||
           M.getPointerSize() != Module::AnyPointerSize)
    return 0;                                    // Match for some other target

  return getJITMatchQuality()/2;
}

unsigned AlphaTargetMachine::getJITMatchQuality() {
#ifdef __alpha
  return 10;
#else
  return 0;
#endif
}

AlphaTargetMachine::AlphaTargetMachine(const Module &M, const std::string &FS)
  : DataLayout("e-f128:128:128"),
    FrameInfo(TargetFrameInfo::StackGrowsDown, 16, 0),
    JITInfo(*this),
    Subtarget(M, FS),
    TLInfo(*this) {
  setRelocationModel(Reloc::PIC_);
}


//===----------------------------------------------------------------------===//
// Pass Pipeline Configuration
//===----------------------------------------------------------------------===//

bool AlphaTargetMachine::addInstSelector(PassManagerBase &PM,
                                         CodeGenOpt::Level OptLevel) {
  PM.add(createAlphaISelDag(*this));
  return false;
}
bool AlphaTargetMachine::addPreEmitPass(PassManagerBase &PM,
                                        CodeGenOpt::Level OptLevel) {
  // Must run branch selection immediately preceding the asm printer
  PM.add(createAlphaBranchSelectionPass());
  return false;
}
bool AlphaTargetMachine::addAssemblyEmitter(PassManagerBase &PM,
                                            CodeGenOpt::Level OptLevel,
                                            bool Verbose,
                                            raw_ostream &Out) {
  PM.add(createAlphaLLRPPass(*this));
  // Output assembly language.
  assert(AsmPrinterCtor && "AsmPrinter was not linked in");
  if (AsmPrinterCtor)
    PM.add(AsmPrinterCtor(Out, *this, Verbose));
  return false;
}
bool AlphaTargetMachine::addCodeEmitter(PassManagerBase &PM,
                                        CodeGenOpt::Level OptLevel,
                                        bool DumpAsm, MachineCodeEmitter &MCE) {
  PM.add(createAlphaCodeEmitterPass(*this, MCE));
  if (DumpAsm) {
    assert(AsmPrinterCtor && "AsmPrinter was not linked in");
    if (AsmPrinterCtor)
      PM.add(AsmPrinterCtor(errs(), *this, true));
  }
  return false;
}
bool AlphaTargetMachine::addCodeEmitter(PassManagerBase &PM,
                                        CodeGenOpt::Level OptLevel,
                                        bool DumpAsm, JITCodeEmitter &JCE) {
  PM.add(createAlphaJITCodeEmitterPass(*this, JCE));
  if (DumpAsm) {
    assert(AsmPrinterCtor && "AsmPrinter was not linked in");
    if (AsmPrinterCtor)
      PM.add(AsmPrinterCtor(errs(), *this, true));
  }
  return false;
}
bool AlphaTargetMachine::addCodeEmitter(PassManagerBase &PM,
                                        CodeGenOpt::Level OptLevel,
                                        bool DumpAsm, ObjectCodeEmitter &OCE) {
  PM.add(createAlphaObjectCodeEmitterPass(*this, OCE));
  if (DumpAsm) {
    assert(AsmPrinterCtor && "AsmPrinter was not linked in");
    if (AsmPrinterCtor)
      PM.add(AsmPrinterCtor(errs(), *this, true));
  }
  return false;
}
bool AlphaTargetMachine::addSimpleCodeEmitter(PassManagerBase &PM,
                                              CodeGenOpt::Level OptLevel,
                                              bool DumpAsm,
                                              MachineCodeEmitter &MCE) {
  return addCodeEmitter(PM, OptLevel, DumpAsm, MCE);
}
bool AlphaTargetMachine::addSimpleCodeEmitter(PassManagerBase &PM,
                                              CodeGenOpt::Level OptLevel,
                                              bool DumpAsm,
                                              JITCodeEmitter &JCE) {
  return addCodeEmitter(PM, OptLevel, DumpAsm, JCE);
}
bool AlphaTargetMachine::addSimpleCodeEmitter(PassManagerBase &PM,
                                              CodeGenOpt::Level OptLevel,
                                              bool DumpAsm,
                                              ObjectCodeEmitter &OCE) {
  return addCodeEmitter(PM, OptLevel, DumpAsm, OCE);
}

