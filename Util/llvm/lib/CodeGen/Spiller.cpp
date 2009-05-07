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
#include "llvm/Support/Compiler.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/STLExtras.h"
#include <algorithm>
using namespace llvm;

STATISTIC(NumDSE     , "Number of dead stores elided");
STATISTIC(NumDSS     , "Number of dead spill slots removed");
STATISTIC(NumCommutes, "Number of instructions commuted");
STATISTIC(NumDRM     , "Number of re-materializable defs elided");
STATISTIC(NumStores  , "Number of stores added");
STATISTIC(NumPSpills , "Number of physical register spills");
STATISTIC(NumOmitted , "Number of reloads omited");
STATISTIC(NumAvoided , "Number of reloads deemed unnecessary");
STATISTIC(NumCopified, "Number of available reloads turned into copies");
STATISTIC(NumReMats  , "Number of re-materialization");
STATISTIC(NumLoads   , "Number of loads added");
STATISTIC(NumReused  , "Number of values reused");
STATISTIC(NumDCE     , "Number of copies elided");
STATISTIC(NumSUnfold , "Number of stores unfolded");
STATISTIC(NumModRefUnfold, "Number of modref unfolded");

namespace {
  enum SpillerName { simple, local };
}

static cl::opt<SpillerName>
SpillerOpt("spiller",
           cl::desc("Spiller to use: (default: local)"),
           cl::Prefix,
           cl::values(clEnumVal(simple, "simple spiller"),
                      clEnumVal(local,  "local spiller"),
                      clEnumValEnd),
           cl::init(local));

// ****************************** //
// Simple Spiller Implementation  //
// ****************************** //

Spiller::~Spiller() {}

bool SimpleSpiller::runOnMachineFunction(MachineFunction &MF, VirtRegMap &VRM,
                                         LiveIntervals* LIs) {
  DOUT << "********** REWRITE MACHINE CODE **********\n";
  DOUT << "********** Function: " << MF.getFunction()->getName() << '\n';
  const TargetMachine &TM = MF.getTarget();
  const TargetInstrInfo &TII = *TM.getInstrInfo();
  const TargetRegisterInfo &TRI = *TM.getRegisterInfo();


  // LoadedRegs - Keep track of which vregs are loaded, so that we only load
  // each vreg once (in the case where a spilled vreg is used by multiple
  // operands).  This is always smaller than the number of operands to the
  // current machine instr, so it should be small.
  std::vector<unsigned> LoadedRegs;

  for (MachineFunction::iterator MBBI = MF.begin(), E = MF.end();
       MBBI != E; ++MBBI) {
    DOUT << MBBI->getBasicBlock()->getName() << ":\n";
    MachineBasicBlock &MBB = *MBBI;
    for (MachineBasicBlock::iterator MII = MBB.begin(), E = MBB.end();
         MII != E; ++MII) {
      MachineInstr &MI = *MII;
      for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
        MachineOperand &MO = MI.getOperand(i);
        if (MO.isReg() && MO.getReg()) {
          if (TargetRegisterInfo::isVirtualRegister(MO.getReg())) {
            unsigned VirtReg = MO.getReg();
            unsigned SubIdx = MO.getSubReg();
            unsigned PhysReg = VRM.getPhys(VirtReg);
            unsigned RReg = SubIdx ? TRI.getSubReg(PhysReg, SubIdx) : PhysReg;
            if (!VRM.isAssignedReg(VirtReg)) {
              int StackSlot = VRM.getStackSlot(VirtReg);
              const TargetRegisterClass* RC = 
                                           MF.getRegInfo().getRegClass(VirtReg);
              
              if (MO.isUse() &&
                  std::find(LoadedRegs.begin(), LoadedRegs.end(), VirtReg)
                           == LoadedRegs.end()) {
                TII.loadRegFromStackSlot(MBB, &MI, PhysReg, StackSlot, RC);
                MachineInstr *LoadMI = prior(MII);
                VRM.addSpillSlotUse(StackSlot, LoadMI);
                LoadedRegs.push_back(VirtReg);
                ++NumLoads;
                DOUT << '\t' << *LoadMI;
              }

              if (MO.isDef()) {
                TII.storeRegToStackSlot(MBB, next(MII), PhysReg, true,   
                                        StackSlot, RC);
                MachineInstr *StoreMI = next(MII);
                VRM.addSpillSlotUse(StackSlot, StoreMI);
                ++NumStores;
              }
            }
            MF.getRegInfo().setPhysRegUsed(RReg);
            MI.getOperand(i).setReg(RReg);
            MI.getOperand(i).setSubReg(0);
          } else {
            MF.getRegInfo().setPhysRegUsed(MO.getReg());
          }
        }
      }

      DOUT << '\t' << MI;
      LoadedRegs.clear();
    }
  }
  return true;
}

// ****************** //
// Utility Functions  //
// ****************** //

/// InvalidateKill - A MI that defines the specified register is being deleted,
/// invalidate the register kill information.
static void InvalidateKill(unsigned Reg, BitVector &RegKills,
                           std::vector<MachineOperand*> &KillOps) {
  if (RegKills[Reg]) {
    KillOps[Reg]->setIsKill(false);
    KillOps[Reg] = NULL;
    RegKills.reset(Reg);
  }
}

/// findSinglePredSuccessor - Return via reference a vector of machine basic
/// blocks each of which is a successor of the specified BB and has no other
/// predecessor.
static void findSinglePredSuccessor(MachineBasicBlock *MBB,
                                   SmallVectorImpl<MachineBasicBlock *> &Succs) {
  for (MachineBasicBlock::succ_iterator SI = MBB->succ_begin(),
         SE = MBB->succ_end(); SI != SE; ++SI) {
    MachineBasicBlock *SuccMBB = *SI;
    if (SuccMBB->pred_size() == 1)
      Succs.push_back(SuccMBB);
  }
}

/// InvalidateKills - MI is going to be deleted. If any of its operands are
/// marked kill, then invalidate the information.
static void InvalidateKills(MachineInstr &MI, BitVector &RegKills,
                            std::vector<MachineOperand*> &KillOps,
                            SmallVector<unsigned, 2> *KillRegs = NULL) {
  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    MachineOperand &MO = MI.getOperand(i);
    if (!MO.isReg() || !MO.isUse() || !MO.isKill())
      continue;
    unsigned Reg = MO.getReg();
    if (TargetRegisterInfo::isVirtualRegister(Reg))
      continue;
    if (KillRegs)
      KillRegs->push_back(Reg);
    assert(Reg < KillOps.size());
    if (KillOps[Reg] == &MO) {
      RegKills.reset(Reg);
      KillOps[Reg] = NULL;
    }
  }
}

/// InvalidateRegDef - If the def operand of the specified def MI is now dead
/// (since it's spill instruction is removed), mark it isDead. Also checks if
/// the def MI has other definition operands that are not dead. Returns it by
/// reference.
static bool InvalidateRegDef(MachineBasicBlock::iterator I,
                             MachineInstr &NewDef, unsigned Reg,
                             bool &HasLiveDef) {
  // Due to remat, it's possible this reg isn't being reused. That is,
  // the def of this reg (by prev MI) is now dead.
  MachineInstr *DefMI = I;
  MachineOperand *DefOp = NULL;
  for (unsigned i = 0, e = DefMI->getNumOperands(); i != e; ++i) {
    MachineOperand &MO = DefMI->getOperand(i);
    if (MO.isReg() && MO.isDef()) {
      if (MO.getReg() == Reg)
        DefOp = &MO;
      else if (!MO.isDead())
        HasLiveDef = true;
    }
  }
  if (!DefOp)
    return false;

  bool FoundUse = false, Done = false;
  MachineBasicBlock::iterator E = &NewDef;
  ++I; ++E;
  for (; !Done && I != E; ++I) {
    MachineInstr *NMI = I;
    for (unsigned j = 0, ee = NMI->getNumOperands(); j != ee; ++j) {
      MachineOperand &MO = NMI->getOperand(j);
      if (!MO.isReg() || MO.getReg() != Reg)
        continue;
      if (MO.isUse())
        FoundUse = true;
      Done = true; // Stop after scanning all the operands of this MI.
    }
  }
  if (!FoundUse) {
    // Def is dead!
    DefOp->setIsDead();
    return true;
  }
  return false;
}

/// UpdateKills - Track and update kill info. If a MI reads a register that is
/// marked kill, then it must be due to register reuse. Transfer the kill info
/// over.
static void UpdateKills(MachineInstr &MI, BitVector &RegKills,
                        std::vector<MachineOperand*> &KillOps,
                        const TargetRegisterInfo* TRI) {
  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    MachineOperand &MO = MI.getOperand(i);
    if (!MO.isReg() || !MO.isUse())
      continue;
    unsigned Reg = MO.getReg();
    if (Reg == 0)
      continue;
    
    if (RegKills[Reg] && KillOps[Reg]->getParent() != &MI) {
      // That can't be right. Register is killed but not re-defined and it's
      // being reused. Let's fix that.
      KillOps[Reg]->setIsKill(false);
      KillOps[Reg] = NULL;
      RegKills.reset(Reg);
      if (!MI.isRegTiedToDefOperand(i))
        // Unless it's a two-address operand, this is the new kill.
        MO.setIsKill();
    }
    if (MO.isKill()) {
      RegKills.set(Reg);
      KillOps[Reg] = &MO;
    }
  }

  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI.getOperand(i);
    if (!MO.isReg() || !MO.isDef())
      continue;
    unsigned Reg = MO.getReg();
    RegKills.reset(Reg);
    KillOps[Reg] = NULL;
    // It also defines (or partially define) aliases.
    for (const unsigned *AS = TRI->getAliasSet(Reg); *AS; ++AS) {
      RegKills.reset(*AS);
      KillOps[*AS] = NULL;
    }
  }
}

/// ReMaterialize - Re-materialize definition for Reg targetting DestReg.
///
static void ReMaterialize(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator &MII,
                          unsigned DestReg, unsigned Reg,
                          const TargetInstrInfo *TII,
                          const TargetRegisterInfo *TRI,
                          VirtRegMap &VRM) {
  TII->reMaterialize(MBB, MII, DestReg, VRM.getReMaterializedMI(Reg));
  MachineInstr *NewMI = prior(MII);
  for (unsigned i = 0, e = NewMI->getNumOperands(); i != e; ++i) {
    MachineOperand &MO = NewMI->getOperand(i);
    if (!MO.isReg() || MO.getReg() == 0)
      continue;
    unsigned VirtReg = MO.getReg();
    if (TargetRegisterInfo::isPhysicalRegister(VirtReg))
      continue;
    assert(MO.isUse());
    unsigned SubIdx = MO.getSubReg();
    unsigned Phys = VRM.getPhys(VirtReg);
    assert(Phys);
    unsigned RReg = SubIdx ? TRI->getSubReg(Phys, SubIdx) : Phys;
    MO.setReg(RReg);
    MO.setSubReg(0);
  }
  ++NumReMats;
}

/// findSuperReg - Find the SubReg's super-register of given register class
/// where its SubIdx sub-register is SubReg.
static unsigned findSuperReg(const TargetRegisterClass *RC, unsigned SubReg,
                             unsigned SubIdx, const TargetRegisterInfo *TRI) {
  for (TargetRegisterClass::iterator I = RC->begin(), E = RC->end();
       I != E; ++I) {
    unsigned Reg = *I;
    if (TRI->getSubReg(Reg, SubIdx) == SubReg)
      return Reg;
  }
  return 0;
}

// ******************************** //
// Available Spills Implementation  //
// ******************************** //

/// disallowClobberPhysRegOnly - Unset the CanClobber bit of the specified
/// stackslot register. The register is still available but is no longer
/// allowed to be modifed.
void AvailableSpills::disallowClobberPhysRegOnly(unsigned PhysReg) {
  std::multimap<unsigned, int>::iterator I =
    PhysRegsAvailable.lower_bound(PhysReg);
  while (I != PhysRegsAvailable.end() && I->first == PhysReg) {
    int SlotOrReMat = I->second;
    I++;
    assert((SpillSlotsOrReMatsAvailable[SlotOrReMat] >> 1) == PhysReg &&
           "Bidirectional map mismatch!");
    SpillSlotsOrReMatsAvailable[SlotOrReMat] &= ~1;
    DOUT << "PhysReg " << TRI->getName(PhysReg)
         << " copied, it is available for use but can no longer be modified\n";
  }
}

/// disallowClobberPhysReg - Unset the CanClobber bit of the specified
/// stackslot register and its aliases. The register and its aliases may
/// still available but is no longer allowed to be modifed.
void AvailableSpills::disallowClobberPhysReg(unsigned PhysReg) {
  for (const unsigned *AS = TRI->getAliasSet(PhysReg); *AS; ++AS)
    disallowClobberPhysRegOnly(*AS);
  disallowClobberPhysRegOnly(PhysReg);
}

/// ClobberPhysRegOnly - This is called when the specified physreg changes
/// value.  We use this to invalidate any info about stuff we thing lives in it.
void AvailableSpills::ClobberPhysRegOnly(unsigned PhysReg) {
  std::multimap<unsigned, int>::iterator I =
    PhysRegsAvailable.lower_bound(PhysReg);
  while (I != PhysRegsAvailable.end() && I->first == PhysReg) {
    int SlotOrReMat = I->second;
    PhysRegsAvailable.erase(I++);
    assert((SpillSlotsOrReMatsAvailable[SlotOrReMat] >> 1) == PhysReg &&
           "Bidirectional map mismatch!");
    SpillSlotsOrReMatsAvailable.erase(SlotOrReMat);
    DOUT << "PhysReg " << TRI->getName(PhysReg)
         << " clobbered, invalidating ";
    if (SlotOrReMat > VirtRegMap::MAX_STACK_SLOT)
      DOUT << "RM#" << SlotOrReMat-VirtRegMap::MAX_STACK_SLOT-1 << "\n";
    else
      DOUT << "SS#" << SlotOrReMat << "\n";
  }
}

/// ClobberPhysReg - This is called when the specified physreg changes
/// value.  We use this to invalidate any info about stuff we thing lives in
/// it and any of its aliases.
void AvailableSpills::ClobberPhysReg(unsigned PhysReg) {
  for (const unsigned *AS = TRI->getAliasSet(PhysReg); *AS; ++AS)
    ClobberPhysRegOnly(*AS);
  ClobberPhysRegOnly(PhysReg);
}

/// AddAvailableRegsToLiveIn - Availability information is being kept coming
/// into the specified MBB. Add available physical registers as potential
/// live-in's. If they are reused in the MBB, they will be added to the
/// live-in set to make register scavenger and post-allocation scheduler.
void AvailableSpills::AddAvailableRegsToLiveIn(MachineBasicBlock &MBB,
                                        BitVector &RegKills,
                                        std::vector<MachineOperand*> &KillOps) {
  std::set<unsigned> NotAvailable;
  for (std::multimap<unsigned, int>::iterator
         I = PhysRegsAvailable.begin(), E = PhysRegsAvailable.end();
       I != E; ++I) {
    unsigned Reg = I->first;
    const TargetRegisterClass* RC = TRI->getPhysicalRegisterRegClass(Reg);
    // FIXME: A temporary workaround. We can't reuse available value if it's
    // not safe to move the def of the virtual register's class. e.g.
    // X86::RFP* register classes. Do not add it as a live-in.
    if (!TII->isSafeToMoveRegClassDefs(RC))
      // This is no longer available.
      NotAvailable.insert(Reg);
    else {
      MBB.addLiveIn(Reg);
      InvalidateKill(Reg, RegKills, KillOps);
    }

    // Skip over the same register.
    std::multimap<unsigned, int>::iterator NI = next(I);
    while (NI != E && NI->first == Reg) {
      ++I;
      ++NI;
    }
  }

  for (std::set<unsigned>::iterator I = NotAvailable.begin(),
         E = NotAvailable.end(); I != E; ++I) {
    ClobberPhysReg(*I);
    for (const unsigned *SubRegs = TRI->getSubRegisters(*I);
       *SubRegs; ++SubRegs)
      ClobberPhysReg(*SubRegs);
  }
}

/// ModifyStackSlotOrReMat - This method is called when the value in a stack
/// slot changes.  This removes information about which register the previous
/// value for this slot lives in (as the previous value is dead now).
void AvailableSpills::ModifyStackSlotOrReMat(int SlotOrReMat) {
  std::map<int, unsigned>::iterator It =
    SpillSlotsOrReMatsAvailable.find(SlotOrReMat);
  if (It == SpillSlotsOrReMatsAvailable.end()) return;
  unsigned Reg = It->second >> 1;
  SpillSlotsOrReMatsAvailable.erase(It);
  
  // This register may hold the value of multiple stack slots, only remove this
  // stack slot from the set of values the register contains.
  std::multimap<unsigned, int>::iterator I = PhysRegsAvailable.lower_bound(Reg);
  for (; ; ++I) {
    assert(I != PhysRegsAvailable.end() && I->first == Reg &&
           "Map inverse broken!");
    if (I->second == SlotOrReMat) break;
  }
  PhysRegsAvailable.erase(I);
}

// ************************** //
// Reuse Info Implementation  //
// ************************** //

/// GetRegForReload - We are about to emit a reload into PhysReg.  If there
/// is some other operand that is using the specified register, either pick
/// a new register to use, or evict the previous reload and use this reg.
unsigned ReuseInfo::GetRegForReload(unsigned PhysReg, MachineInstr *MI,
                         AvailableSpills &Spills,
                         std::vector<MachineInstr*> &MaybeDeadStores,
                         SmallSet<unsigned, 8> &Rejected,
                         BitVector &RegKills,
                         std::vector<MachineOperand*> &KillOps,
                         VirtRegMap &VRM) {
  const TargetInstrInfo* TII = MI->getParent()->getParent()->getTarget()
                               .getInstrInfo();
  
  if (Reuses.empty()) return PhysReg;  // This is most often empty.

  for (unsigned ro = 0, e = Reuses.size(); ro != e; ++ro) {
    ReusedOp &Op = Reuses[ro];
    // If we find some other reuse that was supposed to use this register
    // exactly for its reload, we can change this reload to use ITS reload
    // register. That is, unless its reload register has already been
    // considered and subsequently rejected because it has also been reused
    // by another operand.
    if (Op.PhysRegReused == PhysReg &&
        Rejected.count(Op.AssignedPhysReg) == 0) {
      // Yup, use the reload register that we didn't use before.
      unsigned NewReg = Op.AssignedPhysReg;
      Rejected.insert(PhysReg);
      return GetRegForReload(NewReg, MI, Spills, MaybeDeadStores, Rejected,
                             RegKills, KillOps, VRM);
    } else {
      // Otherwise, we might also have a problem if a previously reused
      // value aliases the new register.  If so, codegen the previous reload
      // and use this one.          
      unsigned PRRU = Op.PhysRegReused;
      const TargetRegisterInfo *TRI = Spills.getRegInfo();
      if (TRI->areAliases(PRRU, PhysReg)) {
        // Okay, we found out that an alias of a reused register
        // was used.  This isn't good because it means we have
        // to undo a previous reuse.
        MachineBasicBlock *MBB = MI->getParent();
        const TargetRegisterClass *AliasRC =
          MBB->getParent()->getRegInfo().getRegClass(Op.VirtReg);

        // Copy Op out of the vector and remove it, we're going to insert an
        // explicit load for it.
        ReusedOp NewOp = Op;
        Reuses.erase(Reuses.begin()+ro);

        // Ok, we're going to try to reload the assigned physreg into the
        // slot that we were supposed to in the first place.  However, that
        // register could hold a reuse.  Check to see if it conflicts or
        // would prefer us to use a different register.
        unsigned NewPhysReg = GetRegForReload(NewOp.AssignedPhysReg,
                                              MI, Spills, MaybeDeadStores,
                                          Rejected, RegKills, KillOps, VRM);
        
        MachineBasicBlock::iterator MII = MI;
        if (NewOp.StackSlotOrReMat > VirtRegMap::MAX_STACK_SLOT) {
          ReMaterialize(*MBB, MII, NewPhysReg, NewOp.VirtReg, TII, TRI,VRM);
        } else {
          TII->loadRegFromStackSlot(*MBB, MII, NewPhysReg,
                                    NewOp.StackSlotOrReMat, AliasRC);
          MachineInstr *LoadMI = prior(MII);
          VRM.addSpillSlotUse(NewOp.StackSlotOrReMat, LoadMI);
          // Any stores to this stack slot are not dead anymore.
          MaybeDeadStores[NewOp.StackSlotOrReMat] = NULL;            
          ++NumLoads;
        }
        Spills.ClobberPhysReg(NewPhysReg);
        Spills.ClobberPhysReg(NewOp.PhysRegReused);

        unsigned SubIdx = MI->getOperand(NewOp.Operand).getSubReg();
        unsigned RReg = SubIdx ? TRI->getSubReg(NewPhysReg, SubIdx) : NewPhysReg;
        MI->getOperand(NewOp.Operand).setReg(RReg);
        MI->getOperand(NewOp.Operand).setSubReg(0);

        Spills.addAvailable(NewOp.StackSlotOrReMat, NewPhysReg);
        --MII;
        UpdateKills(*MII, RegKills, KillOps, TRI);
        DOUT << '\t' << *MII;
        
        DOUT << "Reuse undone!\n";
        --NumReused;
        
        // Finally, PhysReg is now available, go ahead and use it.
        return PhysReg;
      }
    }
  }
  return PhysReg;
}

// ***************************** //
// Local Spiller Implementation  //
// ***************************** //

bool LocalSpiller::runOnMachineFunction(MachineFunction &MF, VirtRegMap &VRM,
                                        LiveIntervals* LIs) {
  RegInfo = &MF.getRegInfo(); 
  TRI = MF.getTarget().getRegisterInfo();
  TII = MF.getTarget().getInstrInfo();
  AllocatableRegs = TRI->getAllocatableSet(MF);
  DOUT << "\n**** Local spiller rewriting function '"
       << MF.getFunction()->getName() << "':\n";
  DOUT << "**** Machine Instrs (NOTE! Does not include spills and reloads!)"
          " ****\n";
  DEBUG(MF.dump());

  // Spills - Keep track of which spilled values are available in physregs
  // so that we can choose to reuse the physregs instead of emitting
  // reloads. This is usually refreshed per basic block.
  AvailableSpills Spills(TRI, TII);

  // Keep track of kill information.
  BitVector RegKills(TRI->getNumRegs());
  std::vector<MachineOperand*> KillOps;
  KillOps.resize(TRI->getNumRegs(), NULL);

  // SingleEntrySuccs - Successor blocks which have a single predecessor.
  SmallVector<MachineBasicBlock*, 4> SinglePredSuccs;
  SmallPtrSet<MachineBasicBlock*,16> EarlyVisited;

  // Traverse the basic blocks depth first.
  MachineBasicBlock *Entry = MF.begin();
  SmallPtrSet<MachineBasicBlock*,16> Visited;
  for (df_ext_iterator<MachineBasicBlock*,
         SmallPtrSet<MachineBasicBlock*,16> >
         DFI = df_ext_begin(Entry, Visited), E = df_ext_end(Entry, Visited);
       DFI != E; ++DFI) {
    MachineBasicBlock *MBB = *DFI;
    if (!EarlyVisited.count(MBB))
      RewriteMBB(*MBB, VRM, LIs, Spills, RegKills, KillOps);

    // If this MBB is the only predecessor of a successor. Keep the
    // availability information and visit it next.
    do {
      // Keep visiting single predecessor successor as long as possible.
      SinglePredSuccs.clear();
      findSinglePredSuccessor(MBB, SinglePredSuccs);
      if (SinglePredSuccs.empty())
        MBB = 0;
      else {
        // FIXME: More than one successors, each of which has MBB has
        // the only predecessor.
        MBB = SinglePredSuccs[0];
        if (!Visited.count(MBB) && EarlyVisited.insert(MBB)) {
          Spills.AddAvailableRegsToLiveIn(*MBB, RegKills, KillOps);
          RewriteMBB(*MBB, VRM, LIs, Spills, RegKills, KillOps);
        }
      }
    } while (MBB);

    // Clear the availability info.
    Spills.clear();
  }

  DOUT << "**** Post Machine Instrs ****\n";
  DEBUG(MF.dump());

  // Mark unused spill slots.
  MachineFrameInfo *MFI = MF.getFrameInfo();
  int SS = VRM.getLowSpillSlot();
  if (SS != VirtRegMap::NO_STACK_SLOT)
    for (int e = VRM.getHighSpillSlot(); SS <= e; ++SS)
      if (!VRM.isSpillSlotUsed(SS)) {
        MFI->RemoveStackObject(SS);
        ++NumDSS;
      }

  return true;
}


/// FoldsStackSlotModRef - Return true if the specified MI folds the specified
/// stack slot mod/ref. It also checks if it's possible to unfold the
/// instruction by having it define a specified physical register instead.
static bool FoldsStackSlotModRef(MachineInstr &MI, int SS, unsigned PhysReg,
                                 const TargetInstrInfo *TII,
                                 const TargetRegisterInfo *TRI,
                                 VirtRegMap &VRM) {
  if (VRM.hasEmergencySpills(&MI) || VRM.isSpillPt(&MI))
    return false;

  bool Found = false;
  VirtRegMap::MI2VirtMapTy::const_iterator I, End;
  for (tie(I, End) = VRM.getFoldedVirts(&MI); I != End; ++I) {
    unsigned VirtReg = I->second.first;
    VirtRegMap::ModRef MR = I->second.second;
    if (MR & VirtRegMap::isModRef)
      if (VRM.getStackSlot(VirtReg) == SS) {
        Found= TII->getOpcodeAfterMemoryUnfold(MI.getOpcode(), true, true) != 0;
        break;
      }
  }
  if (!Found)
    return false;

  // Does the instruction uses a register that overlaps the scratch register?
  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    MachineOperand &MO = MI.getOperand(i);
    if (!MO.isReg() || MO.getReg() == 0)
      continue;
    unsigned Reg = MO.getReg();
    if (TargetRegisterInfo::isVirtualRegister(Reg)) {
      if (!VRM.hasPhys(Reg))
        continue;
      Reg = VRM.getPhys(Reg);
    }
    if (TRI->regsOverlap(PhysReg, Reg))
      return false;
  }
  return true;
}

/// FindFreeRegister - Find a free register of a given register class by looking
/// at (at most) the last two machine instructions.
static unsigned FindFreeRegister(MachineBasicBlock::iterator MII,
                                 MachineBasicBlock &MBB,
                                 const TargetRegisterClass *RC,
                                 const TargetRegisterInfo *TRI,
                                 BitVector &AllocatableRegs) {
  BitVector Defs(TRI->getNumRegs());
  BitVector Uses(TRI->getNumRegs());
  SmallVector<unsigned, 4> LocalUses;
  SmallVector<unsigned, 4> Kills;

  // Take a look at 2 instructions at most.
  for (unsigned Count = 0; Count < 2; ++Count) {
    if (MII == MBB.begin())
      break;
    MachineInstr *PrevMI = prior(MII);
    for (unsigned i = 0, e = PrevMI->getNumOperands(); i != e; ++i) {
      MachineOperand &MO = PrevMI->getOperand(i);
      if (!MO.isReg() || MO.getReg() == 0)
        continue;
      unsigned Reg = MO.getReg();
      if (MO.isDef()) {
        Defs.set(Reg);
        for (const unsigned *AS = TRI->getAliasSet(Reg); *AS; ++AS)
          Defs.set(*AS);
      } else  {
        LocalUses.push_back(Reg);
        if (MO.isKill() && AllocatableRegs[Reg])
          Kills.push_back(Reg);
      }
    }

    for (unsigned i = 0, e = Kills.size(); i != e; ++i) {
      unsigned Kill = Kills[i];
      if (!Defs[Kill] && !Uses[Kill] &&
          TRI->getPhysicalRegisterRegClass(Kill) == RC)
        return Kill;
    }
    for (unsigned i = 0, e = LocalUses.size(); i != e; ++i) {
      unsigned Reg = LocalUses[i];
      Uses.set(Reg);
      for (const unsigned *AS = TRI->getAliasSet(Reg); *AS; ++AS)
        Uses.set(*AS);
    }

    MII = PrevMI;
  }

  return 0;
}

static
void AssignPhysToVirtReg(MachineInstr *MI, unsigned VirtReg, unsigned PhysReg) {
  for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
    MachineOperand &MO = MI->getOperand(i);
    if (MO.isReg() && MO.getReg() == VirtReg)
      MO.setReg(PhysReg);
  }
}

/// OptimizeByUnfold2 - Unfold a series of load / store folding instructions if
/// a scratch register is available.
///     xorq  %r12<kill>, %r13
///     addq  %rax, -184(%rbp)
///     addq  %r13, -184(%rbp)
/// ==>
///     xorq  %r12<kill>, %r13
///     movq  -184(%rbp), %r12
///     addq  %rax, %r12
///     addq  %r13, %r12
///     movq  %r12, -184(%rbp)
bool LocalSpiller::OptimizeByUnfold2(unsigned VirtReg, int SS,
                                    MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator &MII,
                                    std::vector<MachineInstr*> &MaybeDeadStores,
                                    AvailableSpills &Spills,
                                    BitVector &RegKills,
                                    std::vector<MachineOperand*> &KillOps,
                                    VirtRegMap &VRM) {
  MachineBasicBlock::iterator NextMII = next(MII);
  if (NextMII == MBB.end())
    return false;

  if (TII->getOpcodeAfterMemoryUnfold(MII->getOpcode(), true, true) == 0)
    return false;

  // Now let's see if the last couple of instructions happens to have freed up
  // a register.
  const TargetRegisterClass* RC = RegInfo->getRegClass(VirtReg);
  unsigned PhysReg = FindFreeRegister(MII, MBB, RC, TRI, AllocatableRegs);
  if (!PhysReg)
    return false;

  MachineFunction &MF = *MBB.getParent();
  TRI = MF.getTarget().getRegisterInfo();
  MachineInstr &MI = *MII;
  if (!FoldsStackSlotModRef(MI, SS, PhysReg, TII, TRI, VRM))
    return false;

  // If the next instruction also folds the same SS modref and can be unfoled,
  // then it's worthwhile to issue a load from SS into the free register and
  // then unfold these instructions.
  if (!FoldsStackSlotModRef(*NextMII, SS, PhysReg, TII, TRI, VRM))
    return false;

  // Load from SS to the spare physical register.
  TII->loadRegFromStackSlot(MBB, MII, PhysReg, SS, RC);
  // This invalidates Phys.
  Spills.ClobberPhysReg(PhysReg);
  // Remember it's available.
  Spills.addAvailable(SS, PhysReg);
  MaybeDeadStores[SS] = NULL;

  // Unfold current MI.
  SmallVector<MachineInstr*, 4> NewMIs;
  if (!TII->unfoldMemoryOperand(MF, &MI, VirtReg, false, false, NewMIs))
    assert(0 && "Unable unfold the load / store folding instruction!");
  assert(NewMIs.size() == 1);
  AssignPhysToVirtReg(NewMIs[0], VirtReg, PhysReg);
  VRM.transferRestorePts(&MI, NewMIs[0]);
  MII = MBB.insert(MII, NewMIs[0]);
  InvalidateKills(MI, RegKills, KillOps);
  VRM.RemoveMachineInstrFromMaps(&MI);
  MBB.erase(&MI);
  ++NumModRefUnfold;

  // Unfold next instructions that fold the same SS.
  do {
    MachineInstr &NextMI = *NextMII;
    NextMII = next(NextMII);
    NewMIs.clear();
    if (!TII->unfoldMemoryOperand(MF, &NextMI, VirtReg, false, false, NewMIs))
      assert(0 && "Unable unfold the load / store folding instruction!");
    assert(NewMIs.size() == 1);
    AssignPhysToVirtReg(NewMIs[0], VirtReg, PhysReg);
    VRM.transferRestorePts(&NextMI, NewMIs[0]);
    MBB.insert(NextMII, NewMIs[0]);
    InvalidateKills(NextMI, RegKills, KillOps);
    VRM.RemoveMachineInstrFromMaps(&NextMI);
    MBB.erase(&NextMI);
    ++NumModRefUnfold;
  } while (FoldsStackSlotModRef(*NextMII, SS, PhysReg, TII, TRI, VRM));

  // Store the value back into SS.
  TII->storeRegToStackSlot(MBB, NextMII, PhysReg, true, SS, RC);
  MachineInstr *StoreMI = prior(NextMII);
  VRM.addSpillSlotUse(SS, StoreMI);
  VRM.virtFolded(VirtReg, StoreMI, VirtRegMap::isMod);

  return true;
}

/// OptimizeByUnfold - Turn a store folding instruction into a load folding
/// instruction. e.g.
///     xorl  %edi, %eax
///     movl  %eax, -32(%ebp)
///     movl  -36(%ebp), %eax
///     orl   %eax, -32(%ebp)
/// ==>
///     xorl  %edi, %eax
///     orl   -36(%ebp), %eax
///     mov   %eax, -32(%ebp)
/// This enables unfolding optimization for a subsequent instruction which will
/// also eliminate the newly introduced store instruction.
bool LocalSpiller::OptimizeByUnfold(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator &MII,
                                    std::vector<MachineInstr*> &MaybeDeadStores,
                                    AvailableSpills &Spills,
                                    BitVector &RegKills,
                                    std::vector<MachineOperand*> &KillOps,
                                    VirtRegMap &VRM) {
  MachineFunction &MF = *MBB.getParent();
  MachineInstr &MI = *MII;
  unsigned UnfoldedOpc = 0;
  unsigned UnfoldPR = 0;
  unsigned UnfoldVR = 0;
  int FoldedSS = VirtRegMap::NO_STACK_SLOT;
  VirtRegMap::MI2VirtMapTy::const_iterator I, End;
  for (tie(I, End) = VRM.getFoldedVirts(&MI); I != End; ) {
    // Only transform a MI that folds a single register.
    if (UnfoldedOpc)
      return false;
    UnfoldVR = I->second.first;
    VirtRegMap::ModRef MR = I->second.second;
    // MI2VirtMap be can updated which invalidate the iterator.
    // Increment the iterator first.
    ++I; 
    if (VRM.isAssignedReg(UnfoldVR))
      continue;
    // If this reference is not a use, any previous store is now dead.
    // Otherwise, the store to this stack slot is not dead anymore.
    FoldedSS = VRM.getStackSlot(UnfoldVR);
    MachineInstr* DeadStore = MaybeDeadStores[FoldedSS];
    if (DeadStore && (MR & VirtRegMap::isModRef)) {
      unsigned PhysReg = Spills.getSpillSlotOrReMatPhysReg(FoldedSS);
      if (!PhysReg || !DeadStore->readsRegister(PhysReg))
        continue;
      UnfoldPR = PhysReg;
      UnfoldedOpc = TII->getOpcodeAfterMemoryUnfold(MI.getOpcode(),
                                                    false, true);
    }
  }

  if (!UnfoldedOpc) {
    if (!UnfoldVR)
      return false;

    // Look for other unfolding opportunities.
    return OptimizeByUnfold2(UnfoldVR, FoldedSS, MBB, MII,
                             MaybeDeadStores, Spills, RegKills, KillOps, VRM);
  }

  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    MachineOperand &MO = MI.getOperand(i);
    if (!MO.isReg() || MO.getReg() == 0 || !MO.isUse())
      continue;
    unsigned VirtReg = MO.getReg();
    if (TargetRegisterInfo::isPhysicalRegister(VirtReg) || MO.getSubReg())
      continue;
    if (VRM.isAssignedReg(VirtReg)) {
      unsigned PhysReg = VRM.getPhys(VirtReg);
      if (PhysReg && TRI->regsOverlap(PhysReg, UnfoldPR))
        return false;
    } else if (VRM.isReMaterialized(VirtReg))
      continue;
    int SS = VRM.getStackSlot(VirtReg);
    unsigned PhysReg = Spills.getSpillSlotOrReMatPhysReg(SS);
    if (PhysReg) {
      if (TRI->regsOverlap(PhysReg, UnfoldPR))
        return false;
      continue;
    }
    if (VRM.hasPhys(VirtReg)) {
      PhysReg = VRM.getPhys(VirtReg);
      if (!TRI->regsOverlap(PhysReg, UnfoldPR))
        continue;
    }

    // Ok, we'll need to reload the value into a register which makes
    // it impossible to perform the store unfolding optimization later.
    // Let's see if it is possible to fold the load if the store is
    // unfolded. This allows us to perform the store unfolding
    // optimization.
    SmallVector<MachineInstr*, 4> NewMIs;
    if (TII->unfoldMemoryOperand(MF, &MI, UnfoldVR, false, false, NewMIs)) {
      assert(NewMIs.size() == 1);
      MachineInstr *NewMI = NewMIs.back();
      NewMIs.clear();
      int Idx = NewMI->findRegisterUseOperandIdx(VirtReg, false);
      assert(Idx != -1);
      SmallVector<unsigned, 1> Ops;
      Ops.push_back(Idx);
      MachineInstr *FoldedMI = TII->foldMemoryOperand(MF, NewMI, Ops, SS);
      if (FoldedMI) {
        VRM.addSpillSlotUse(SS, FoldedMI);
        if (!VRM.hasPhys(UnfoldVR))
          VRM.assignVirt2Phys(UnfoldVR, UnfoldPR);
        VRM.virtFolded(VirtReg, FoldedMI, VirtRegMap::isRef);
        MII = MBB.insert(MII, FoldedMI);
        InvalidateKills(MI, RegKills, KillOps);
        VRM.RemoveMachineInstrFromMaps(&MI);
        MBB.erase(&MI);
        MF.DeleteMachineInstr(NewMI);
        return true;
      }
      MF.DeleteMachineInstr(NewMI);
    }
  }

  return false;
}

/// CommuteToFoldReload -
/// Look for
/// r1 = load fi#1
/// r1 = op r1, r2<kill>
/// store r1, fi#1
///
/// If op is commutable and r2 is killed, then we can xform these to
/// r2 = op r2, fi#1
/// store r2, fi#1
bool LocalSpiller::CommuteToFoldReload(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator &MII,
                                    unsigned VirtReg, unsigned SrcReg, int SS,
                                    AvailableSpills &Spills,
                                    BitVector &RegKills,
                                    std::vector<MachineOperand*> &KillOps,
                                    const TargetRegisterInfo *TRI,
                                    VirtRegMap &VRM) {
  if (MII == MBB.begin() || !MII->killsRegister(SrcReg))
    return false;

  MachineFunction &MF = *MBB.getParent();
  MachineInstr &MI = *MII;
  MachineBasicBlock::iterator DefMII = prior(MII);
  MachineInstr *DefMI = DefMII;
  const TargetInstrDesc &TID = DefMI->getDesc();
  unsigned NewDstIdx;
  if (DefMII != MBB.begin() &&
      TID.isCommutable() &&
      TII->CommuteChangesDestination(DefMI, NewDstIdx)) {
    MachineOperand &NewDstMO = DefMI->getOperand(NewDstIdx);
    unsigned NewReg = NewDstMO.getReg();
    if (!NewDstMO.isKill() || TRI->regsOverlap(NewReg, SrcReg))
      return false;
    MachineInstr *ReloadMI = prior(DefMII);
    int FrameIdx;
    unsigned DestReg = TII->isLoadFromStackSlot(ReloadMI, FrameIdx);
    if (DestReg != SrcReg || FrameIdx != SS)
      return false;
    int UseIdx = DefMI->findRegisterUseOperandIdx(DestReg, false);
    if (UseIdx == -1)
      return false;
    unsigned DefIdx;
    if (!MI.isRegTiedToDefOperand(UseIdx, &DefIdx))
      return false;
    assert(DefMI->getOperand(DefIdx).isReg() &&
           DefMI->getOperand(DefIdx).getReg() == SrcReg);

    // Now commute def instruction.
    MachineInstr *CommutedMI = TII->commuteInstruction(DefMI, true);
    if (!CommutedMI)
      return false;
    SmallVector<unsigned, 1> Ops;
    Ops.push_back(NewDstIdx);
    MachineInstr *FoldedMI = TII->foldMemoryOperand(MF, CommutedMI, Ops, SS);
    // Not needed since foldMemoryOperand returns new MI.
    MF.DeleteMachineInstr(CommutedMI);
    if (!FoldedMI)
      return false;

    VRM.addSpillSlotUse(SS, FoldedMI);
    VRM.virtFolded(VirtReg, FoldedMI, VirtRegMap::isRef);
    // Insert new def MI and spill MI.
    const TargetRegisterClass* RC = RegInfo->getRegClass(VirtReg);
    TII->storeRegToStackSlot(MBB, &MI, NewReg, true, SS, RC);
    MII = prior(MII);
    MachineInstr *StoreMI = MII;
    VRM.addSpillSlotUse(SS, StoreMI);
    VRM.virtFolded(VirtReg, StoreMI, VirtRegMap::isMod);
    MII = MBB.insert(MII, FoldedMI);  // Update MII to backtrack.

    // Delete all 3 old instructions.
    InvalidateKills(*ReloadMI, RegKills, KillOps);
    VRM.RemoveMachineInstrFromMaps(ReloadMI);
    MBB.erase(ReloadMI);
    InvalidateKills(*DefMI, RegKills, KillOps);
    VRM.RemoveMachineInstrFromMaps(DefMI);
    MBB.erase(DefMI);
    InvalidateKills(MI, RegKills, KillOps);
    VRM.RemoveMachineInstrFromMaps(&MI);
    MBB.erase(&MI);

    // If NewReg was previously holding value of some SS, it's now clobbered.
    // This has to be done now because it's a physical register. When this
    // instruction is re-visited, it's ignored.
    Spills.ClobberPhysReg(NewReg);

    ++NumCommutes;
    return true;
  }

  return false;
}

/// SpillRegToStackSlot - Spill a register to a specified stack slot. Check if
/// the last store to the same slot is now dead. If so, remove the last store.
void LocalSpiller::SpillRegToStackSlot(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator &MII,
                                  int Idx, unsigned PhysReg, int StackSlot,
                                  const TargetRegisterClass *RC,
                                  bool isAvailable, MachineInstr *&LastStore,
                                  AvailableSpills &Spills,
                                  SmallSet<MachineInstr*, 4> &ReMatDefs,
                                  BitVector &RegKills,
                                  std::vector<MachineOperand*> &KillOps,
                                  VirtRegMap &VRM) {
  TII->storeRegToStackSlot(MBB, next(MII), PhysReg, true, StackSlot, RC);
  MachineInstr *StoreMI = next(MII);
  VRM.addSpillSlotUse(StackSlot, StoreMI);
  DOUT << "Store:\t" << *StoreMI;

  // If there is a dead store to this stack slot, nuke it now.
  if (LastStore) {
    DOUT << "Removed dead store:\t" << *LastStore;
    ++NumDSE;
    SmallVector<unsigned, 2> KillRegs;
    InvalidateKills(*LastStore, RegKills, KillOps, &KillRegs);
    MachineBasicBlock::iterator PrevMII = LastStore;
    bool CheckDef = PrevMII != MBB.begin();
    if (CheckDef)
      --PrevMII;
    VRM.RemoveMachineInstrFromMaps(LastStore);
    MBB.erase(LastStore);
    if (CheckDef) {
      // Look at defs of killed registers on the store. Mark the defs
      // as dead since the store has been deleted and they aren't
      // being reused.
      for (unsigned j = 0, ee = KillRegs.size(); j != ee; ++j) {
        bool HasOtherDef = false;
        if (InvalidateRegDef(PrevMII, *MII, KillRegs[j], HasOtherDef)) {
          MachineInstr *DeadDef = PrevMII;
          if (ReMatDefs.count(DeadDef) && !HasOtherDef) {
            // FIXME: This assumes a remat def does not have side
            // effects.
            VRM.RemoveMachineInstrFromMaps(DeadDef);
            MBB.erase(DeadDef);
            ++NumDRM;
          }
        }
      }
    }
  }

  LastStore = next(MII);

  // If the stack slot value was previously available in some other
  // register, change it now.  Otherwise, make the register available,
  // in PhysReg.
  Spills.ModifyStackSlotOrReMat(StackSlot);
  Spills.ClobberPhysReg(PhysReg);
  Spills.addAvailable(StackSlot, PhysReg, isAvailable);
  ++NumStores;
}

/// TransferDeadness - A identity copy definition is dead and it's being
/// removed. Find the last def or use and mark it as dead / kill.
void LocalSpiller::TransferDeadness(MachineBasicBlock *MBB, unsigned CurDist,
                                    unsigned Reg, BitVector &RegKills,
                                    std::vector<MachineOperand*> &KillOps) {
  int LastUDDist = -1;
  MachineInstr *LastUDMI = NULL;
  for (MachineRegisterInfo::reg_iterator RI = RegInfo->reg_begin(Reg),
         RE = RegInfo->reg_end(); RI != RE; ++RI) {
    MachineInstr *UDMI = &*RI;
    if (UDMI->getParent() != MBB)
      continue;
    DenseMap<MachineInstr*, unsigned>::iterator DI = DistanceMap.find(UDMI);
    if (DI == DistanceMap.end() || DI->second > CurDist)
      continue;
    if ((int)DI->second < LastUDDist)
      continue;
    LastUDDist = DI->second;
    LastUDMI = UDMI;
  }

  if (LastUDMI) {
    MachineOperand *LastUD = NULL;
    for (unsigned i = 0, e = LastUDMI->getNumOperands(); i != e; ++i) {
      MachineOperand &MO = LastUDMI->getOperand(i);
      if (!MO.isReg() || MO.getReg() != Reg)
        continue;
      if (!LastUD || (LastUD->isUse() && MO.isDef()))
        LastUD = &MO;
      if (LastUDMI->isRegTiedToDefOperand(i))
        return;
    }
    if (LastUD->isDef())
      LastUD->setIsDead();
    else {
      LastUD->setIsKill();
      RegKills.set(Reg);
      KillOps[Reg] = LastUD;
    }
  }
}

/// rewriteMBB - Keep track of which spills are available even after the
/// register allocator is done with them.  If possible, avid reloading vregs.
void LocalSpiller::RewriteMBB(MachineBasicBlock &MBB, VirtRegMap &VRM,
                              LiveIntervals *LIs,
                              AvailableSpills &Spills, BitVector &RegKills,
                              std::vector<MachineOperand*> &KillOps) {
  DOUT << "\n**** Local spiller rewriting MBB '"
       << MBB.getBasicBlock()->getName() << "':\n";

  MachineFunction &MF = *MBB.getParent();
  
  // MaybeDeadStores - When we need to write a value back into a stack slot,
  // keep track of the inserted store.  If the stack slot value is never read
  // (because the value was used from some available register, for example), and
  // subsequently stored to, the original store is dead.  This map keeps track
  // of inserted stores that are not used.  If we see a subsequent store to the
  // same stack slot, the original store is deleted.
  std::vector<MachineInstr*> MaybeDeadStores;
  MaybeDeadStores.resize(MF.getFrameInfo()->getObjectIndexEnd(), NULL);

  // ReMatDefs - These are rematerializable def MIs which are not deleted.
  SmallSet<MachineInstr*, 4> ReMatDefs;

  // Clear kill info.
  SmallSet<unsigned, 2> KilledMIRegs;
  RegKills.reset();
  KillOps.clear();
  KillOps.resize(TRI->getNumRegs(), NULL);

  unsigned Dist = 0;
  DistanceMap.clear();
  for (MachineBasicBlock::iterator MII = MBB.begin(), E = MBB.end();
       MII != E; ) {
    MachineBasicBlock::iterator NextMII = next(MII);

    VirtRegMap::MI2VirtMapTy::const_iterator I, End;
    bool Erased = false;
    bool BackTracked = false;
    if (OptimizeByUnfold(MBB, MII,
                         MaybeDeadStores, Spills, RegKills, KillOps, VRM))
      NextMII = next(MII);

    MachineInstr &MI = *MII;

    if (VRM.hasEmergencySpills(&MI)) {
      // Spill physical register(s) in the rare case the allocator has run out
      // of registers to allocate.
      SmallSet<int, 4> UsedSS;
      std::vector<unsigned> &EmSpills = VRM.getEmergencySpills(&MI);
      for (unsigned i = 0, e = EmSpills.size(); i != e; ++i) {
        unsigned PhysReg = EmSpills[i];
        const TargetRegisterClass *RC =
          TRI->getPhysicalRegisterRegClass(PhysReg);
        assert(RC && "Unable to determine register class!");
        int SS = VRM.getEmergencySpillSlot(RC);
        if (UsedSS.count(SS))
          assert(0 && "Need to spill more than one physical registers!");
        UsedSS.insert(SS);
        TII->storeRegToStackSlot(MBB, MII, PhysReg, true, SS, RC);
        MachineInstr *StoreMI = prior(MII);
        VRM.addSpillSlotUse(SS, StoreMI);
        TII->loadRegFromStackSlot(MBB, next(MII), PhysReg, SS, RC);
        MachineInstr *LoadMI = next(MII);
        VRM.addSpillSlotUse(SS, LoadMI);
        ++NumPSpills;
      }
      NextMII = next(MII);
    }

    // Insert restores here if asked to.
    if (VRM.isRestorePt(&MI)) {
      std::vector<unsigned> &RestoreRegs = VRM.getRestorePtRestores(&MI);
      for (unsigned i = 0, e = RestoreRegs.size(); i != e; ++i) {
        unsigned VirtReg = RestoreRegs[e-i-1];  // Reverse order.
        if (!VRM.getPreSplitReg(VirtReg))
          continue; // Split interval spilled again.
        unsigned Phys = VRM.getPhys(VirtReg);
        RegInfo->setPhysRegUsed(Phys);

        // Check if the value being restored if available. If so, it must be
        // from a predecessor BB that fallthrough into this BB. We do not
        // expect:
        // BB1:
        // r1 = load fi#1
        // ...
        //    = r1<kill>
        // ... # r1 not clobbered
        // ...
        //    = load fi#1
        bool DoReMat = VRM.isReMaterialized(VirtReg);
        int SSorRMId = DoReMat
          ? VRM.getReMatId(VirtReg) : VRM.getStackSlot(VirtReg);
        const TargetRegisterClass* RC = RegInfo->getRegClass(VirtReg);
        unsigned InReg = Spills.getSpillSlotOrReMatPhysReg(SSorRMId);
        if (InReg == Phys) {
          // If the value is already available in the expected register, save
          // a reload / remat.
          if (SSorRMId)
            DOUT << "Reusing RM#" << SSorRMId-VirtRegMap::MAX_STACK_SLOT-1;
          else
            DOUT << "Reusing SS#" << SSorRMId;
          DOUT << " from physreg "
               << TRI->getName(InReg) << " for vreg"
               << VirtReg <<" instead of reloading into physreg "
               << TRI->getName(Phys) << "\n";
          ++NumOmitted;
          continue;
        } else if (InReg && InReg != Phys) {
          if (SSorRMId)
            DOUT << "Reusing RM#" << SSorRMId-VirtRegMap::MAX_STACK_SLOT-1;
          else
            DOUT << "Reusing SS#" << SSorRMId;
          DOUT << " from physreg "
               << TRI->getName(InReg) << " for vreg"
               << VirtReg <<" by copying it into physreg "
               << TRI->getName(Phys) << "\n";

          // If the reloaded / remat value is available in another register,
          // copy it to the desired register.
          TII->copyRegToReg(MBB, &MI, Phys, InReg, RC, RC);

          // This invalidates Phys.
          Spills.ClobberPhysReg(Phys);
          // Remember it's available.
          Spills.addAvailable(SSorRMId, Phys);

          // Mark is killed.
          MachineInstr *CopyMI = prior(MII);
          MachineOperand *KillOpnd = CopyMI->findRegisterUseOperand(InReg);
          KillOpnd->setIsKill();
          UpdateKills(*CopyMI, RegKills, KillOps, TRI);

          DOUT << '\t' << *CopyMI;
          ++NumCopified;
          continue;
        }

        if (VRM.isReMaterialized(VirtReg)) {
          ReMaterialize(MBB, MII, Phys, VirtReg, TII, TRI, VRM);
        } else {
          const TargetRegisterClass* RC = RegInfo->getRegClass(VirtReg);
          TII->loadRegFromStackSlot(MBB, &MI, Phys, SSorRMId, RC);
          MachineInstr *LoadMI = prior(MII);
          VRM.addSpillSlotUse(SSorRMId, LoadMI);
          ++NumLoads;
        }

        // This invalidates Phys.
        Spills.ClobberPhysReg(Phys);
        // Remember it's available.
        Spills.addAvailable(SSorRMId, Phys);

        UpdateKills(*prior(MII), RegKills, KillOps, TRI);
        DOUT << '\t' << *prior(MII);
      }
    }

    // Insert spills here if asked to.
    if (VRM.isSpillPt(&MI)) {
      std::vector<std::pair<unsigned,bool> > &SpillRegs =
        VRM.getSpillPtSpills(&MI);
      for (unsigned i = 0, e = SpillRegs.size(); i != e; ++i) {
        unsigned VirtReg = SpillRegs[i].first;
        bool isKill = SpillRegs[i].second;
        if (!VRM.getPreSplitReg(VirtReg))
          continue; // Split interval spilled again.
        const TargetRegisterClass *RC = RegInfo->getRegClass(VirtReg);
        unsigned Phys = VRM.getPhys(VirtReg);
        int StackSlot = VRM.getStackSlot(VirtReg);
        TII->storeRegToStackSlot(MBB, next(MII), Phys, isKill, StackSlot, RC);
        MachineInstr *StoreMI = next(MII);
        VRM.addSpillSlotUse(StackSlot, StoreMI);
        DOUT << "Store:\t" << *StoreMI;
        VRM.virtFolded(VirtReg, StoreMI, VirtRegMap::isMod);
      }
      NextMII = next(MII);
    }

    /// ReusedOperands - Keep track of operand reuse in case we need to undo
    /// reuse.
    ReuseInfo ReusedOperands(MI, TRI);
    SmallVector<unsigned, 4> VirtUseOps;
    for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
      MachineOperand &MO = MI.getOperand(i);
      if (!MO.isReg() || MO.getReg() == 0)
        continue;   // Ignore non-register operands.
      
      unsigned VirtReg = MO.getReg();
      if (TargetRegisterInfo::isPhysicalRegister(VirtReg)) {
        // Ignore physregs for spilling, but remember that it is used by this
        // function.
        RegInfo->setPhysRegUsed(VirtReg);
        continue;
      }

      // We want to process implicit virtual register uses first.
      if (MO.isImplicit())
        // If the virtual register is implicitly defined, emit a implicit_def
        // before so scavenger knows it's "defined".
        VirtUseOps.insert(VirtUseOps.begin(), i);
      else
        VirtUseOps.push_back(i);
    }

    // Process all of the spilled uses and all non spilled reg references.
    SmallVector<int, 2> PotentialDeadStoreSlots;
    KilledMIRegs.clear();
    for (unsigned j = 0, e = VirtUseOps.size(); j != e; ++j) {
      unsigned i = VirtUseOps[j];
      MachineOperand &MO = MI.getOperand(i);
      unsigned VirtReg = MO.getReg();
      assert(TargetRegisterInfo::isVirtualRegister(VirtReg) &&
             "Not a virtual register?");

      unsigned SubIdx = MO.getSubReg();
      if (VRM.isAssignedReg(VirtReg)) {
        // This virtual register was assigned a physreg!
        unsigned Phys = VRM.getPhys(VirtReg);
        RegInfo->setPhysRegUsed(Phys);
        if (MO.isDef())
          ReusedOperands.markClobbered(Phys);
        unsigned RReg = SubIdx ? TRI->getSubReg(Phys, SubIdx) : Phys;
        MI.getOperand(i).setReg(RReg);
        MI.getOperand(i).setSubReg(0);
        if (VRM.isImplicitlyDefined(VirtReg))
          BuildMI(MBB, &MI, MI.getDebugLoc(),
                  TII->get(TargetInstrInfo::IMPLICIT_DEF), RReg);
        continue;
      }
      
      // This virtual register is now known to be a spilled value.
      if (!MO.isUse())
        continue;  // Handle defs in the loop below (handle use&def here though)

      bool AvoidReload = false;
      if (LIs->hasInterval(VirtReg)) {
        LiveInterval &LI = LIs->getInterval(VirtReg);
        if (!LI.liveAt(LIs->getUseIndex(LI.beginNumber())))
          // Must be defined by an implicit def. It should not be spilled. Note,
          // this is for correctness reason. e.g.
          // 8   %reg1024<def> = IMPLICIT_DEF
          // 12  %reg1024<def> = INSERT_SUBREG %reg1024<kill>, %reg1025, 2
          // The live range [12, 14) are not part of the r1024 live interval since
          // it's defined by an implicit def. It will not conflicts with live
          // interval of r1025. Now suppose both registers are spilled, you can
          // easily see a situation where both registers are reloaded before
          // the INSERT_SUBREG and both target registers that would overlap.
          AvoidReload = true;
      }

      bool DoReMat = VRM.isReMaterialized(VirtReg);
      int SSorRMId = DoReMat
        ? VRM.getReMatId(VirtReg) : VRM.getStackSlot(VirtReg);
      int ReuseSlot = SSorRMId;

      // Check to see if this stack slot is available.
      unsigned PhysReg = Spills.getSpillSlotOrReMatPhysReg(SSorRMId);

      // If this is a sub-register use, make sure the reuse register is in the
      // right register class. For example, for x86 not all of the 32-bit
      // registers have accessible sub-registers.
      // Similarly so for EXTRACT_SUBREG. Consider this:
      // EDI = op
      // MOV32_mr fi#1, EDI
      // ...
      //       = EXTRACT_SUBREG fi#1
      // fi#1 is available in EDI, but it cannot be reused because it's not in
      // the right register file.
      if (PhysReg && !AvoidReload &&
          (SubIdx || MI.getOpcode() == TargetInstrInfo::EXTRACT_SUBREG)) {
        const TargetRegisterClass* RC = RegInfo->getRegClass(VirtReg);
        if (!RC->contains(PhysReg))
          PhysReg = 0;
      }

      if (PhysReg && !AvoidReload) {
        // This spilled operand might be part of a two-address operand.  If this
        // is the case, then changing it will necessarily require changing the 
        // def part of the instruction as well.  However, in some cases, we
        // aren't allowed to modify the reused register.  If none of these cases
        // apply, reuse it.
        bool CanReuse = true;
        bool isTied = MI.isRegTiedToDefOperand(i);
        if (isTied) {
          // Okay, we have a two address operand.  We can reuse this physreg as
          // long as we are allowed to clobber the value and there isn't an
          // earlier def that has already clobbered the physreg.
          CanReuse = !ReusedOperands.isClobbered(PhysReg) &&
            Spills.canClobberPhysReg(PhysReg);
        }
        
        if (CanReuse) {
          // If this stack slot value is already available, reuse it!
          if (ReuseSlot > VirtRegMap::MAX_STACK_SLOT)
            DOUT << "Reusing RM#" << ReuseSlot-VirtRegMap::MAX_STACK_SLOT-1;
          else
            DOUT << "Reusing SS#" << ReuseSlot;
          DOUT << " from physreg "
               << TRI->getName(PhysReg) << " for vreg"
               << VirtReg <<" instead of reloading into physreg "
               << TRI->getName(VRM.getPhys(VirtReg)) << "\n";
          unsigned RReg = SubIdx ? TRI->getSubReg(PhysReg, SubIdx) : PhysReg;
          MI.getOperand(i).setReg(RReg);
          MI.getOperand(i).setSubReg(0);

          // The only technical detail we have is that we don't know that
          // PhysReg won't be clobbered by a reloaded stack slot that occurs
          // later in the instruction.  In particular, consider 'op V1, V2'.
          // If V1 is available in physreg R0, we would choose to reuse it
          // here, instead of reloading it into the register the allocator
          // indicated (say R1).  However, V2 might have to be reloaded
          // later, and it might indicate that it needs to live in R0.  When
          // this occurs, we need to have information available that
          // indicates it is safe to use R1 for the reload instead of R0.
          //
          // To further complicate matters, we might conflict with an alias,
          // or R0 and R1 might not be compatible with each other.  In this
          // case, we actually insert a reload for V1 in R1, ensuring that
          // we can get at R0 or its alias.
          ReusedOperands.addReuse(i, ReuseSlot, PhysReg,
                                  VRM.getPhys(VirtReg), VirtReg);
          if (isTied)
            // Only mark it clobbered if this is a use&def operand.
            ReusedOperands.markClobbered(PhysReg);
          ++NumReused;

          if (MI.getOperand(i).isKill() &&
              ReuseSlot <= VirtRegMap::MAX_STACK_SLOT) {

            // The store of this spilled value is potentially dead, but we
            // won't know for certain until we've confirmed that the re-use
            // above is valid, which means waiting until the other operands
            // are processed. For now we just track the spill slot, we'll
            // remove it after the other operands are processed if valid.

            PotentialDeadStoreSlots.push_back(ReuseSlot);
          }

          // Mark is isKill if it's there no other uses of the same virtual
          // register and it's not a two-address operand. IsKill will be
          // unset if reg is reused.
          if (!isTied && KilledMIRegs.count(VirtReg) == 0) {
            MI.getOperand(i).setIsKill();
            KilledMIRegs.insert(VirtReg);
          }

          continue;
        }  // CanReuse
        
        // Otherwise we have a situation where we have a two-address instruction
        // whose mod/ref operand needs to be reloaded.  This reload is already
        // available in some register "PhysReg", but if we used PhysReg as the
        // operand to our 2-addr instruction, the instruction would modify
        // PhysReg.  This isn't cool if something later uses PhysReg and expects
        // to get its initial value.
        //
        // To avoid this problem, and to avoid doing a load right after a store,
        // we emit a copy from PhysReg into the designated register for this
        // operand.
        unsigned DesignatedReg = VRM.getPhys(VirtReg);
        assert(DesignatedReg && "Must map virtreg to physreg!");

        // Note that, if we reused a register for a previous operand, the
        // register we want to reload into might not actually be
        // available.  If this occurs, use the register indicated by the
        // reuser.
        if (ReusedOperands.hasReuses())
          DesignatedReg = ReusedOperands.GetRegForReload(DesignatedReg, &MI, 
                               Spills, MaybeDeadStores, RegKills, KillOps, VRM);
        
        // If the mapped designated register is actually the physreg we have
        // incoming, we don't need to inserted a dead copy.
        if (DesignatedReg == PhysReg) {
          // If this stack slot value is already available, reuse it!
          if (ReuseSlot > VirtRegMap::MAX_STACK_SLOT)
            DOUT << "Reusing RM#" << ReuseSlot-VirtRegMap::MAX_STACK_SLOT-1;
          else
            DOUT << "Reusing SS#" << ReuseSlot;
          DOUT << " from physreg " << TRI->getName(PhysReg)
               << " for vreg" << VirtReg
               << " instead of reloading into same physreg.\n";
          unsigned RReg = SubIdx ? TRI->getSubReg(PhysReg, SubIdx) : PhysReg;
          MI.getOperand(i).setReg(RReg);
          MI.getOperand(i).setSubReg(0);
          ReusedOperands.markClobbered(RReg);
          ++NumReused;
          continue;
        }
        
        const TargetRegisterClass* RC = RegInfo->getRegClass(VirtReg);
        RegInfo->setPhysRegUsed(DesignatedReg);
        ReusedOperands.markClobbered(DesignatedReg);
        TII->copyRegToReg(MBB, &MI, DesignatedReg, PhysReg, RC, RC);

        MachineInstr *CopyMI = prior(MII);
        UpdateKills(*CopyMI, RegKills, KillOps, TRI);

        // This invalidates DesignatedReg.
        Spills.ClobberPhysReg(DesignatedReg);
        
        Spills.addAvailable(ReuseSlot, DesignatedReg);
        unsigned RReg =
          SubIdx ? TRI->getSubReg(DesignatedReg, SubIdx) : DesignatedReg;
        MI.getOperand(i).setReg(RReg);
        MI.getOperand(i).setSubReg(0);
        DOUT << '\t' << *prior(MII);
        ++NumReused;
        continue;
      } // if (PhysReg)
      
      // Otherwise, reload it and remember that we have it.
      PhysReg = VRM.getPhys(VirtReg);
      assert(PhysReg && "Must map virtreg to physreg!");

      // Note that, if we reused a register for a previous operand, the
      // register we want to reload into might not actually be
      // available.  If this occurs, use the register indicated by the
      // reuser.
      if (ReusedOperands.hasReuses())
        PhysReg = ReusedOperands.GetRegForReload(PhysReg, &MI, 
                               Spills, MaybeDeadStores, RegKills, KillOps, VRM);
      
      RegInfo->setPhysRegUsed(PhysReg);
      ReusedOperands.markClobbered(PhysReg);
      if (AvoidReload)
        ++NumAvoided;
      else {
        if (DoReMat) {
          ReMaterialize(MBB, MII, PhysReg, VirtReg, TII, TRI, VRM);
        } else {
          const TargetRegisterClass* RC = RegInfo->getRegClass(VirtReg);
          TII->loadRegFromStackSlot(MBB, &MI, PhysReg, SSorRMId, RC);
          MachineInstr *LoadMI = prior(MII);
          VRM.addSpillSlotUse(SSorRMId, LoadMI);
          ++NumLoads;
        }
        // This invalidates PhysReg.
        Spills.ClobberPhysReg(PhysReg);

        // Any stores to this stack slot are not dead anymore.
        if (!DoReMat)
          MaybeDeadStores[SSorRMId] = NULL;
        Spills.addAvailable(SSorRMId, PhysReg);
        // Assumes this is the last use. IsKill will be unset if reg is reused
        // unless it's a two-address operand.
        if (!MI.isRegTiedToDefOperand(i) &&
            KilledMIRegs.count(VirtReg) == 0) {
          MI.getOperand(i).setIsKill();
          KilledMIRegs.insert(VirtReg);
        }

        UpdateKills(*prior(MII), RegKills, KillOps, TRI);
        DOUT << '\t' << *prior(MII);
      }
      unsigned RReg = SubIdx ? TRI->getSubReg(PhysReg, SubIdx) : PhysReg;
      MI.getOperand(i).setReg(RReg);
      MI.getOperand(i).setSubReg(0);
    }

    // Ok - now we can remove stores that have been confirmed dead.
    for (unsigned j = 0, e = PotentialDeadStoreSlots.size(); j != e; ++j) {
      // This was the last use and the spilled value is still available
      // for reuse. That means the spill was unnecessary!
      int PDSSlot = PotentialDeadStoreSlots[j];
      MachineInstr* DeadStore = MaybeDeadStores[PDSSlot];
      if (DeadStore) {
        DOUT << "Removed dead store:\t" << *DeadStore;
        InvalidateKills(*DeadStore, RegKills, KillOps);
        VRM.RemoveMachineInstrFromMaps(DeadStore);
        MBB.erase(DeadStore);
        MaybeDeadStores[PDSSlot] = NULL;
        ++NumDSE;
      }
    }


    DOUT << '\t' << MI;


    // If we have folded references to memory operands, make sure we clear all
    // physical registers that may contain the value of the spilled virtual
    // register
    SmallSet<int, 2> FoldedSS;
    for (tie(I, End) = VRM.getFoldedVirts(&MI); I != End; ) {
      unsigned VirtReg = I->second.first;
      VirtRegMap::ModRef MR = I->second.second;
      DOUT << "Folded vreg: " << VirtReg << "  MR: " << MR;

      // MI2VirtMap be can updated which invalidate the iterator.
      // Increment the iterator first.
      ++I;
      int SS = VRM.getStackSlot(VirtReg);
      if (SS == VirtRegMap::NO_STACK_SLOT)
        continue;
      FoldedSS.insert(SS);
      DOUT << " - StackSlot: " << SS << "\n";
      
      // If this folded instruction is just a use, check to see if it's a
      // straight load from the virt reg slot.
      if ((MR & VirtRegMap::isRef) && !(MR & VirtRegMap::isMod)) {
        int FrameIdx;
        unsigned DestReg = TII->isLoadFromStackSlot(&MI, FrameIdx);
        if (DestReg && FrameIdx == SS) {
          // If this spill slot is available, turn it into a copy (or nothing)
          // instead of leaving it as a load!
          if (unsigned InReg = Spills.getSpillSlotOrReMatPhysReg(SS)) {
            DOUT << "Promoted Load To Copy: " << MI;
            if (DestReg != InReg) {
              const TargetRegisterClass *RC = RegInfo->getRegClass(VirtReg);
              TII->copyRegToReg(MBB, &MI, DestReg, InReg, RC, RC);
              MachineOperand *DefMO = MI.findRegisterDefOperand(DestReg);
              unsigned SubIdx = DefMO->getSubReg();
              // Revisit the copy so we make sure to notice the effects of the
              // operation on the destreg (either needing to RA it if it's 
              // virtual or needing to clobber any values if it's physical).
              NextMII = &MI;
              --NextMII;  // backtrack to the copy.
              // Propagate the sub-register index over.
              if (SubIdx) {
                DefMO = NextMII->findRegisterDefOperand(DestReg);
                DefMO->setSubReg(SubIdx);
              }

              // Mark is killed.
              MachineOperand *KillOpnd = NextMII->findRegisterUseOperand(InReg);
              KillOpnd->setIsKill();

              BackTracked = true;
            } else {
              DOUT << "Removing now-noop copy: " << MI;
              // Unset last kill since it's being reused.
              InvalidateKill(InReg, RegKills, KillOps);
              Spills.disallowClobberPhysReg(InReg);
            }

            InvalidateKills(MI, RegKills, KillOps);
            VRM.RemoveMachineInstrFromMaps(&MI);
            MBB.erase(&MI);
            Erased = true;
            goto ProcessNextInst;
          }
        } else {
          unsigned PhysReg = Spills.getSpillSlotOrReMatPhysReg(SS);
          SmallVector<MachineInstr*, 4> NewMIs;
          if (PhysReg &&
              TII->unfoldMemoryOperand(MF, &MI, PhysReg, false, false, NewMIs)) {
            MBB.insert(MII, NewMIs[0]);
            InvalidateKills(MI, RegKills, KillOps);
            VRM.RemoveMachineInstrFromMaps(&MI);
            MBB.erase(&MI);
            Erased = true;
            --NextMII;  // backtrack to the unfolded instruction.
            BackTracked = true;
            goto ProcessNextInst;
          }
        }
      }

      // If this reference is not a use, any previous store is now dead.
      // Otherwise, the store to this stack slot is not dead anymore.
      MachineInstr* DeadStore = MaybeDeadStores[SS];
      if (DeadStore) {
        bool isDead = !(MR & VirtRegMap::isRef);
        MachineInstr *NewStore = NULL;
        if (MR & VirtRegMap::isModRef) {
          unsigned PhysReg = Spills.getSpillSlotOrReMatPhysReg(SS);
          SmallVector<MachineInstr*, 4> NewMIs;
          // We can reuse this physreg as long as we are allowed to clobber
          // the value and there isn't an earlier def that has already clobbered
          // the physreg.
          if (PhysReg &&
              !ReusedOperands.isClobbered(PhysReg) &&
              Spills.canClobberPhysReg(PhysReg) &&
              !TII->isStoreToStackSlot(&MI, SS)) { // Not profitable!
            MachineOperand *KillOpnd =
              DeadStore->findRegisterUseOperand(PhysReg, true);
            // Note, if the store is storing a sub-register, it's possible the
            // super-register is needed below.
            if (KillOpnd && !KillOpnd->getSubReg() &&
                TII->unfoldMemoryOperand(MF, &MI, PhysReg, false, true,NewMIs)){
              MBB.insert(MII, NewMIs[0]);
              NewStore = NewMIs[1];
              MBB.insert(MII, NewStore);
              VRM.addSpillSlotUse(SS, NewStore);
              InvalidateKills(MI, RegKills, KillOps);
              VRM.RemoveMachineInstrFromMaps(&MI);
              MBB.erase(&MI);
              Erased = true;
              --NextMII;
              --NextMII;  // backtrack to the unfolded instruction.
              BackTracked = true;
              isDead = true;
              ++NumSUnfold;
            }
          }
        }

        if (isDead) {  // Previous store is dead.
          // If we get here, the store is dead, nuke it now.
          DOUT << "Removed dead store:\t" << *DeadStore;
          InvalidateKills(*DeadStore, RegKills, KillOps);
          VRM.RemoveMachineInstrFromMaps(DeadStore);
          MBB.erase(DeadStore);
          if (!NewStore)
            ++NumDSE;
        }

        MaybeDeadStores[SS] = NULL;
        if (NewStore) {
          // Treat this store as a spill merged into a copy. That makes the
          // stack slot value available.
          VRM.virtFolded(VirtReg, NewStore, VirtRegMap::isMod);
          goto ProcessNextInst;
        }
      }

      // If the spill slot value is available, and this is a new definition of
      // the value, the value is not available anymore.
      if (MR & VirtRegMap::isMod) {
        // Notice that the value in this stack slot has been modified.
        Spills.ModifyStackSlotOrReMat(SS);
        
        // If this is *just* a mod of the value, check to see if this is just a
        // store to the spill slot (i.e. the spill got merged into the copy). If
        // so, realize that the vreg is available now, and add the store to the
        // MaybeDeadStore info.
        int StackSlot;
        if (!(MR & VirtRegMap::isRef)) {
          if (unsigned SrcReg = TII->isStoreToStackSlot(&MI, StackSlot)) {
            assert(TargetRegisterInfo::isPhysicalRegister(SrcReg) &&
                   "Src hasn't been allocated yet?");

            if (CommuteToFoldReload(MBB, MII, VirtReg, SrcReg, StackSlot,
                                    Spills, RegKills, KillOps, TRI, VRM)) {
              NextMII = next(MII);
              BackTracked = true;
              goto ProcessNextInst;
            }

            // Okay, this is certainly a store of SrcReg to [StackSlot].  Mark
            // this as a potentially dead store in case there is a subsequent
            // store into the stack slot without a read from it.
            MaybeDeadStores[StackSlot] = &MI;

            // If the stack slot value was previously available in some other
            // register, change it now.  Otherwise, make the register
            // available in PhysReg.
            Spills.addAvailable(StackSlot, SrcReg, MI.killsRegister(SrcReg));
          }
        }
      }
    }

    // Process all of the spilled defs.
    for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
      MachineOperand &MO = MI.getOperand(i);
      if (!(MO.isReg() && MO.getReg() && MO.isDef()))
        continue;

      unsigned VirtReg = MO.getReg();
      if (!TargetRegisterInfo::isVirtualRegister(VirtReg)) {
        // Check to see if this is a noop copy.  If so, eliminate the
        // instruction before considering the dest reg to be changed.
        unsigned Src, Dst, SrcSR, DstSR;
        if (TII->isMoveInstr(MI, Src, Dst, SrcSR, DstSR) && Src == Dst) {
          ++NumDCE;
          DOUT << "Removing now-noop copy: " << MI;
          SmallVector<unsigned, 2> KillRegs;
          InvalidateKills(MI, RegKills, KillOps, &KillRegs);
          if (MO.isDead() && !KillRegs.empty()) {
            // Source register or an implicit super/sub-register use is killed.
            assert(KillRegs[0] == Dst ||
                   TRI->isSubRegister(KillRegs[0], Dst) ||
                   TRI->isSuperRegister(KillRegs[0], Dst));
            // Last def is now dead.
            TransferDeadness(&MBB, Dist, Src, RegKills, KillOps);
          }
          VRM.RemoveMachineInstrFromMaps(&MI);
          MBB.erase(&MI);
          Erased = true;
          Spills.disallowClobberPhysReg(VirtReg);
          goto ProcessNextInst;
        }
          
        // If it's not a no-op copy, it clobbers the value in the destreg.
        Spills.ClobberPhysReg(VirtReg);
        ReusedOperands.markClobbered(VirtReg);
 
        // Check to see if this instruction is a load from a stack slot into
        // a register.  If so, this provides the stack slot value in the reg.
        int FrameIdx;
        if (unsigned DestReg = TII->isLoadFromStackSlot(&MI, FrameIdx)) {
          assert(DestReg == VirtReg && "Unknown load situation!");

          // If it is a folded reference, then it's not safe to clobber.
          bool Folded = FoldedSS.count(FrameIdx);
          // Otherwise, if it wasn't available, remember that it is now!
          Spills.addAvailable(FrameIdx, DestReg, !Folded);
          goto ProcessNextInst;
        }
            
        continue;
      }

      unsigned SubIdx = MO.getSubReg();
      bool DoReMat = VRM.isReMaterialized(VirtReg);
      if (DoReMat)
        ReMatDefs.insert(&MI);

      // The only vregs left are stack slot definitions.
      int StackSlot = VRM.getStackSlot(VirtReg);
      const TargetRegisterClass *RC = RegInfo->getRegClass(VirtReg);

      // If this def is part of a two-address operand, make sure to execute
      // the store from the correct physical register.
      unsigned PhysReg;
      unsigned TiedOp;
      if (MI.isRegTiedToUseOperand(i, &TiedOp)) {
        PhysReg = MI.getOperand(TiedOp).getReg();
        if (SubIdx) {
          unsigned SuperReg = findSuperReg(RC, PhysReg, SubIdx, TRI);
          assert(SuperReg && TRI->getSubReg(SuperReg, SubIdx) == PhysReg &&
                 "Can't find corresponding super-register!");
          PhysReg = SuperReg;
        }
      } else {
        PhysReg = VRM.getPhys(VirtReg);
        if (ReusedOperands.isClobbered(PhysReg)) {
          // Another def has taken the assigned physreg. It must have been a
          // use&def which got it due to reuse. Undo the reuse!
          PhysReg = ReusedOperands.GetRegForReload(PhysReg, &MI, 
                               Spills, MaybeDeadStores, RegKills, KillOps, VRM);
        }
      }

      assert(PhysReg && "VR not assigned a physical register?");
      RegInfo->setPhysRegUsed(PhysReg);
      unsigned RReg = SubIdx ? TRI->getSubReg(PhysReg, SubIdx) : PhysReg;
      ReusedOperands.markClobbered(RReg);
      MI.getOperand(i).setReg(RReg);
      MI.getOperand(i).setSubReg(0);

      if (!MO.isDead()) {
        MachineInstr *&LastStore = MaybeDeadStores[StackSlot];
        SpillRegToStackSlot(MBB, MII, -1, PhysReg, StackSlot, RC, true,
                          LastStore, Spills, ReMatDefs, RegKills, KillOps, VRM);
        NextMII = next(MII);

        // Check to see if this is a noop copy.  If so, eliminate the
        // instruction before considering the dest reg to be changed.
        {
          unsigned Src, Dst, SrcSR, DstSR;
          if (TII->isMoveInstr(MI, Src, Dst, SrcSR, DstSR) && Src == Dst) {
            ++NumDCE;
            DOUT << "Removing now-noop copy: " << MI;
            InvalidateKills(MI, RegKills, KillOps);
            VRM.RemoveMachineInstrFromMaps(&MI);
            MBB.erase(&MI);
            Erased = true;
            UpdateKills(*LastStore, RegKills, KillOps, TRI);
            goto ProcessNextInst;
          }
        }
      }    
    }
  ProcessNextInst:
    DistanceMap.insert(std::make_pair(&MI, Dist++));
    if (!Erased && !BackTracked) {
      for (MachineBasicBlock::iterator II = &MI; II != NextMII; ++II)
        UpdateKills(*II, RegKills, KillOps, TRI);
    }
    MII = NextMII;
  }

}

llvm::Spiller* llvm::createSpiller() {
  switch (SpillerOpt) {
  default: assert(0 && "Unreachable!");
  case local:
    return new LocalSpiller();
  case simple:
    return new SimpleSpiller();
  }
}
