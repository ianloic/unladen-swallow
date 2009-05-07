//===-- MSP430TargetMachine.cpp - Define TargetMachine for MSP430 ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Top-level implementation for the MSP430 target.
//
//===----------------------------------------------------------------------===//

#include "MSP430.h"
#include "MSP430TargetAsmInfo.h"
#include "MSP430TargetMachine.h"
#include "llvm/Module.h"
#include "llvm/PassManager.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Target/TargetAsmInfo.h"
#include "llvm/Target/TargetMachineRegistry.h"

using namespace llvm;

/// MSP430TargetMachineModule - Note that this is used on hosts that
/// cannot link in a library unless there are references into the
/// library.  In particular, it seems that it is not possible to get
/// things to work on Win32 without this.  Though it is unused, do not
/// remove it.
extern "C" int MSP430TargetMachineModule;
int MSP430TargetMachineModule = 0;


// Register the targets
static RegisterTarget<MSP430TargetMachine>
X("msp430", "MSP430 [experimental]");

MSP430TargetMachine::MSP430TargetMachine(const Module &M,
                                         const std::string &FS) :
  Subtarget(*this, M, FS),
  // FIXME: Check TargetData string.
  DataLayout("e-p:16:8:8-i8:8:8-i16:8:8-i32:8:8"),
  InstrInfo(*this), TLInfo(*this),
  FrameInfo(TargetFrameInfo::StackGrowsDown, 2, -2) { }

const TargetAsmInfo *MSP430TargetMachine::createTargetAsmInfo() const {
  return new MSP430TargetAsmInfo(*this);
}

bool MSP430TargetMachine::addInstSelector(PassManagerBase &PM,
                                          CodeGenOpt::Level OptLevel) {
  // Install an instruction selector.
  PM.add(createMSP430ISelDag(*this, OptLevel));
  return false;
}

bool MSP430TargetMachine::addAssemblyEmitter(PassManagerBase &PM,
                                             CodeGenOpt::Level OptLevel,
                                             bool Verbose,
                                             raw_ostream &Out) {
  // Output assembly language.
  PM.add(createMSP430CodePrinterPass(Out, *this, OptLevel, Verbose));
  return false;
}

unsigned MSP430TargetMachine::getModuleMatchQuality(const Module &M) {
  std::string TT = M.getTargetTriple();

  // We strongly match msp430
  if (TT.size() >= 6 && TT[0] == 'm' && TT[1] == 's' && TT[2] == 'p' &&
      TT[3] == '4' &&  TT[4] == '3' && TT[5] == '0')
    return 20;

  return 0;
}

