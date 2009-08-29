//===- ARMConstantPoolValue.h - ARM constantpool value ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the ARM specific constantpool value class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_ARM_CONSTANTPOOLVALUE_H
#define LLVM_TARGET_ARM_CONSTANTPOOLVALUE_H

#include "llvm/CodeGen/MachineConstantPool.h"

namespace llvm {

class GlobalValue;
class LLVMContext;

/// ARMConstantPoolValue - ARM specific constantpool value. This is used to
/// represent PC relative displacement between the address of the load
/// instruction and the global value being loaded, i.e. (&GV-(LPIC+8)).
class ARMConstantPoolValue : public MachineConstantPoolValue {
  GlobalValue *GV;         // GlobalValue being loaded.
  const char *S;           // ExtSymbol being loaded.
  unsigned LabelId;        // Label id of the load.
  unsigned char PCAdjust;  // Extra adjustment if constantpool is pc relative.
                           // 8 for ARM, 4 for Thumb.
  const char *Modifier;    // GV modifier i.e. (&GV(modifier)-(LPIC+8))
  bool AddCurrentAddress;

public:
  ARMConstantPoolValue(GlobalValue *gv, unsigned id,
                       unsigned char PCAdj = 0, const char *Modifier = NULL,
                       bool AddCurrentAddress = false);
  ARMConstantPoolValue(LLVMContext &C, const char *s, unsigned id,
                       unsigned char PCAdj = 0, const char *Modifier = NULL,
                       bool AddCurrentAddress = false);
  ARMConstantPoolValue(GlobalValue *GV, const char *Modifier);
  ARMConstantPoolValue();
  ~ARMConstantPoolValue();


  GlobalValue *getGV() const { return GV; }
  const char *getSymbol() const { return S; }
  const char *getModifier() const { return Modifier; }
  bool hasModifier() const { return Modifier != NULL; }
  bool mustAddCurrentAddress() const { return AddCurrentAddress; }
  unsigned getLabelId() const { return LabelId; }
  unsigned char getPCAdjustment() const { return PCAdjust; }

  virtual unsigned getRelocationInfo() const {
    // FIXME: This is conservatively claiming that these entries require a
    // relocation, we may be able to do better than this.
    return 2;
  }


  virtual int getExistingMachineCPValue(MachineConstantPool *CP,
                                        unsigned Alignment);

  virtual void AddSelectionDAGCSEId(FoldingSetNodeID &ID);

  void print(raw_ostream *O) const { if (O) print(*O); }
  void print(raw_ostream &O) const;
  void dump() const;
};


inline raw_ostream &operator<<(raw_ostream &O, const ARMConstantPoolValue &V) {
  V.print(O);
  return O;
}

} // End llvm namespace

#endif
