//===-- DwarfException.h - Dwarf Exception Framework -----------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing dwarf exception info into asm files.
//
//===----------------------------------------------------------------------===//

#ifndef CODEGEN_ASMPRINTER_DWARFEXCEPTION_H__
#define CODEGEN_ASMPRINTER_DWARFEXCEPTION_H__

#include "DIE.h"
#include "DwarfPrinter.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/ADT/DenseMap.h"
#include <string>

namespace llvm {

struct LandingPadInfo;
class MachineModuleInfo;
class TargetAsmInfo;
class Timer;
class raw_ostream;

//===----------------------------------------------------------------------===//
/// DwarfException - Emits Dwarf exception handling directives.
///
class VISIBILITY_HIDDEN DwarfException : public Dwarf {
  struct FunctionEHFrameInfo {
    std::string FnName;
    unsigned Number;
    unsigned PersonalityIndex;
    bool hasCalls;
    bool hasLandingPads;
    std::vector<MachineMove> Moves;
    const Function * function;

    FunctionEHFrameInfo(const std::string &FN, unsigned Num, unsigned P,
                        bool hC, bool hL,
                        const std::vector<MachineMove> &M,
                        const Function *f):
      FnName(FN), Number(Num), PersonalityIndex(P),
      hasCalls(hC), hasLandingPads(hL), Moves(M), function (f) { }
  };

  std::vector<FunctionEHFrameInfo> EHFrames;

  /// shouldEmitTable - Per-function flag to indicate if EH tables should
  /// be emitted.
  bool shouldEmitTable;

  /// shouldEmitMoves - Per-function flag to indicate if frame moves info
  /// should be emitted.
  bool shouldEmitMoves;

  /// shouldEmitTableModule - Per-module flag to indicate if EH tables
  /// should be emitted.
  bool shouldEmitTableModule;

  /// shouldEmitFrameModule - Per-module flag to indicate if frame moves
  /// should be emitted.
  bool shouldEmitMovesModule;

  /// ExceptionTimer - Timer for the Dwarf exception writer.
  Timer *ExceptionTimer;

  /// EmitCommonEHFrame - Emit the common eh unwind frame.
  ///
  void EmitCommonEHFrame(const Function *Personality, unsigned Index);

  /// EmitEHFrame - Emit function exception frame information.
  ///
  void EmitEHFrame(const FunctionEHFrameInfo &EHFrameInfo);

  /// EmitExceptionTable - Emit landing pads and actions.
  ///
  /// The general organization of the table is complex, but the basic concepts
  /// are easy.  First there is a header which describes the location and
  /// organization of the three components that follow.
  ///  1. The landing pad site information describes the range of code covered
  ///     by the try.  In our case it's an accumulation of the ranges covered
  ///     by the invokes in the try.  There is also a reference to the landing
  ///     pad that handles the exception once processed.  Finally an index into
  ///     the actions table.
  ///  2. The action table, in our case, is composed of pairs of type ids
  ///     and next action offset.  Starting with the action index from the
  ///     landing pad site, each type Id is checked for a match to the current
  ///     exception.  If it matches then the exception and type id are passed
  ///     on to the landing pad.  Otherwise the next action is looked up.  This
  ///     chain is terminated with a next action of zero.  If no type id is
  ///     found the the frame is unwound and handling continues.
  ///  3. Type id table contains references to all the C++ typeinfo for all
  ///     catches in the function.  This tables is reversed indexed base 1.

  /// SharedTypeIds - How many leading type ids two landing pads have in common.
  static unsigned SharedTypeIds(const LandingPadInfo *L,
                                const LandingPadInfo *R);

  /// PadLT - Order landing pads lexicographically by type id.
  static bool PadLT(const LandingPadInfo *L, const LandingPadInfo *R);

  struct KeyInfo {
    static inline unsigned getEmptyKey() { return -1U; }
    static inline unsigned getTombstoneKey() { return -2U; }
    static unsigned getHashValue(const unsigned &Key) { return Key; }
    static bool isEqual(unsigned LHS, unsigned RHS) { return LHS == RHS; }
    static bool isPod() { return true; }
  };

  /// ActionEntry - Structure describing an entry in the actions table.
  struct ActionEntry {
    int ValueForTypeID; // The value to write - may not be equal to the type id.
    int NextAction;
    struct ActionEntry *Previous;
  };

  /// PadRange - Structure holding a try-range and the associated landing pad.
  struct PadRange {
    // The index of the landing pad.
    unsigned PadIndex;
    // The index of the begin and end labels in the landing pad's label lists.
    unsigned RangeIndex;
  };

  typedef DenseMap<unsigned, PadRange, KeyInfo> RangeMapType;

  /// CallSiteEntry - Structure describing an entry in the call-site table.
  struct CallSiteEntry {
    // The 'try-range' is BeginLabel .. EndLabel.
    unsigned BeginLabel; // zero indicates the start of the function.
    unsigned EndLabel;   // zero indicates the end of the function.
    // The landing pad starts at PadLabel.
    unsigned PadLabel;   // zero indicates that there is no landing pad.
    unsigned Action;
  };

  void EmitExceptionTable();

public:
  //===--------------------------------------------------------------------===//
  // Main entry points.
  //
  DwarfException(raw_ostream &OS, AsmPrinter *A, const TargetAsmInfo *T);
  virtual ~DwarfException();

  /// BeginModule - Emit all exception information that should come prior to the
  /// content.
  void BeginModule(Module *m, MachineModuleInfo *mmi) {
    this->M = m;
    this->MMI = mmi;
  }

  /// EndModule - Emit all exception information that should come after the
  /// content.
  void EndModule();

  /// BeginFunction - Gather pre-function exception information.  Assumes being
  /// emitted immediately after the function entry point.
  void BeginFunction(MachineFunction *MF);

  /// EndFunction - Gather and emit post-function exception information.
  void EndFunction();
};

} // End of namespace llvm

#endif
