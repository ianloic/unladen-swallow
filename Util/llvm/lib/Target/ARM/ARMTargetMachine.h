//===-- ARMTargetMachine.h - Define TargetMachine for ARM -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the ARM specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#ifndef ARMTARGETMACHINE_H
#define ARMTARGETMACHINE_H

#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetFrameInfo.h"
#include "ARMInstrInfo.h"
#include "ARMFrameInfo.h"
#include "ARMJITInfo.h"
#include "ARMSubtarget.h"
#include "ARMISelLowering.h"
#include "Thumb1InstrInfo.h"
#include "Thumb2InstrInfo.h"

namespace llvm {

class Module;

class ARMBaseTargetMachine : public LLVMTargetMachine {
protected:
  ARMSubtarget        Subtarget;

private:
  ARMFrameInfo        FrameInfo;
  ARMJITInfo          JITInfo;
  InstrItineraryData  InstrItins;
  Reloc::Model        DefRelocModel;    // Reloc model before it's overridden.

protected:
  // To avoid having target depend on the asmprinter stuff libraries, asmprinter
  // set this functions to ctor pointer at startup time if they are linked in.
  typedef FunctionPass *(*AsmPrinterCtorFn)(raw_ostream &o,
                                            ARMBaseTargetMachine &tm,
                                            bool verbose);
  static AsmPrinterCtorFn AsmPrinterCtor;

public:
  ARMBaseTargetMachine(const Module &M, const std::string &FS, bool isThumb);

  virtual const ARMFrameInfo     *getFrameInfo() const { return &FrameInfo; }
  virtual       ARMJITInfo       *getJITInfo()         { return &JITInfo; }
  virtual const ARMSubtarget  *getSubtargetImpl() const { return &Subtarget; }
  virtual const InstrItineraryData getInstrItineraryData() const {
    return InstrItins;
  }

  static void registerAsmPrinter(AsmPrinterCtorFn F) {
    AsmPrinterCtor = F;
  }

  static unsigned getModuleMatchQuality(const Module &M);
  static unsigned getJITMatchQuality();

  virtual const TargetAsmInfo *createTargetAsmInfo() const;

  // Pass Pipeline Configuration
  virtual bool addInstSelector(PassManagerBase &PM, CodeGenOpt::Level OptLevel);
  virtual bool addPreRegAlloc(PassManagerBase &PM, CodeGenOpt::Level OptLevel);
  virtual bool addPreEmitPass(PassManagerBase &PM, CodeGenOpt::Level OptLevel);
  virtual bool addAssemblyEmitter(PassManagerBase &PM,
                                  CodeGenOpt::Level OptLevel,
                                  bool Verbose, raw_ostream &Out);
  virtual bool addCodeEmitter(PassManagerBase &PM, CodeGenOpt::Level OptLevel,
                              bool DumpAsm, MachineCodeEmitter &MCE);
  virtual bool addCodeEmitter(PassManagerBase &PM, CodeGenOpt::Level OptLevel,
                              bool DumpAsm, JITCodeEmitter &MCE);
  virtual bool addCodeEmitter(PassManagerBase &PM, CodeGenOpt::Level OptLevel,
                              bool DumpAsm, ObjectCodeEmitter &OCE);
  virtual bool addSimpleCodeEmitter(PassManagerBase &PM,
                                    CodeGenOpt::Level OptLevel,
                                    bool DumpAsm,
                                    MachineCodeEmitter &MCE);
  virtual bool addSimpleCodeEmitter(PassManagerBase &PM,
                                    CodeGenOpt::Level OptLevel,
                                    bool DumpAsm,
                                    JITCodeEmitter &MCE);
  virtual bool addSimpleCodeEmitter(PassManagerBase &PM,
                                    CodeGenOpt::Level OptLevel,
                                    bool DumpAsm,
                                    ObjectCodeEmitter &OCE);
};

/// ARMTargetMachine - ARM target machine.
///
class ARMTargetMachine : public ARMBaseTargetMachine {
  ARMInstrInfo        InstrInfo;
  const TargetData    DataLayout;       // Calculates type size & alignment
  ARMTargetLowering   TLInfo;
public:
  ARMTargetMachine(const Module &M, const std::string &FS);

  virtual const ARMRegisterInfo  *getRegisterInfo() const {
    return &InstrInfo.getRegisterInfo();
  }

  virtual       ARMTargetLowering *getTargetLowering() const {
    return const_cast<ARMTargetLowering*>(&TLInfo);
  }

  virtual const ARMInstrInfo     *getInstrInfo() const { return &InstrInfo; }
  virtual const TargetData       *getTargetData() const { return &DataLayout; }

  static unsigned getJITMatchQuality();
  static unsigned getModuleMatchQuality(const Module &M);
};

/// ThumbTargetMachine - Thumb target machine.
/// Due to the way architectures are handled, this represents both
///   Thumb-1 and Thumb-2.
///
class ThumbTargetMachine : public ARMBaseTargetMachine {
  ARMBaseInstrInfo    *InstrInfo;   // either Thumb1InstrInfo or Thumb2InstrInfo
  const TargetData    DataLayout;   // Calculates type size & alignment
  ARMTargetLowering   TLInfo;
public:
  ThumbTargetMachine(const Module &M, const std::string &FS);

  /// returns either Thumb1RegisterInfo of Thumb2RegisterInfo
  virtual const ARMBaseRegisterInfo *getRegisterInfo() const {
    return &InstrInfo->getRegisterInfo();
  }

  virtual ARMTargetLowering *getTargetLowering() const {
    return const_cast<ARMTargetLowering*>(&TLInfo);
  }

  /// returns either Thumb1InstrInfo or Thumb2InstrInfo
  virtual const ARMBaseInstrInfo *getInstrInfo() const { return InstrInfo; }
  virtual const TargetData       *getTargetData() const { return &DataLayout; }

  static unsigned getJITMatchQuality();
  static unsigned getModuleMatchQuality(const Module &M);
};

} // end namespace llvm

#endif
