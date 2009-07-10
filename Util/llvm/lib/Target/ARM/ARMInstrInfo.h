//===- ARMInstrInfo.h - ARM Instruction Information -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the ARM implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef ARMINSTRUCTIONINFO_H
#define ARMINSTRUCTIONINFO_H

#include "llvm/Target/TargetInstrInfo.h"
#include "ARMBaseInstrInfo.h"
#include "ARMRegisterInfo.h"
#include "ARMSubtarget.h"
#include "ARM.h"

namespace llvm {
  class ARMSubtarget;

class ARMInstrInfo : public ARMBaseInstrInfo {
  ARMRegisterInfo RI;
public:
  explicit ARMInstrInfo(const ARMSubtarget &STI);

  // Return the non-pre/post incrementing version of 'Opc'. Return 0
  // if there is not such an opcode.
  unsigned getUnindexedOpcode(unsigned Opc) const;

  // Return the opcode that implements 'Op', or 0 if no opcode
  unsigned getOpcode(ARMII::Op Op) const;

  // Return true if the block does not fall through.
  bool BlockHasNoFallThrough(const MachineBasicBlock &MBB) const;

  /// getRegisterInfo - TargetInstrInfo is a superset of MRegister info.  As
  /// such, whenever a client has an instance of instruction info, it should
  /// always be able to get register info as well (through this method).
  ///
  const ARMRegisterInfo &getRegisterInfo() const { return RI; }

  void reMaterialize(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                     unsigned DestReg, const MachineInstr *Orig) const;
};

}

#endif
