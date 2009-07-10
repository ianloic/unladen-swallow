//===-- llvm/CodeGen/Spiller.cpp -  Spiller -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "spiller"

#include "Spiller.h"
#include "VirtRegMap.h"
#include "llvm/CodeGen/LiveIntervalAnalysis.h"
#include "llvm/CodeGen/LiveStackAnalysis.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

Spiller::~Spiller() {}

namespace {

/// Utility class for spillers.
class SpillerBase : public Spiller {
protected:

  MachineFunction *mf;
  LiveIntervals *lis;
  LiveStacks *ls;
  MachineFrameInfo *mfi;
  MachineRegisterInfo *mri;
  const TargetInstrInfo *tii;
  VirtRegMap *vrm;
  
  /// Construct a spiller base. 
  SpillerBase(MachineFunction *mf, LiveIntervals *lis, LiveStacks *ls,
              VirtRegMap *vrm) :
    mf(mf), lis(lis), ls(ls), vrm(vrm)
  {
    mfi = mf->getFrameInfo();
    mri = &mf->getRegInfo();
    tii = mf->getTarget().getInstrInfo();
  }

  /// Ensures there is space before the given machine instruction, returns the
  /// instruction's new number.
  unsigned makeSpaceBefore(MachineInstr *mi) {
    if (!lis->hasGapBeforeInstr(lis->getInstructionIndex(mi))) {
      lis->scaleNumbering(2);
      ls->scaleNumbering(2);
    }

    unsigned miIdx = lis->getInstructionIndex(mi);

    assert(lis->hasGapBeforeInstr(miIdx));
    
    return miIdx;
  }

  /// Ensure there is space after the given machine instruction, returns the
  /// instruction's new number.
  unsigned makeSpaceAfter(MachineInstr *mi) {
    if (!lis->hasGapAfterInstr(lis->getInstructionIndex(mi))) {
      lis->scaleNumbering(2);
      ls->scaleNumbering(2);
    }

    unsigned miIdx = lis->getInstructionIndex(mi);

    assert(lis->hasGapAfterInstr(miIdx));

    return miIdx;
  }  

  /// Insert a store of the given vreg to the given stack slot immediately
  /// after the given instruction. Returns the base index of the inserted
  /// instruction. The caller is responsible for adding an appropriate
  /// LiveInterval to the LiveIntervals analysis.
  unsigned insertStoreAfter(MachineInstr *mi, unsigned ss,
                          unsigned vreg,
                          const TargetRegisterClass *trc) {

    MachineBasicBlock::iterator nextInstItr(next(mi)); 

    unsigned miIdx = makeSpaceAfter(mi);

    tii->storeRegToStackSlot(*mi->getParent(), nextInstItr, vreg,
                             true, ss, trc);
    MachineBasicBlock::iterator storeInstItr(next(mi));
    MachineInstr *storeInst = &*storeInstItr;
    unsigned storeInstIdx = miIdx + LiveInterval::InstrSlots::NUM;

    assert(lis->getInstructionFromIndex(storeInstIdx) == 0 &&
           "Store inst index already in use.");
    
    lis->InsertMachineInstrInMaps(storeInst, storeInstIdx);

    return storeInstIdx;
  }

  /// Insert a store of the given vreg to the given stack slot immediately
  /// before the given instructnion. Returns the base index of the inserted
  /// Instruction.
  unsigned insertStoreBefore(MachineInstr *mi, unsigned ss,
                            unsigned vreg,
                            const TargetRegisterClass *trc) {
    unsigned miIdx = makeSpaceBefore(mi);
  
    tii->storeRegToStackSlot(*mi->getParent(), mi, vreg, true, ss, trc);
    MachineBasicBlock::iterator storeInstItr(prior(mi));
    MachineInstr *storeInst = &*storeInstItr;
    unsigned storeInstIdx = miIdx - LiveInterval::InstrSlots::NUM;

    assert(lis->getInstructionFromIndex(storeInstIdx) == 0 &&
           "Store inst index already in use.");

    lis->InsertMachineInstrInMaps(storeInst, storeInstIdx);

    return storeInstIdx;
  }

  void insertStoreAfterInstOnInterval(LiveInterval *li,
                                      MachineInstr *mi, unsigned ss,
                                      unsigned vreg,
                                      const TargetRegisterClass *trc) {

    unsigned storeInstIdx = insertStoreAfter(mi, ss, vreg, trc);
    unsigned start = lis->getDefIndex(lis->getInstructionIndex(mi)),
             end = lis->getUseIndex(storeInstIdx);

    VNInfo *vni =
      li->getNextValue(storeInstIdx, 0, true, lis->getVNInfoAllocator());
    vni->kills.push_back(storeInstIdx);
    DOUT << "    Inserting store range: [" << start << ", " << end << ")\n";
    LiveRange lr(start, end, vni);
      
    li->addRange(lr);
  }

  /// Insert a load of the given vreg from the given stack slot immediately
  /// after the given instruction. Returns the base index of the inserted
  /// instruction. The caller is responsibel for adding/removing an appropriate
  /// range vreg's LiveInterval.
  unsigned insertLoadAfter(MachineInstr *mi, unsigned ss,
                          unsigned vreg,
                          const TargetRegisterClass *trc) {

    MachineBasicBlock::iterator nextInstItr(next(mi)); 

    unsigned miIdx = makeSpaceAfter(mi);

    tii->loadRegFromStackSlot(*mi->getParent(), nextInstItr, vreg, ss, trc);
    MachineBasicBlock::iterator loadInstItr(next(mi));
    MachineInstr *loadInst = &*loadInstItr;
    unsigned loadInstIdx = miIdx + LiveInterval::InstrSlots::NUM;

    assert(lis->getInstructionFromIndex(loadInstIdx) == 0 &&
           "Store inst index already in use.");
    
    lis->InsertMachineInstrInMaps(loadInst, loadInstIdx);

    return loadInstIdx;
  }

  /// Insert a load of the given vreg from the given stack slot immediately
  /// before the given instruction. Returns the base index of the inserted
  /// instruction. The caller is responsible for adding an appropriate
  /// LiveInterval to the LiveIntervals analysis.
  unsigned insertLoadBefore(MachineInstr *mi, unsigned ss,
                            unsigned vreg,
                            const TargetRegisterClass *trc) {  
    unsigned miIdx = makeSpaceBefore(mi);
  
    tii->loadRegFromStackSlot(*mi->getParent(), mi, vreg, ss, trc);
    MachineBasicBlock::iterator loadInstItr(prior(mi));
    MachineInstr *loadInst = &*loadInstItr;
    unsigned loadInstIdx = miIdx - LiveInterval::InstrSlots::NUM;

    assert(lis->getInstructionFromIndex(loadInstIdx) == 0 &&
           "Load inst index already in use.");

    lis->InsertMachineInstrInMaps(loadInst, loadInstIdx);

    return loadInstIdx;
  }

  void insertLoadBeforeInstOnInterval(LiveInterval *li,
                                      MachineInstr *mi, unsigned ss, 
                                      unsigned vreg,
                                      const TargetRegisterClass *trc) {

    unsigned loadInstIdx = insertLoadBefore(mi, ss, vreg, trc);
    unsigned start = lis->getDefIndex(loadInstIdx),
             end = lis->getUseIndex(lis->getInstructionIndex(mi));

    VNInfo *vni =
      li->getNextValue(loadInstIdx, 0, true, lis->getVNInfoAllocator());
    vni->kills.push_back(lis->getInstructionIndex(mi));
    DOUT << "    Intserting load range: [" << start << ", " << end << ")\n";
    LiveRange lr(start, end, vni);

    li->addRange(lr);
  }



  /// Add spill ranges for every use/def of the live interval, inserting loads
  /// immediately before each use, and stores after each def. No folding is
  /// attempted.
  std::vector<LiveInterval*> trivialSpillEverywhere(LiveInterval *li) {
    DOUT << "Spilling everywhere " << *li << "\n";

    assert(li->weight != HUGE_VALF &&
           "Attempting to spill already spilled value.");

    assert(!li->isStackSlot() &&
           "Trying to spill a stack slot.");

    DOUT << "Trivial spill everywhere of reg" << li->reg << "\n";

    std::vector<LiveInterval*> added;
    
    const TargetRegisterClass *trc = mri->getRegClass(li->reg);
    unsigned ss = vrm->assignVirt2StackSlot(li->reg);

    for (MachineRegisterInfo::reg_iterator
         regItr = mri->reg_begin(li->reg); regItr != mri->reg_end();) {

      MachineInstr *mi = &*regItr;

      DOUT << "  Processing " << *mi;

      do {
        ++regItr;
      } while (regItr != mri->reg_end() && (&*regItr == mi));
      
      SmallVector<unsigned, 2> indices;
      bool hasUse = false;
      bool hasDef = false;
    
      for (unsigned i = 0; i != mi->getNumOperands(); ++i) {
        MachineOperand &op = mi->getOperand(i);

        if (!op.isReg() || op.getReg() != li->reg)
          continue;
      
        hasUse |= mi->getOperand(i).isUse();
        hasDef |= mi->getOperand(i).isDef();
      
        indices.push_back(i);
      }

      unsigned newVReg = mri->createVirtualRegister(trc);
      vrm->grow();
      vrm->assignVirt2StackSlot(newVReg, ss);

      LiveInterval *newLI = &lis->getOrCreateInterval(newVReg);
      newLI->weight = HUGE_VALF;
      
      for (unsigned i = 0; i < indices.size(); ++i) {
        mi->getOperand(indices[i]).setReg(newVReg);

        if (mi->getOperand(indices[i]).isUse()) {
          mi->getOperand(indices[i]).setIsKill(true);
        }
      }

      assert(hasUse || hasDef);

      if (hasUse) {
        insertLoadBeforeInstOnInterval(newLI, mi, ss, newVReg, trc);
      }

      if (hasDef) {
        insertStoreAfterInstOnInterval(newLI, mi, ss, newVReg, trc);
      }

      added.push_back(newLI);
    }

    return added;
  }

};


/// Spills any live range using the spill-everywhere method with no attempt at
/// folding.
class TrivialSpiller : public SpillerBase {
public:

  TrivialSpiller(MachineFunction *mf, LiveIntervals *lis, LiveStacks *ls,
                 VirtRegMap *vrm) :
    SpillerBase(mf, lis, ls, vrm) {}

  std::vector<LiveInterval*> spill(LiveInterval *li) {
    return trivialSpillEverywhere(li);
  }

  std::vector<LiveInterval*> intraBlockSplit(LiveInterval *li, VNInfo *valno)  {
    std::vector<LiveInterval*> spillIntervals;

    if (!valno->isDefAccurate() && !valno->isPHIDef()) {
      // Early out for values which have no well defined def point.
      return spillIntervals;
    }

    // Ok.. we should be able to proceed...
    const TargetRegisterClass *trc = mri->getRegClass(li->reg);
    unsigned ss = vrm->assignVirt2StackSlot(li->reg);    
    vrm->grow();
    vrm->assignVirt2StackSlot(li->reg, ss);

    MachineInstr *mi = 0;
    unsigned storeIdx = 0;

    if (valno->isDefAccurate()) {
      // If we have an accurate def we can just grab an iterator to the instr
      // after the def.
      mi = lis->getInstructionFromIndex(valno->def);
      storeIdx = insertStoreAfter(mi, ss, li->reg, trc) +
        LiveInterval::InstrSlots::DEF;
    } else {
      // if we get here we have a PHI def.
      mi = &lis->getMBBFromIndex(valno->def)->front();
      storeIdx = insertStoreBefore(mi, ss, li->reg, trc) +
        LiveInterval::InstrSlots::DEF;
    }

    MachineBasicBlock *defBlock = mi->getParent();
    unsigned loadIdx = 0;

    // Now we need to find the load...
    MachineBasicBlock::iterator useItr(mi);
    for (; !useItr->readsRegister(li->reg); ++useItr) {}

    if (useItr != defBlock->end()) {
      MachineInstr *loadInst = useItr;
      loadIdx = insertLoadBefore(loadInst, ss, li->reg, trc) +
        LiveInterval::InstrSlots::USE;
    }
    else {
      MachineInstr *loadInst = &defBlock->back();
      loadIdx = insertLoadAfter(loadInst, ss, li->reg, trc) +
        LiveInterval::InstrSlots::USE;
    }

    li->removeRange(storeIdx, loadIdx, true);

    return spillIntervals;
  }

};

}

llvm::Spiller* llvm::createSpiller(MachineFunction *mf, LiveIntervals *lis,
                                   LiveStacks *ls, VirtRegMap *vrm) {
  return new TrivialSpiller(mf, lis, ls, vrm);
}
