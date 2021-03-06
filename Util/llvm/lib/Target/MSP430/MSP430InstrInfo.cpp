//===- MSP430InstrInfo.cpp - MSP430 Instruction Information ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the MSP430 implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "MSP430.h"
#include "MSP430InstrInfo.h"
#include "MSP430MachineFunctionInfo.h"
#include "MSP430TargetMachine.h"
#include "MSP430GenInstrInfo.inc"
#include "llvm/Function.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

MSP430InstrInfo::MSP430InstrInfo(MSP430TargetMachine &tm)
  : TargetInstrInfoImpl(MSP430Insts, array_lengthof(MSP430Insts)),
    RI(tm, *this), TM(tm) {}

void MSP430InstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                          MachineBasicBlock::iterator MI,
                                    unsigned SrcReg, bool isKill, int FrameIdx,
                                    const TargetRegisterClass *RC) const {
  DebugLoc DL = DebugLoc::getUnknownLoc();
  if (MI != MBB.end()) DL = MI->getDebugLoc();
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = *MF.getFrameInfo();

  MachineMemOperand *MMO =
    MF.getMachineMemOperand(PseudoSourceValue::getFixedStack(FrameIdx),
                            MachineMemOperand::MOStore, 0,
                            MFI.getObjectSize(FrameIdx),
                            MFI.getObjectAlignment(FrameIdx));

  if (RC == &MSP430::GR16RegClass)
    BuildMI(MBB, MI, DL, get(MSP430::MOV16mr))
      .addFrameIndex(FrameIdx).addImm(0)
      .addReg(SrcReg, getKillRegState(isKill)).addMemOperand(MMO);
  else if (RC == &MSP430::GR8RegClass)
    BuildMI(MBB, MI, DL, get(MSP430::MOV8mr))
      .addFrameIndex(FrameIdx).addImm(0)
      .addReg(SrcReg, getKillRegState(isKill)).addMemOperand(MMO);
  else
    llvm_unreachable("Cannot store this register to stack slot!");
}

void MSP430InstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                           MachineBasicBlock::iterator MI,
                                           unsigned DestReg, int FrameIdx,
                                           const TargetRegisterClass *RC) const{
  DebugLoc DL = DebugLoc::getUnknownLoc();
  if (MI != MBB.end()) DL = MI->getDebugLoc();
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = *MF.getFrameInfo();

  MachineMemOperand *MMO =
    MF.getMachineMemOperand(PseudoSourceValue::getFixedStack(FrameIdx),
                            MachineMemOperand::MOLoad, 0,
                            MFI.getObjectSize(FrameIdx),
                            MFI.getObjectAlignment(FrameIdx));

  if (RC == &MSP430::GR16RegClass)
    BuildMI(MBB, MI, DL, get(MSP430::MOV16rm))
      .addReg(DestReg).addFrameIndex(FrameIdx).addImm(0).addMemOperand(MMO);
  else if (RC == &MSP430::GR8RegClass)
    BuildMI(MBB, MI, DL, get(MSP430::MOV8rm))
      .addReg(DestReg).addFrameIndex(FrameIdx).addImm(0).addMemOperand(MMO);
  else
    llvm_unreachable("Cannot store this register to stack slot!");
}

bool MSP430InstrInfo::copyRegToReg(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator I,
                                   unsigned DestReg, unsigned SrcReg,
                                   const TargetRegisterClass *DestRC,
                                   const TargetRegisterClass *SrcRC) const {
  DebugLoc DL = DebugLoc::getUnknownLoc();
  if (I != MBB.end()) DL = I->getDebugLoc();

  if (DestRC == SrcRC) {
    unsigned Opc;
    if (DestRC == &MSP430::GR16RegClass) {
      Opc = MSP430::MOV16rr;
    } else if (DestRC == &MSP430::GR8RegClass) {
      Opc = MSP430::MOV8rr;
    } else {
      return false;
    }

    BuildMI(MBB, I, DL, get(Opc), DestReg).addReg(SrcReg);
    return true;
  }

  return false;
}

bool
MSP430InstrInfo::isMoveInstr(const MachineInstr& MI,
                             unsigned &SrcReg, unsigned &DstReg,
                             unsigned &SrcSubIdx, unsigned &DstSubIdx) const {
  SrcSubIdx = DstSubIdx = 0; // No sub-registers yet.

  switch (MI.getOpcode()) {
  default:
    return false;
  case MSP430::MOV8rr:
  case MSP430::MOV16rr:
   assert(MI.getNumOperands() >= 2 &&
           MI.getOperand(0).isReg() &&
           MI.getOperand(1).isReg() &&
           "invalid register-register move instruction");
    SrcReg = MI.getOperand(1).getReg();
    DstReg = MI.getOperand(0).getReg();
    return true;
  }
}

bool
MSP430InstrInfo::spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                           MachineBasicBlock::iterator MI,
                                const std::vector<CalleeSavedInfo> &CSI) const {
  if (CSI.empty())
    return false;

  DebugLoc DL = DebugLoc::getUnknownLoc();
  if (MI != MBB.end()) DL = MI->getDebugLoc();

  MachineFunction &MF = *MBB.getParent();
  MSP430MachineFunctionInfo *MFI = MF.getInfo<MSP430MachineFunctionInfo>();
  MFI->setCalleeSavedFrameSize(CSI.size() * 2);

  for (unsigned i = CSI.size(); i != 0; --i) {
    unsigned Reg = CSI[i-1].getReg();
    // Add the callee-saved register as live-in. It's killed at the spill.
    MBB.addLiveIn(Reg);
    BuildMI(MBB, MI, DL, get(MSP430::PUSH16r))
      .addReg(Reg, RegState::Kill);
  }
  return true;
}

bool
MSP430InstrInfo::restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                                             MachineBasicBlock::iterator MI,
                                const std::vector<CalleeSavedInfo> &CSI) const {
  if (CSI.empty())
    return false;

  DebugLoc DL = DebugLoc::getUnknownLoc();
  if (MI != MBB.end()) DL = MI->getDebugLoc();

  for (unsigned i = 0, e = CSI.size(); i != e; ++i)
    BuildMI(MBB, MI, DL, get(MSP430::POP16r), CSI[i].getReg());

  return true;
}

unsigned MSP430InstrInfo::RemoveBranch(MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator I = MBB.end();
  unsigned Count = 0;

  while (I != MBB.begin()) {
    --I;
    if (I->getOpcode() != MSP430::JMP &&
        I->getOpcode() != MSP430::JCC)
      break;
    // Remove the branch.
    I->eraseFromParent();
    I = MBB.end();
    ++Count;
  }

  return Count;
}

bool MSP430InstrInfo::
ReverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() == 1 && "Invalid Xbranch condition!");

  MSP430CC::CondCodes CC = static_cast<MSP430CC::CondCodes>(Cond[0].getImm());

  switch (CC) {
  default:
    assert(0 && "Invalid branch condition!");
    break;
  case MSP430CC::COND_E:
    CC = MSP430CC::COND_NE;
    break;
  case MSP430CC::COND_NE:
    CC = MSP430CC::COND_E;
    break;
  case MSP430CC::COND_L:
    CC = MSP430CC::COND_GE;
    break;
  case MSP430CC::COND_GE:
    CC = MSP430CC::COND_L;
    break;
  case MSP430CC::COND_HS:
    CC = MSP430CC::COND_LO;
    break;
  case MSP430CC::COND_LO:
    CC = MSP430CC::COND_HS;
    break;
  }

  Cond[0].setImm(CC);
  return false;
}

bool MSP430InstrInfo::BlockHasNoFallThrough(const MachineBasicBlock &MBB)const{
  if (MBB.empty()) return false;

  switch (MBB.back().getOpcode()) {
  case MSP430::RET:   // Return.
  case MSP430::JMP:   // Uncond branch.
    return true;
  default: return false;
  }
}

bool MSP430InstrInfo::isUnpredicatedTerminator(const MachineInstr *MI) const {
  const TargetInstrDesc &TID = MI->getDesc();
  if (!TID.isTerminator()) return false;

  // Conditional branch is a special case.
  if (TID.isBranch() && !TID.isBarrier())
    return true;
  if (!TID.isPredicable())
    return true;
  return !isPredicated(MI);
}

bool MSP430InstrInfo::AnalyzeBranch(MachineBasicBlock &MBB,
                                    MachineBasicBlock *&TBB,
                                    MachineBasicBlock *&FBB,
                                    SmallVectorImpl<MachineOperand> &Cond,
                                    bool AllowModify) const {
  // Start from the bottom of the block and work up, examining the
  // terminator instructions.
  MachineBasicBlock::iterator I = MBB.end();
  while (I != MBB.begin()) {
    --I;
    // Working from the bottom, when we see a non-terminator
    // instruction, we're done.
    if (!isUnpredicatedTerminator(I))
      break;

    // A terminator that isn't a branch can't easily be handled
    // by this analysis.
    if (!I->getDesc().isBranch())
      return true;

    // Handle unconditional branches.
    if (I->getOpcode() == MSP430::JMP) {
      if (!AllowModify) {
        TBB = I->getOperand(0).getMBB();
        continue;
      }

      // If the block has any instructions after a JMP, delete them.
      while (next(I) != MBB.end())
        next(I)->eraseFromParent();
      Cond.clear();
      FBB = 0;

      // Delete the JMP if it's equivalent to a fall-through.
      if (MBB.isLayoutSuccessor(I->getOperand(0).getMBB())) {
        TBB = 0;
        I->eraseFromParent();
        I = MBB.end();
        continue;
      }

      // TBB is used to indicate the unconditinal destination.
      TBB = I->getOperand(0).getMBB();
      continue;
    }

    // Handle conditional branches.
    assert(I->getOpcode() == MSP430::JCC && "Invalid conditional branch");
    MSP430CC::CondCodes BranchCode =
      static_cast<MSP430CC::CondCodes>(I->getOperand(1).getImm());
    if (BranchCode == MSP430CC::COND_INVALID)
      return true;  // Can't handle weird stuff.

    // Working from the bottom, handle the first conditional branch.
    if (Cond.empty()) {
      FBB = TBB;
      TBB = I->getOperand(0).getMBB();
      Cond.push_back(MachineOperand::CreateImm(BranchCode));
      continue;
    }

    // Handle subsequent conditional branches. Only handle the case where all
    // conditional branches branch to the same destination.
    assert(Cond.size() == 1);
    assert(TBB);

    // Only handle the case where all conditional branches branch to
    // the same destination.
    if (TBB != I->getOperand(0).getMBB())
      return true;

    MSP430CC::CondCodes OldBranchCode = (MSP430CC::CondCodes)Cond[0].getImm();
    // If the conditions are the same, we can leave them alone.
    if (OldBranchCode == BranchCode)
      continue;

    return true;
  }

  return false;
}

unsigned
MSP430InstrInfo::InsertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                              MachineBasicBlock *FBB,
                            const SmallVectorImpl<MachineOperand> &Cond) const {
  // FIXME this should probably have a DebugLoc operand
  DebugLoc dl = DebugLoc::getUnknownLoc();

  // Shouldn't be a fall through.
  assert(TBB && "InsertBranch must not be told to insert a fallthrough");
  assert((Cond.size() == 1 || Cond.size() == 0) &&
         "MSP430 branch conditions have one component!");

  if (Cond.empty()) {
    // Unconditional branch?
    assert(!FBB && "Unconditional branch with multiple successors!");
    BuildMI(&MBB, dl, get(MSP430::JMP)).addMBB(TBB);
    return 1;
  }

  // Conditional branch.
  unsigned Count = 0;
  BuildMI(&MBB, dl, get(MSP430::JCC)).addMBB(TBB).addImm(Cond[0].getImm());
  ++Count;

  if (FBB) {
    // Two-way Conditional branch. Insert the second branch.
    BuildMI(&MBB, dl, get(MSP430::JMP)).addMBB(FBB);
    ++Count;
  }
  return Count;
}
