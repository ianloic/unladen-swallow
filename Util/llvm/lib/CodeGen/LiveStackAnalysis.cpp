//===-- LiveStackAnalysis.cpp - Live Stack Slot Analysis ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the live stack slot analysis pass. It is analogous to
// live interval analysis except it's analyzing liveness of stack slots rather
// than registers.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "livestacks"
#include "llvm/CodeGen/LiveStackAnalysis.h"
#include "llvm/CodeGen/LiveIntervalAnalysis.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/Statistic.h"
#include <limits>
using namespace llvm;

char LiveStacks::ID = 0;
static RegisterPass<LiveStacks> X("livestacks", "Live Stack Slot Analysis");

void LiveStacks::scaleNumbering(int factor) {
  // Scale the intervals.
  for (iterator LI = begin(), LE = end(); LI != LE; ++LI) {
    LI->second.scaleNumbering(factor);
  }
}

void LiveStacks::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  MachineFunctionPass::getAnalysisUsage(AU);
}

void LiveStacks::releaseMemory() {
  // Release VNInfo memroy regions after all VNInfo objects are dtor'd.
  VNInfoAllocator.Reset();
  S2IMap.clear();
  S2RCMap.clear();
}

bool LiveStacks::runOnMachineFunction(MachineFunction &) {
  // FIXME: No analysis is being done right now. We are relying on the
  // register allocators to provide the information.
  return false;
}

/// print - Implement the dump method.
void LiveStacks::print(std::ostream &O, const Module*) const {
  O << "********** INTERVALS **********\n";
  for (const_iterator I = begin(), E = end(); I != E; ++I) {
    I->second.print(O);
    int Slot = I->first;
    const TargetRegisterClass *RC = getIntervalRegClass(Slot);
    if (RC)
      O << " [" << RC->getName() << "]\n";
    else
      O << " [Unknown]\n";
  }
}
