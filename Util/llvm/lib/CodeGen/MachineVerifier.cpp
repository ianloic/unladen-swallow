//===-- MachineVerifier.cpp - Machine Code Verifier -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Pass to verify generated machine code. The following is checked:
//
// Operand counts: All explicit operands must be present.
//
// Register classes: All physical and virtual register operands must be
// compatible with the register class required by the instruction descriptor.
//
// Register live intervals: Registers must be defined only once, and must be
// defined before use.
//
// The machine code verifier is enabled from LLVMTargetMachine.cpp with the
// command-line option -verify-machineinstrs, or by defining the environment
// variable LLVM_VERIFY_MACHINEINSTRS to the name of a file that will receive
// the verifier errors.
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Function.h"
#include "llvm/CodeGen/LiveVariables.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include <fstream>

using namespace llvm;

namespace {
  struct VISIBILITY_HIDDEN MachineVerifier : public MachineFunctionPass {
    static char ID; // Pass ID, replacement for typeid

    MachineVerifier(bool allowDoubleDefs = false) :
      MachineFunctionPass(&ID),
      allowVirtDoubleDefs(allowDoubleDefs),
      allowPhysDoubleDefs(allowDoubleDefs),
      OutFileName(getenv("LLVM_VERIFY_MACHINEINSTRS"))
        {}

    void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesAll();
    }

    bool runOnMachineFunction(MachineFunction &MF);

    const bool allowVirtDoubleDefs;
    const bool allowPhysDoubleDefs;

    const char *const OutFileName;
    std::ostream *OS;
    const MachineFunction *MF;
    const TargetMachine *TM;
    const TargetRegisterInfo *TRI;
    const MachineRegisterInfo *MRI;

    unsigned foundErrors;

    typedef SmallVector<unsigned, 16> RegVector;
    typedef DenseSet<unsigned> RegSet;
    typedef DenseMap<unsigned, const MachineInstr*> RegMap;

    BitVector regsReserved;
    RegSet regsLive;
    RegVector regsDefined, regsImpDefined, regsDead, regsKilled;

    // Add Reg and any sub-registers to RV
    void addRegWithSubRegs(RegVector &RV, unsigned Reg) {
      RV.push_back(Reg);
      if (TargetRegisterInfo::isPhysicalRegister(Reg))
        for (const unsigned *R = TRI->getSubRegisters(Reg); *R; R++)
          RV.push_back(*R);
    }

    // Does RS contain any super-registers of Reg?
    bool anySuperRegisters(const RegSet &RS, unsigned Reg) {
      for (const unsigned *R = TRI->getSuperRegisters(Reg); *R; R++)
        if (RS.count(*R))
          return true;
      return false;
    }

    struct BBInfo {
      // Is this MBB reachable from the MF entry point?
      bool reachable;

      // Vregs that must be live in because they are used without being
      // defined. Map value is the user.
      RegMap vregsLiveIn;

      // Vregs that must be dead in because they are defined without being
      // killed first. Map value is the defining instruction.
      RegMap vregsDeadIn;

      // Regs killed in MBB. They may be defined again, and will then be in both
      // regsKilled and regsLiveOut.
      RegSet regsKilled;

      // Regs defined in MBB and live out. Note that vregs passing through may
      // be live out without being mentioned here.
      RegSet regsLiveOut;

      // Vregs that pass through MBB untouched. This set is disjoint from
      // regsKilled and regsLiveOut.
      RegSet vregsPassed;

      BBInfo() : reachable(false) {}

      // Add register to vregsPassed if it belongs there. Return true if
      // anything changed.
      bool addPassed(unsigned Reg) {
        if (!TargetRegisterInfo::isVirtualRegister(Reg))
          return false;
        if (regsKilled.count(Reg) || regsLiveOut.count(Reg))
          return false;
        return vregsPassed.insert(Reg).second;
      }

      // Same for a full set.
      bool addPassed(const RegSet &RS) {
        bool changed = false;
        for (RegSet::const_iterator I = RS.begin(), E = RS.end(); I != E; ++I)
          if (addPassed(*I))
            changed = true;
        return changed;
      }

      // Live-out registers are either in regsLiveOut or vregsPassed.
      bool isLiveOut(unsigned Reg) const {
        return regsLiveOut.count(Reg) || vregsPassed.count(Reg);
      }
    };

    // Extra register info per MBB.
    DenseMap<const MachineBasicBlock*, BBInfo> MBBInfoMap;

    bool isReserved(unsigned Reg) {
      return Reg < regsReserved.size() && regsReserved[Reg];
    }

    void visitMachineFunctionBefore();
    void visitMachineBasicBlockBefore(const MachineBasicBlock *MBB);
    void visitMachineInstrBefore(const MachineInstr *MI);
    void visitMachineOperand(const MachineOperand *MO, unsigned MONum);
    void visitMachineInstrAfter(const MachineInstr *MI);
    void visitMachineBasicBlockAfter(const MachineBasicBlock *MBB);
    void visitMachineFunctionAfter();

    void report(const char *msg, const MachineFunction *MF);
    void report(const char *msg, const MachineBasicBlock *MBB);
    void report(const char *msg, const MachineInstr *MI);
    void report(const char *msg, const MachineOperand *MO, unsigned MONum);

    void markReachable(const MachineBasicBlock *MBB);
    void calcMaxRegsPassed();
    void calcMinRegsPassed();
    void checkPHIOps(const MachineBasicBlock *MBB);
  };
}

char MachineVerifier::ID = 0;
static RegisterPass<MachineVerifier>
MachineVer("machineverifier", "Verify generated machine code");
static const PassInfo *const MachineVerifyID = &MachineVer;

FunctionPass *
llvm::createMachineVerifierPass(bool allowPhysDoubleDefs)
{
  return new MachineVerifier(allowPhysDoubleDefs);
}

bool
MachineVerifier::runOnMachineFunction(MachineFunction &MF)
{
  std::ofstream OutFile;
  if (OutFileName) {
    OutFile.open(OutFileName, std::ios::out | std::ios::app);
    OS = &OutFile;
  } else {
    OS = cerr.stream();
  }

  foundErrors = 0;

  this->MF = &MF;
  TM = &MF.getTarget();
  TRI = TM->getRegisterInfo();
  MRI = &MF.getRegInfo();

  visitMachineFunctionBefore();
  for (MachineFunction::const_iterator MFI = MF.begin(), MFE = MF.end();
       MFI!=MFE; ++MFI) {
    visitMachineBasicBlockBefore(MFI);
    for (MachineBasicBlock::const_iterator MBBI = MFI->begin(),
           MBBE = MFI->end(); MBBI != MBBE; ++MBBI) {
      visitMachineInstrBefore(MBBI);
      for (unsigned I = 0, E = MBBI->getNumOperands(); I != E; ++I)
        visitMachineOperand(&MBBI->getOperand(I), I);
      visitMachineInstrAfter(MBBI);
    }
    visitMachineBasicBlockAfter(MFI);
  }
  visitMachineFunctionAfter();

  if (OutFileName)
    OutFile.close();

  if (foundErrors) {
    cerr << "\nStopping with " << foundErrors << " machine code errors.\n";
    exit(1);
  }

  return false;                 // no changes
}

void
MachineVerifier::report(const char *msg, const MachineFunction *MF)
{
  assert(MF);
  *OS << "\n";
  if (!foundErrors++)
    MF->print(OS);
  *OS << "*** Bad machine code: " << msg << " ***\n"
      << "- function:    " << MF->getFunction()->getName() << "\n";
}

void
MachineVerifier::report(const char *msg, const MachineBasicBlock *MBB)
{
  assert(MBB);
  report(msg, MBB->getParent());
  *OS << "- basic block: " << MBB->getBasicBlock()->getName()
      << " " << (void*)MBB
      << " (#" << MBB->getNumber() << ")\n";
}

void
MachineVerifier::report(const char *msg, const MachineInstr *MI)
{
  assert(MI);
  report(msg, MI->getParent());
  *OS << "- instruction: ";
  MI->print(OS, TM);
}

void
MachineVerifier::report(const char *msg,
                        const MachineOperand *MO, unsigned MONum)
{
  assert(MO);
  report(msg, MO->getParent());
  *OS << "- operand " << MONum << ":   ";
  MO->print(*OS, TM);
  *OS << "\n";
}

void
MachineVerifier::markReachable(const MachineBasicBlock *MBB)
{
  BBInfo &MInfo = MBBInfoMap[MBB];
  if (!MInfo.reachable) {
    MInfo.reachable = true;
    for (MachineBasicBlock::const_succ_iterator SuI = MBB->succ_begin(),
           SuE = MBB->succ_end(); SuI != SuE; ++SuI)
      markReachable(*SuI);
  }
}

void
MachineVerifier::visitMachineFunctionBefore()
{
  regsReserved = TRI->getReservedRegs(*MF);
  markReachable(&MF->front());
}

void
MachineVerifier::visitMachineBasicBlockBefore(const MachineBasicBlock *MBB)
{
  regsLive.clear();
  for (MachineBasicBlock::const_livein_iterator I = MBB->livein_begin(),
         E = MBB->livein_end(); I != E; ++I) {
    if (!TargetRegisterInfo::isPhysicalRegister(*I)) {
      report("MBB live-in list contains non-physical register", MBB);
      continue;
    }
    regsLive.insert(*I);
    for (const unsigned *R = TRI->getSubRegisters(*I); *R; R++)
      regsLive.insert(*R);
  }
  regsKilled.clear();
  regsDefined.clear();
  regsImpDefined.clear();
}

void
MachineVerifier::visitMachineInstrBefore(const MachineInstr *MI)
{
  const TargetInstrDesc &TI = MI->getDesc();
  if (MI->getNumExplicitOperands() < TI.getNumOperands()) {
    report("Too few operands", MI);
    *OS << TI.getNumOperands() << " operands expected, but "
        << MI->getNumExplicitOperands() << " given.\n";
  }
  if (!TI.isVariadic()) {
    if (MI->getNumExplicitOperands() > TI.getNumOperands()) {
      report("Too many operands", MI);
      *OS << TI.getNumOperands() << " operands expected, but "
          << MI->getNumExplicitOperands() << " given.\n";
    }
  }
}

void
MachineVerifier::visitMachineOperand(const MachineOperand *MO, unsigned MONum)
{
  const MachineInstr *MI = MO->getParent();
  const TargetInstrDesc &TI = MI->getDesc();

  // The first TI.NumDefs operands must be explicit register defines
  if (MONum < TI.getNumDefs()) {
    if (!MO->isReg())
      report("Explicit definition must be a register", MO, MONum);
    else if (!MO->isDef())
      report("Explicit definition marked as use", MO, MONum);
    else if (MO->isImplicit())
      report("Explicit definition marked as implicit", MO, MONum);
  }

  switch (MO->getType()) {
  case MachineOperand::MO_Register: {
    const unsigned Reg = MO->getReg();
    if (!Reg)
      return;

    // Check Live Variables.
    if (MO->isUse()) {
      if (MO->isKill()) {
        addRegWithSubRegs(regsKilled, Reg);
      } else {
        // TwoAddress instr modyfying a reg is treated as kill+def.
        unsigned defIdx;
        if (MI->isRegTiedToDefOperand(MONum, &defIdx) &&
            MI->getOperand(defIdx).getReg() == Reg)
          addRegWithSubRegs(regsKilled, Reg);
      }
      // Explicit use of a dead register.
      if (!MO->isImplicit() && !regsLive.count(Reg)) {
        if (TargetRegisterInfo::isPhysicalRegister(Reg)) {
          // Reserved registers may be used even when 'dead'.
          if (!isReserved(Reg))
            report("Using an undefined physical register", MO, MONum);
        } else {
          BBInfo &MInfo = MBBInfoMap[MI->getParent()];
          // We don't know which virtual registers are live in, so only complain
          // if vreg was killed in this MBB. Otherwise keep track of vregs that
          // must be live in. PHI instructions are handled separately.
          if (MInfo.regsKilled.count(Reg))
            report("Using a killed virtual register", MO, MONum);
          else if (MI->getOpcode() != TargetInstrInfo::PHI)
            MInfo.vregsLiveIn.insert(std::make_pair(Reg, MI));
        }
      }
    } else {
      // Register defined.
      // TODO: verify that earlyclobber ops are not used.
      if (MO->isImplicit())
        addRegWithSubRegs(regsImpDefined, Reg);
      else
        addRegWithSubRegs(regsDefined, Reg);

      if (MO->isDead())
        addRegWithSubRegs(regsDead, Reg);
    }

    // Check register classes.
    if (MONum < TI.getNumOperands() && !MO->isImplicit()) {
      const TargetOperandInfo &TOI = TI.OpInfo[MONum];
      unsigned SubIdx = MO->getSubReg();

      if (TargetRegisterInfo::isPhysicalRegister(Reg)) {
        unsigned sr = Reg;
        if (SubIdx) {
          unsigned s = TRI->getSubReg(Reg, SubIdx);
          if (!s) {
            report("Invalid subregister index for physical register",
                   MO, MONum);
            return;
          }
          sr = s;
        }
        if (TOI.RegClass) {
          const TargetRegisterClass *DRC = TRI->getRegClass(TOI.RegClass);
          if (!DRC->contains(sr)) {
            report("Illegal physical register for instruction", MO, MONum);
            *OS << TRI->getName(sr) << " is not a "
                << DRC->getName() << " register.\n";
          }
        }
      } else {
        // Virtual register.
        const TargetRegisterClass *RC = MRI->getRegClass(Reg);
        if (SubIdx) {
          if (RC->subregclasses_begin()+SubIdx >= RC->subregclasses_end()) {
            report("Invalid subregister index for virtual register", MO, MONum);
            return;
          }
          RC = *(RC->subregclasses_begin()+SubIdx);
        }
        if (TOI.RegClass) {
          const TargetRegisterClass *DRC = TRI->getRegClass(TOI.RegClass);
          if (RC != DRC && !RC->hasSuperClass(DRC)) {
            report("Illegal virtual register for instruction", MO, MONum);
            *OS << "Expected a " << DRC->getName() << " register, but got a "
                << RC->getName() << " register\n";
          }
        }
      }
    }
    break;
  }
    // Can PHI instrs refer to MBBs not in the CFG? X86 and ARM do.
    // case MachineOperand::MO_MachineBasicBlock:
    //   if (MI->getOpcode() == TargetInstrInfo::PHI) {
    //     if (!MO->getMBB()->isSuccessor(MI->getParent()))
    //       report("PHI operand is not in the CFG", MO, MONum);
    //   }
    //   break;
  default:
    break;
  }
}

void
MachineVerifier::visitMachineInstrAfter(const MachineInstr *MI)
{
  BBInfo &MInfo = MBBInfoMap[MI->getParent()];
  set_union(MInfo.regsKilled, regsKilled);
  set_subtract(regsLive, regsKilled);
  regsKilled.clear();

  for (RegVector::const_iterator I = regsDefined.begin(),
         E = regsDefined.end(); I != E; ++I) {
    if (regsLive.count(*I)) {
      if (TargetRegisterInfo::isPhysicalRegister(*I)) {
        // We allow double defines to physical registers with live
        // super-registers.
        if (!allowPhysDoubleDefs && !isReserved(*I) &&
            !anySuperRegisters(regsLive, *I)) {
          report("Redefining a live physical register", MI);
          *OS << "Register " << TRI->getName(*I)
              << " was defined but already live.\n";
        }
      } else {
        if (!allowVirtDoubleDefs) {
          report("Redefining a live virtual register", MI);
          *OS << "Virtual register %reg" << *I
              << " was defined but already live.\n";
        }
      }
    } else if (TargetRegisterInfo::isVirtualRegister(*I) &&
               !MInfo.regsKilled.count(*I)) {
      // Virtual register defined without being killed first must be dead on
      // entry.
      MInfo.vregsDeadIn.insert(std::make_pair(*I, MI));
    }
  }

  set_union(regsLive, regsDefined); regsDefined.clear();
  set_union(regsLive, regsImpDefined); regsImpDefined.clear();
  set_subtract(regsLive, regsDead); regsDead.clear();
}

void
MachineVerifier::visitMachineBasicBlockAfter(const MachineBasicBlock *MBB)
{
  MBBInfoMap[MBB].regsLiveOut = regsLive;
  regsLive.clear();
}

// Calculate the largest possible vregsPassed sets. These are the registers that
// can pass through an MBB live, but may not be live every time. It is assumed
// that all vregsPassed sets are empty before the call.
void
MachineVerifier::calcMaxRegsPassed()
{
  // First push live-out regs to successors' vregsPassed. Remember the MBBs that
  // have any vregsPassed.
  DenseSet<const MachineBasicBlock*> todo;
  for (MachineFunction::const_iterator MFI = MF->begin(), MFE = MF->end();
       MFI != MFE; ++MFI) {
    const MachineBasicBlock &MBB(*MFI);
    BBInfo &MInfo = MBBInfoMap[&MBB];
    if (!MInfo.reachable)
      continue;
    for (MachineBasicBlock::const_succ_iterator SuI = MBB.succ_begin(),
           SuE = MBB.succ_end(); SuI != SuE; ++SuI) {
      BBInfo &SInfo = MBBInfoMap[*SuI];
      if (SInfo.addPassed(MInfo.regsLiveOut))
        todo.insert(*SuI);
    }
  }

  // Iteratively push vregsPassed to successors. This will converge to the same
  // final state regardless of DenseSet iteration order.
  while (!todo.empty()) {
    const MachineBasicBlock *MBB = *todo.begin();
    todo.erase(MBB);
    BBInfo &MInfo = MBBInfoMap[MBB];
    for (MachineBasicBlock::const_succ_iterator SuI = MBB->succ_begin(),
           SuE = MBB->succ_end(); SuI != SuE; ++SuI) {
      if (*SuI == MBB)
        continue;
      BBInfo &SInfo = MBBInfoMap[*SuI];
      if (SInfo.addPassed(MInfo.vregsPassed))
        todo.insert(*SuI);
    }
  }
}

// Calculate the minimum vregsPassed set. These are the registers that always
// pass live through an MBB. The calculation assumes that calcMaxRegsPassed has
// been called earlier.
void
MachineVerifier::calcMinRegsPassed()
{
  DenseSet<const MachineBasicBlock*> todo;
  for (MachineFunction::const_iterator MFI = MF->begin(), MFE = MF->end();
       MFI != MFE; ++MFI)
    todo.insert(MFI);

  while (!todo.empty()) {
    const MachineBasicBlock *MBB = *todo.begin();
    todo.erase(MBB);
    BBInfo &MInfo = MBBInfoMap[MBB];

    // Remove entries from vRegsPassed that are not live out from all
    // reachable predecessors.
    RegSet dead;
    for (RegSet::iterator I = MInfo.vregsPassed.begin(),
           E = MInfo.vregsPassed.end(); I != E; ++I) {
      for (MachineBasicBlock::const_pred_iterator PrI = MBB->pred_begin(),
             PrE = MBB->pred_end(); PrI != PrE; ++PrI) {
        BBInfo &PrInfo = MBBInfoMap[*PrI];
        if (PrInfo.reachable && !PrInfo.isLiveOut(*I)) {
          dead.insert(*I);
          break;
        }
      }
    }
    // If any regs removed, we need to recheck successors.
    if (!dead.empty()) {
      set_subtract(MInfo.vregsPassed, dead);
      todo.insert(MBB->succ_begin(), MBB->succ_end());
    }
  }
}

// Check PHI instructions at the beginning of MBB. It is assumed that
// calcMinRegsPassed has been run so BBInfo::isLiveOut is valid.
void
MachineVerifier::checkPHIOps(const MachineBasicBlock *MBB)
{
  for (MachineBasicBlock::const_iterator BBI = MBB->begin(), BBE = MBB->end();
       BBI != BBE && BBI->getOpcode() == TargetInstrInfo::PHI; ++BBI) {
    DenseSet<const MachineBasicBlock*> seen;

    for (unsigned i = 1, e = BBI->getNumOperands(); i != e; i += 2) {
      unsigned Reg = BBI->getOperand(i).getReg();
      const MachineBasicBlock *Pre = BBI->getOperand(i + 1).getMBB();
      if (!Pre->isSuccessor(MBB))
        continue;
      seen.insert(Pre);
      BBInfo &PrInfo = MBBInfoMap[Pre];
      if (PrInfo.reachable && !PrInfo.isLiveOut(Reg))
        report("PHI operand is not live-out from predecessor",
               &BBI->getOperand(i), i);
    }

    // Did we see all predecessors?
    for (MachineBasicBlock::const_pred_iterator PrI = MBB->pred_begin(),
           PrE = MBB->pred_end(); PrI != PrE; ++PrI) {
      if (!seen.count(*PrI)) {
        report("Missing PHI operand", BBI);
        *OS << "MBB #" << (*PrI)->getNumber()
            << " is a predecessor according to the CFG.\n";
      }
    }
  }
}

void
MachineVerifier::visitMachineFunctionAfter()
{
  calcMaxRegsPassed();

  // With the maximal set of vregsPassed we can verify dead-in registers.
  for (MachineFunction::const_iterator MFI = MF->begin(), MFE = MF->end();
       MFI != MFE; ++MFI) {
    BBInfo &MInfo = MBBInfoMap[MFI];

    // Skip unreachable MBBs.
    if (!MInfo.reachable)
      continue;

    for (MachineBasicBlock::const_pred_iterator PrI = MFI->pred_begin(),
           PrE = MFI->pred_end(); PrI != PrE; ++PrI) {
      BBInfo &PrInfo = MBBInfoMap[*PrI];
      if (!PrInfo.reachable)
        continue;

      // Verify physical live-ins. EH landing pads have magic live-ins so we
      // ignore them.
      if (!MFI->isLandingPad()) {
        for (MachineBasicBlock::const_livein_iterator I = MFI->livein_begin(),
               E = MFI->livein_end(); I != E; ++I) {
          if (TargetRegisterInfo::isPhysicalRegister(*I) &&
              !isReserved (*I) && !PrInfo.isLiveOut(*I)) {
            report("Live-in physical register is not live-out from predecessor",
                   MFI);
            *OS << "Register " << TRI->getName(*I)
                << " is not live-out from MBB #" << (*PrI)->getNumber()
                << ".\n";
          }
        }
      }


      // Verify dead-in virtual registers.
      if (!allowVirtDoubleDefs) {
        for (RegMap::iterator I = MInfo.vregsDeadIn.begin(),
               E = MInfo.vregsDeadIn.end(); I != E; ++I) {
          // DeadIn register must be in neither regsLiveOut or vregsPassed of
          // any predecessor.
          if (PrInfo.isLiveOut(I->first)) {
            report("Live-in virtual register redefined", I->second);
            *OS << "Register %reg" << I->first
                << " was live-out from predecessor MBB #"
                << (*PrI)->getNumber() << ".\n";
          }
        }
      }
    }
  }

  calcMinRegsPassed();

  // With the minimal set of vregsPassed we can verify live-in virtual
  // registers, including PHI instructions.
  for (MachineFunction::const_iterator MFI = MF->begin(), MFE = MF->end();
       MFI != MFE; ++MFI) {
    BBInfo &MInfo = MBBInfoMap[MFI];

    // Skip unreachable MBBs.
    if (!MInfo.reachable)
      continue;

    checkPHIOps(MFI);

    for (MachineBasicBlock::const_pred_iterator PrI = MFI->pred_begin(),
           PrE = MFI->pred_end(); PrI != PrE; ++PrI) {
      BBInfo &PrInfo = MBBInfoMap[*PrI];
      if (!PrInfo.reachable)
        continue;

      for (RegMap::iterator I = MInfo.vregsLiveIn.begin(),
             E = MInfo.vregsLiveIn.end(); I != E; ++I) {
        if (!PrInfo.isLiveOut(I->first)) {
          report("Used virtual register is not live-in", I->second);
          *OS << "Register %reg" << I->first
              << " is not live-out from predecessor MBB #"
              << (*PrI)->getNumber()
              << ".\n";
        }
      }
    }
  }
}
