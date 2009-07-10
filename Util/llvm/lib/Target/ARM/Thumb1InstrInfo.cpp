//===- Thumb1InstrInfo.cpp - Thumb-1 Instruction Information --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Thumb-1 implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "ARMInstrInfo.h"
#include "ARM.h"
#include "ARMGenInstrInfo.inc"
#include "ARMMachineFunctionInfo.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/ADT/SmallVector.h"
#include "Thumb1InstrInfo.h"

using namespace llvm;

Thumb1InstrInfo::Thumb1InstrInfo(const ARMSubtarget &STI)
  : ARMBaseInstrInfo(STI), RI(*this, STI) {
}

unsigned Thumb1InstrInfo::
getUnindexedOpcode(unsigned Opc) const {
  return 0;
}

unsigned Thumb1InstrInfo::
getOpcode(ARMII::Op Op) const {
  switch (Op) {
  case ARMII::ADDri: return ARM::tADDi8;
  case ARMII::ADDrs: return 0;
  case ARMII::ADDrr: return ARM::tADDrr;
  case ARMII::B: return ARM::tB;
  case ARMII::Bcc: return ARM::tBcc;
  case ARMII::BR_JTr: return ARM::tBR_JTr;
  case ARMII::BR_JTm: return 0;
  case ARMII::BR_JTadd: return 0;
  case ARMII::BX_RET: return ARM::tBX_RET;
  case ARMII::FCPYS: return 0;
  case ARMII::FCPYD: return 0;
  case ARMII::FLDD: return 0;
  case ARMII::FLDS: return 0;
  case ARMII::FSTD: return 0;
  case ARMII::FSTS: return 0;
  case ARMII::LDR: return ARM::tLDR;
  case ARMII::MOVr: return ARM::tMOVr;
  case ARMII::STR: return ARM::tSTR;
  case ARMII::SUBri: return ARM::tSUBi8;
  case ARMII::SUBrs: return 0;
  case ARMII::SUBrr: return ARM::tSUBrr;
  case ARMII::VMOVD: return 0;
  case ARMII::VMOVQ: return 0;
  default:
    break;
  }

  return 0;
}

bool
Thumb1InstrInfo::BlockHasNoFallThrough(const MachineBasicBlock &MBB) const {
  if (MBB.empty()) return false;

  switch (MBB.back().getOpcode()) {
  case ARM::tBX_RET:
  case ARM::tBX_RET_vararg:
  case ARM::tPOP_RET:
  case ARM::tB:
  case ARM::tBR_JTr:
    return true;
  default:
    break;
  }

  return false;
}

bool Thumb1InstrInfo::isMoveInstr(const MachineInstr &MI,
                                  unsigned &SrcReg, unsigned &DstReg,
                                  unsigned& SrcSubIdx, unsigned& DstSubIdx) const {
  SrcSubIdx = DstSubIdx = 0; // No sub-registers.

  unsigned oc = MI.getOpcode();
  switch (oc) {
  default:
    return false;
  case ARM::tMOVr:
  case ARM::tMOVhir2lor:
  case ARM::tMOVlor2hir:
  case ARM::tMOVhir2hir:
    assert(MI.getDesc().getNumOperands() >= 2 &&
           MI.getOperand(0).isReg() &&
           MI.getOperand(1).isReg() &&
           "Invalid Thumb MOV instruction");
    SrcReg = MI.getOperand(1).getReg();
    DstReg = MI.getOperand(0).getReg();
    return true;
  }
}

unsigned Thumb1InstrInfo::isLoadFromStackSlot(const MachineInstr *MI,
                                              int &FrameIndex) const {
  switch (MI->getOpcode()) {
  default: break;
  case ARM::tRestore:
    if (MI->getOperand(1).isFI() &&
        MI->getOperand(2).isImm() &&
        MI->getOperand(2).getImm() == 0) {
      FrameIndex = MI->getOperand(1).getIndex();
      return MI->getOperand(0).getReg();
    }
    break;
  }
  return 0;
}

unsigned Thumb1InstrInfo::isStoreToStackSlot(const MachineInstr *MI,
                                             int &FrameIndex) const {
  switch (MI->getOpcode()) {
  default: break;
  case ARM::tSpill:
    if (MI->getOperand(1).isFI() &&
        MI->getOperand(2).isImm() &&
        MI->getOperand(2).getImm() == 0) {
      FrameIndex = MI->getOperand(1).getIndex();
      return MI->getOperand(0).getReg();
    }
    break;
  }
  return 0;
}

bool Thumb1InstrInfo::copyRegToReg(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator I,
                                   unsigned DestReg, unsigned SrcReg,
                                   const TargetRegisterClass *DestRC,
                                   const TargetRegisterClass *SrcRC) const {
  DebugLoc DL = DebugLoc::getUnknownLoc();
  if (I != MBB.end()) DL = I->getDebugLoc();

  if (DestRC == ARM::GPRRegisterClass) {
    if (SrcRC == ARM::GPRRegisterClass) {
      BuildMI(MBB, I, DL, get(ARM::tMOVhir2hir), DestReg).addReg(SrcReg);
      return true;
    } else if (SrcRC == ARM::tGPRRegisterClass) {
      BuildMI(MBB, I, DL, get(ARM::tMOVlor2hir), DestReg).addReg(SrcReg);
      return true;
    }
  } else if (DestRC == ARM::tGPRRegisterClass) {
    if (SrcRC == ARM::GPRRegisterClass) {
      BuildMI(MBB, I, DL, get(ARM::tMOVhir2lor), DestReg).addReg(SrcReg);
      return true;
    } else if (SrcRC == ARM::tGPRRegisterClass) {
      BuildMI(MBB, I, DL, get(ARM::tMOVr), DestReg).addReg(SrcReg);
      return true;
    }
  }

  return false;
}

bool Thumb1InstrInfo::
canFoldMemoryOperand(const MachineInstr *MI,
                     const SmallVectorImpl<unsigned> &Ops) const {
  if (Ops.size() != 1) return false;

  unsigned OpNum = Ops[0];
  unsigned Opc = MI->getOpcode();
  switch (Opc) {
  default: break;
  case ARM::tMOVr:
  case ARM::tMOVlor2hir:
  case ARM::tMOVhir2lor:
  case ARM::tMOVhir2hir: {
    if (OpNum == 0) { // move -> store
      unsigned SrcReg = MI->getOperand(1).getReg();
      if (RI.isPhysicalRegister(SrcReg) && !isARMLowRegister(SrcReg))
        // tSpill cannot take a high register operand.
        return false;
    } else {          // move -> load
      unsigned DstReg = MI->getOperand(0).getReg();
      if (RI.isPhysicalRegister(DstReg) && !isARMLowRegister(DstReg))
        // tRestore cannot target a high register operand.
        return false;
    }
    return true;
  }
  }

  return false;
}

void Thumb1InstrInfo::
storeRegToStackSlot(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                    unsigned SrcReg, bool isKill, int FI,
                    const TargetRegisterClass *RC) const {
  DebugLoc DL = DebugLoc::getUnknownLoc();
  if (I != MBB.end()) DL = I->getDebugLoc();

  assert(RC == ARM::tGPRRegisterClass && "Unknown regclass!");

  if (RC == ARM::tGPRRegisterClass) {
    BuildMI(MBB, I, DL, get(ARM::tSpill))
      .addReg(SrcReg, getKillRegState(isKill))
      .addFrameIndex(FI).addImm(0);
  }
}

void Thumb1InstrInfo::storeRegToAddr(MachineFunction &MF, unsigned SrcReg,
                                     bool isKill,
                                     SmallVectorImpl<MachineOperand> &Addr,
                                     const TargetRegisterClass *RC,
                                     SmallVectorImpl<MachineInstr*> &NewMIs) const{
  DebugLoc DL = DebugLoc::getUnknownLoc();
  unsigned Opc = 0;

  assert(RC == ARM::GPRRegisterClass && "Unknown regclass!");
  if (RC == ARM::GPRRegisterClass) {
    Opc = Addr[0].isFI() ? ARM::tSpill : ARM::tSTR;
  }

  MachineInstrBuilder MIB =
    BuildMI(MF, DL,  get(Opc)).addReg(SrcReg, getKillRegState(isKill));
  for (unsigned i = 0, e = Addr.size(); i != e; ++i)
    MIB.addOperand(Addr[i]);
  NewMIs.push_back(MIB);
  return;
}

void Thumb1InstrInfo::
loadRegFromStackSlot(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                     unsigned DestReg, int FI,
                     const TargetRegisterClass *RC) const {
  DebugLoc DL = DebugLoc::getUnknownLoc();
  if (I != MBB.end()) DL = I->getDebugLoc();

  assert(RC == ARM::tGPRRegisterClass && "Unknown regclass!");

  if (RC == ARM::tGPRRegisterClass) {
    BuildMI(MBB, I, DL, get(ARM::tRestore), DestReg)
      .addFrameIndex(FI).addImm(0);
  }
}

void Thumb1InstrInfo::
loadRegFromAddr(MachineFunction &MF, unsigned DestReg,
                SmallVectorImpl<MachineOperand> &Addr,
                const TargetRegisterClass *RC,
                SmallVectorImpl<MachineInstr*> &NewMIs) const {
  DebugLoc DL = DebugLoc::getUnknownLoc();
  unsigned Opc = 0;

  if (RC == ARM::GPRRegisterClass) {
    Opc = Addr[0].isFI() ? ARM::tRestore : ARM::tLDR;
  }

  MachineInstrBuilder MIB = BuildMI(MF, DL, get(Opc), DestReg);
  for (unsigned i = 0, e = Addr.size(); i != e; ++i)
    MIB.addOperand(Addr[i]);
  NewMIs.push_back(MIB);
  return;
}

bool Thumb1InstrInfo::
spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator MI,
                          const std::vector<CalleeSavedInfo> &CSI) const {
  if (CSI.empty())
    return false;

  DebugLoc DL = DebugLoc::getUnknownLoc();
  if (MI != MBB.end()) DL = MI->getDebugLoc();

  MachineInstrBuilder MIB = BuildMI(MBB, MI, DL, get(ARM::tPUSH));
  for (unsigned i = CSI.size(); i != 0; --i) {
    unsigned Reg = CSI[i-1].getReg();
    // Add the callee-saved register as live-in. It's killed at the spill.
    MBB.addLiveIn(Reg);
    MIB.addReg(Reg, RegState::Kill);
  }
  return true;
}

bool Thumb1InstrInfo::
restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MI,
                            const std::vector<CalleeSavedInfo> &CSI) const {
  MachineFunction &MF = *MBB.getParent();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  if (CSI.empty())
    return false;

  bool isVarArg = AFI->getVarArgsRegSaveSize() > 0;
  MachineInstr *PopMI = MF.CreateMachineInstr(get(ARM::tPOP),MI->getDebugLoc());
  for (unsigned i = CSI.size(); i != 0; --i) {
    unsigned Reg = CSI[i-1].getReg();
    if (Reg == ARM::LR) {
      // Special epilogue for vararg functions. See emitEpilogue
      if (isVarArg)
        continue;
      Reg = ARM::PC;
      PopMI->setDesc(get(ARM::tPOP_RET));
      MI = MBB.erase(MI);
    }
    PopMI->addOperand(MachineOperand::CreateReg(Reg, true));
  }

  // It's illegal to emit pop instruction without operands.
  if (PopMI->getNumOperands() > 0)
    MBB.insert(MI, PopMI);

  return true;
}

MachineInstr *Thumb1InstrInfo::
foldMemoryOperandImpl(MachineFunction &MF, MachineInstr *MI,
                      const SmallVectorImpl<unsigned> &Ops, int FI) const {
  if (Ops.size() != 1) return NULL;

  unsigned OpNum = Ops[0];
  unsigned Opc = MI->getOpcode();
  MachineInstr *NewMI = NULL;
  switch (Opc) {
  default: break;
  case ARM::tMOVr:
  case ARM::tMOVlor2hir:
  case ARM::tMOVhir2lor:
  case ARM::tMOVhir2hir: {
    if (OpNum == 0) { // move -> store
      unsigned SrcReg = MI->getOperand(1).getReg();
      bool isKill = MI->getOperand(1).isKill();
      if (RI.isPhysicalRegister(SrcReg) && !isARMLowRegister(SrcReg))
        // tSpill cannot take a high register operand.
        break;
      NewMI = BuildMI(MF, MI->getDebugLoc(), get(ARM::tSpill))
        .addReg(SrcReg, getKillRegState(isKill))
        .addFrameIndex(FI).addImm(0);
    } else {          // move -> load
      unsigned DstReg = MI->getOperand(0).getReg();
      if (RI.isPhysicalRegister(DstReg) && !isARMLowRegister(DstReg))
        // tRestore cannot target a high register operand.
        break;
      bool isDead = MI->getOperand(0).isDead();
      NewMI = BuildMI(MF, MI->getDebugLoc(), get(ARM::tRestore))
        .addReg(DstReg, RegState::Define | getDeadRegState(isDead))
        .addFrameIndex(FI).addImm(0);
    }
    break;
  }
  }

  return NewMI;
}
