//===-- TargetInstrInfoImpl.cpp - Target Instruction Information ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the TargetInstrInfoImpl class, it just provides default
// implementations of various methods.
//
//===----------------------------------------------------------------------===//

#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

// commuteInstruction - The default implementation of this method just exchanges
// the two operands returned by findCommutedOpIndices.
MachineInstr *TargetInstrInfoImpl::commuteInstruction(MachineInstr *MI,
                                                      bool NewMI) const {
  const TargetInstrDesc &TID = MI->getDesc();
  bool HasDef = TID.getNumDefs();
  if (HasDef && !MI->getOperand(0).isReg())
    // No idea how to commute this instruction. Target should implement its own.
    return 0;
  unsigned Idx1, Idx2;
  if (!findCommutedOpIndices(MI, Idx1, Idx2)) {
    std::string msg;
    raw_string_ostream Msg(msg);
    Msg << "Don't know how to commute: " << *MI;
    llvm_report_error(Msg.str());
  }

  assert(MI->getOperand(Idx1).isReg() && MI->getOperand(Idx2).isReg() &&
         "This only knows how to commute register operands so far");
  unsigned Reg1 = MI->getOperand(Idx1).getReg();
  unsigned Reg2 = MI->getOperand(Idx2).getReg();
  bool Reg1IsKill = MI->getOperand(Idx1).isKill();
  bool Reg2IsKill = MI->getOperand(Idx2).isKill();
  bool ChangeReg0 = false;
  if (HasDef && MI->getOperand(0).getReg() == Reg1) {
    // Must be two address instruction!
    assert(MI->getDesc().getOperandConstraint(0, TOI::TIED_TO) &&
           "Expecting a two-address instruction!");
    Reg2IsKill = false;
    ChangeReg0 = true;
  }

  if (NewMI) {
    // Create a new instruction.
    unsigned Reg0 = HasDef
      ? (ChangeReg0 ? Reg2 : MI->getOperand(0).getReg()) : 0;
    bool Reg0IsDead = HasDef ? MI->getOperand(0).isDead() : false;
    MachineFunction &MF = *MI->getParent()->getParent();
    if (HasDef)
      return BuildMI(MF, MI->getDebugLoc(), MI->getDesc())
        .addReg(Reg0, RegState::Define | getDeadRegState(Reg0IsDead))
        .addReg(Reg2, getKillRegState(Reg2IsKill))
        .addReg(Reg1, getKillRegState(Reg2IsKill));
    else
      return BuildMI(MF, MI->getDebugLoc(), MI->getDesc())
        .addReg(Reg2, getKillRegState(Reg2IsKill))
        .addReg(Reg1, getKillRegState(Reg2IsKill));
  }

  if (ChangeReg0)
    MI->getOperand(0).setReg(Reg2);
  MI->getOperand(Idx2).setReg(Reg1);
  MI->getOperand(Idx1).setReg(Reg2);
  MI->getOperand(Idx2).setIsKill(Reg1IsKill);
  MI->getOperand(Idx1).setIsKill(Reg2IsKill);
  return MI;
}

/// findCommutedOpIndices - If specified MI is commutable, return the two
/// operand indices that would swap value. Return true if the instruction
/// is not in a form which this routine understands.
bool TargetInstrInfoImpl::findCommutedOpIndices(MachineInstr *MI,
                                                unsigned &SrcOpIdx1,
                                                unsigned &SrcOpIdx2) const {
  const TargetInstrDesc &TID = MI->getDesc();
  if (!TID.isCommutable())
    return false;
  // This assumes v0 = op v1, v2 and commuting would swap v1 and v2. If this
  // is not true, then the target must implement this.
  SrcOpIdx1 = TID.getNumDefs();
  SrcOpIdx2 = SrcOpIdx1 + 1;
  if (!MI->getOperand(SrcOpIdx1).isReg() ||
      !MI->getOperand(SrcOpIdx2).isReg())
    // No idea.
    return false;
  return true;
}


bool TargetInstrInfoImpl::PredicateInstruction(MachineInstr *MI,
                            const SmallVectorImpl<MachineOperand> &Pred) const {
  bool MadeChange = false;
  const TargetInstrDesc &TID = MI->getDesc();
  if (!TID.isPredicable())
    return false;
  
  for (unsigned j = 0, i = 0, e = MI->getNumOperands(); i != e; ++i) {
    if (TID.OpInfo[i].isPredicate()) {
      MachineOperand &MO = MI->getOperand(i);
      if (MO.isReg()) {
        MO.setReg(Pred[j].getReg());
        MadeChange = true;
      } else if (MO.isImm()) {
        MO.setImm(Pred[j].getImm());
        MadeChange = true;
      } else if (MO.isMBB()) {
        MO.setMBB(Pred[j].getMBB());
        MadeChange = true;
      }
      ++j;
    }
  }
  return MadeChange;
}

void TargetInstrInfoImpl::reMaterialize(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator I,
                                        unsigned DestReg,
                                        unsigned SubIdx,
                                        const MachineInstr *Orig) const {
  MachineInstr *MI = MBB.getParent()->CloneMachineInstr(Orig);
  MachineOperand &MO = MI->getOperand(0);
  MO.setReg(DestReg);
  MO.setSubReg(SubIdx);
  MBB.insert(I, MI);
}

bool TargetInstrInfoImpl::isDeadInstruction(const MachineInstr *MI) const {
  const TargetInstrDesc &TID = MI->getDesc();
  if (TID.mayLoad() || TID.mayStore() || TID.isCall() || TID.isTerminator() ||
      TID.isCall() || TID.isBarrier() || TID.isReturn() ||
      TID.hasUnmodeledSideEffects())
    return false;
  for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI->getOperand(i);
    if (!MO.isReg() || !MO.getReg())
      continue;
    if (MO.isDef() && !MO.isDead())
      return false;
    if (MO.isUse() && MO.isKill())
      // FIXME: We can't remove kill markers or else the scavenger will assert.
      // An alternative is to add a ADD pseudo instruction to replace kill
      // markers.
      return false;
  }
  return true;
}

unsigned
TargetInstrInfoImpl::GetFunctionSizeInBytes(const MachineFunction &MF) const {
  unsigned FnSize = 0;
  for (MachineFunction::const_iterator MBBI = MF.begin(), E = MF.end();
       MBBI != E; ++MBBI) {
    const MachineBasicBlock &MBB = *MBBI;
    for (MachineBasicBlock::const_iterator I = MBB.begin(),E = MBB.end();
         I != E; ++I)
      FnSize += GetInstSizeInBytes(I);
  }
  return FnSize;
}

/// foldMemoryOperand - Attempt to fold a load or store of the specified stack
/// slot into the specified machine instruction for the specified operand(s).
/// If this is possible, a new instruction is returned with the specified
/// operand folded, otherwise NULL is returned. The client is responsible for
/// removing the old instruction and adding the new one in the instruction
/// stream.
MachineInstr*
TargetInstrInfo::foldMemoryOperand(MachineFunction &MF,
                                   MachineInstr* MI,
                                   const SmallVectorImpl<unsigned> &Ops,
                                   int FrameIndex) const {
  unsigned Flags = 0;
  for (unsigned i = 0, e = Ops.size(); i != e; ++i)
    if (MI->getOperand(Ops[i]).isDef())
      Flags |= MachineMemOperand::MOStore;
    else
      Flags |= MachineMemOperand::MOLoad;

  // Ask the target to do the actual folding.
  MachineInstr *NewMI = foldMemoryOperandImpl(MF, MI, Ops, FrameIndex);
  if (!NewMI) return 0;

  assert((!(Flags & MachineMemOperand::MOStore) ||
          NewMI->getDesc().mayStore()) &&
         "Folded a def to a non-store!");
  assert((!(Flags & MachineMemOperand::MOLoad) ||
          NewMI->getDesc().mayLoad()) &&
         "Folded a use to a non-load!");
  const MachineFrameInfo &MFI = *MF.getFrameInfo();
  assert(MFI.getObjectOffset(FrameIndex) != -1);
  MachineMemOperand MMO(PseudoSourceValue::getFixedStack(FrameIndex),
                        Flags,
                        /*Offset=*/0,
                        MFI.getObjectSize(FrameIndex),
                        MFI.getObjectAlignment(FrameIndex));
  NewMI->addMemOperand(MF, MMO);

  return NewMI;
}

/// foldMemoryOperand - Same as the previous version except it allows folding
/// of any load and store from / to any address, not just from a specific
/// stack slot.
MachineInstr*
TargetInstrInfo::foldMemoryOperand(MachineFunction &MF,
                                   MachineInstr* MI,
                                   const SmallVectorImpl<unsigned> &Ops,
                                   MachineInstr* LoadMI) const {
  assert(LoadMI->getDesc().canFoldAsLoad() && "LoadMI isn't foldable!");
#ifndef NDEBUG
  for (unsigned i = 0, e = Ops.size(); i != e; ++i)
    assert(MI->getOperand(Ops[i]).isUse() && "Folding load into def!");
#endif

  // Ask the target to do the actual folding.
  MachineInstr *NewMI = foldMemoryOperandImpl(MF, MI, Ops, LoadMI);
  if (!NewMI) return 0;

  // Copy the memoperands from the load to the folded instruction.
  for (std::list<MachineMemOperand>::iterator I = LoadMI->memoperands_begin(),
       E = LoadMI->memoperands_end(); I != E; ++I)
    NewMI->addMemOperand(MF, *I);

  return NewMI;
}
