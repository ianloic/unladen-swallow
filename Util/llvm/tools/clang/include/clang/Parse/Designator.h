//===--- Designator.h - Initialization Designator ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines interfaces used to represent Designators in the parser and
// is the input to Actions module.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_PARSE_DESIGNATOR_H
#define LLVM_CLANG_PARSE_DESIGNATOR_H

#include "clang/Parse/Action.h"

namespace clang {

/// Designator - This class is a discriminated union which holds the various
/// different sorts of designators possible.  A Designation is an array of
/// these.  An example of a designator are things like this:
///     [8] .field [47]        // C99 designation: 3 designators
///     [8 ... 47]  field:     // GNU extensions: 2 designators
/// These occur in initializers, e.g.:
///  int a[10] = {2, 4, [8]=9, 10};
///
class Designator {
public:
  enum DesignatorKind {
    FieldDesignator, ArrayDesignator, ArrayRangeDesignator
  };
private:
  DesignatorKind Kind;
  
  struct FieldDesignatorInfo {
    const IdentifierInfo *II;
    unsigned DotLoc;
    unsigned NameLoc;
  };
  struct ArrayDesignatorInfo {
    Action::ExprTy *Index;
    unsigned LBracketLoc;
    mutable unsigned  RBracketLoc;
  };
  struct ArrayRangeDesignatorInfo {
    Action::ExprTy *Start, *End;
    unsigned LBracketLoc, EllipsisLoc;
    mutable unsigned RBracketLoc;
  };
  
  union {
    FieldDesignatorInfo FieldInfo;
    ArrayDesignatorInfo ArrayInfo;
    ArrayRangeDesignatorInfo ArrayRangeInfo;
  };
  
public:
  
  DesignatorKind getKind() const { return Kind; }
  bool isFieldDesignator() const { return Kind == FieldDesignator; }
  bool isArrayDesignator() const { return Kind == ArrayDesignator; }
  bool isArrayRangeDesignator() const { return Kind == ArrayRangeDesignator; }

  const IdentifierInfo *getField() const {
    assert(isFieldDesignator() && "Invalid accessor");
    return FieldInfo.II;
  }

  SourceLocation getDotLoc() const {
    assert(isFieldDesignator() && "Invalid accessor");
    return SourceLocation::getFromRawEncoding(FieldInfo.DotLoc);
  }

  SourceLocation getFieldLoc() const {
    assert(isFieldDesignator() && "Invalid accessor");
    return SourceLocation::getFromRawEncoding(FieldInfo.NameLoc);
  }
  
  Action::ExprTy *getArrayIndex() const {
    assert(isArrayDesignator() && "Invalid accessor");
    return ArrayInfo.Index;
  }

  Action::ExprTy *getArrayRangeStart() const {
    assert(isArrayRangeDesignator() && "Invalid accessor");
    return ArrayRangeInfo.Start;
  }
  Action::ExprTy *getArrayRangeEnd() const {
    assert(isArrayRangeDesignator() && "Invalid accessor");
    return ArrayRangeInfo.End;
  }
  
  SourceLocation getLBracketLoc() const {
    assert((isArrayDesignator() || isArrayRangeDesignator()) && 
           "Invalid accessor");
    if (isArrayDesignator())
      return SourceLocation::getFromRawEncoding(ArrayInfo.LBracketLoc);
    else 
      return SourceLocation::getFromRawEncoding(ArrayRangeInfo.LBracketLoc);
  }

  SourceLocation getRBracketLoc() const {
    assert((isArrayDesignator() || isArrayRangeDesignator()) && 
           "Invalid accessor");
    if (isArrayDesignator())
      return SourceLocation::getFromRawEncoding(ArrayInfo.RBracketLoc);
    else 
      return SourceLocation::getFromRawEncoding(ArrayRangeInfo.RBracketLoc);
  }

  SourceLocation getEllipsisLoc() const {
    assert(isArrayRangeDesignator() && "Invalid accessor");
    return SourceLocation::getFromRawEncoding(ArrayRangeInfo.EllipsisLoc);
  }

  static Designator getField(const IdentifierInfo *II, SourceLocation DotLoc,
                             SourceLocation NameLoc) {
    Designator D;
    D.Kind = FieldDesignator;
    D.FieldInfo.II = II;
    D.FieldInfo.DotLoc = DotLoc.getRawEncoding();
    D.FieldInfo.NameLoc = NameLoc.getRawEncoding();
    return D;
  }

  static Designator getArray(Action::ExprTy *Index, SourceLocation LBracketLoc) {
    Designator D;
    D.Kind = ArrayDesignator;
    D.ArrayInfo.Index = Index;
    D.ArrayInfo.LBracketLoc = LBracketLoc.getRawEncoding();
    D.ArrayInfo.RBracketLoc = 0;
    return D;
  }
  
  static Designator getArrayRange(Action::ExprTy *Start, Action::ExprTy *End,
                                  SourceLocation LBracketLoc, 
                                  SourceLocation EllipsisLoc) {
    Designator D;
    D.Kind = ArrayRangeDesignator;
    D.ArrayRangeInfo.Start = Start;
    D.ArrayRangeInfo.End = End;
    D.ArrayRangeInfo.LBracketLoc = LBracketLoc.getRawEncoding();
    D.ArrayRangeInfo.EllipsisLoc = EllipsisLoc.getRawEncoding();
    D.ArrayRangeInfo.RBracketLoc = 0;
    return D;
  }

  void setRBracketLoc(SourceLocation RBracketLoc) const {
    assert((isArrayDesignator() || isArrayRangeDesignator()) && 
           "Invalid accessor");
    if (isArrayDesignator())
      ArrayInfo.RBracketLoc = RBracketLoc.getRawEncoding();
    else
      ArrayRangeInfo.RBracketLoc = RBracketLoc.getRawEncoding();
  }
  
  /// ClearExprs - Null out any expression references, which prevents them from
  /// being 'delete'd later.
  void ClearExprs(Action &Actions) {
    switch (Kind) {
    case FieldDesignator: return;
    case ArrayDesignator:
      ArrayInfo.Index = 0;
      return;
    case ArrayRangeDesignator:
      ArrayRangeInfo.Start = 0;
      ArrayRangeInfo.End = 0;
      return;
    }
  }
  
  /// FreeExprs - Release any unclaimed memory for the expressions in this
  /// designator.
  void FreeExprs(Action &Actions) {
    switch (Kind) {
    case FieldDesignator: return; // nothing to free.
    case ArrayDesignator:
      Actions.DeleteExpr(getArrayIndex());
      return;
    case ArrayRangeDesignator:
      Actions.DeleteExpr(getArrayRangeStart());
      Actions.DeleteExpr(getArrayRangeEnd());
      return;
    }
  }
};

  
/// Designation - Represent a full designation, which is a sequence of
/// designators.  This class is mostly a helper for InitListDesignations.
class Designation {
  friend class InitListDesignations;
  
  /// InitIndex - The index of the initializer expression this is for.  For
  /// example, if the initializer were "{ A, .foo=B, C }" a Designation would
  /// exist with InitIndex=1, because element #1 has a designation.
  unsigned InitIndex;
  
  /// Designators - The actual designators for this initializer.
  llvm::SmallVector<Designator, 2> Designators;
  
  Designation(unsigned Idx) : InitIndex(Idx) {}
public:
  
  /// AddDesignator - Add a designator to the end of this list.
  void AddDesignator(Designator D) {
    Designators.push_back(D);
  }
  
  unsigned getNumDesignators() const { return Designators.size(); }
  const Designator &getDesignator(unsigned Idx) const {
    assert(Idx < Designators.size());
    return Designators[Idx];
  }
  
  /// ClearExprs - Null out any expression references, which prevents them from
  /// being 'delete'd later.
  void ClearExprs(Action &Actions) {
    for (unsigned i = 0, e = Designators.size(); i != e; ++i)
      Designators[i].ClearExprs(Actions);
  }
  
  /// FreeExprs - Release any unclaimed memory for the expressions in this
  /// designation.
  void FreeExprs(Action &Actions) {
    for (unsigned i = 0, e = Designators.size(); i != e; ++i)
      Designators[i].FreeExprs(Actions);
  }
};
  
  
/// InitListDesignations - This contains all the designators for an
/// initializer list.  This is somewhat like a two dimensional array of
/// Designators, but is optimized for the cases when designators are not
/// present.
class InitListDesignations {
  Action &Actions;
  
  /// Designations - All of the designators in this init list.  These are kept
  /// in order sorted by their InitIndex.
  llvm::SmallVector<Designation, 3> Designations;
  
  InitListDesignations(const InitListDesignations&); // DO NOT IMPLEMENT
  void operator=(const InitListDesignations&);      // DO NOT IMPLEMENT
public:
  InitListDesignations(Action &A) : Actions(A) {}
  
  ~InitListDesignations() {
    // Release any unclaimed memory for the expressions in this init list.
    for (unsigned i = 0, e = Designations.size(); i != e; ++i)
      Designations[i].FreeExprs(Actions);
  }
  
  bool hasAnyDesignators() const {
    return !Designations.empty();
  }
  
  Designation &CreateDesignation(unsigned Idx) {
    assert((Designations.empty() || Designations.back().InitIndex < Idx) &&
           "not sorted by InitIndex!");
    Designations.push_back(Designation(Idx));
    return Designations.back();
  }
  
  /// getDesignationForInitializer - If there is a designator for the specified
  /// initializer, return it, otherwise return null.
  const Designation *getDesignationForInitializer(unsigned Idx) const {
    // The common case is no designators.
    if (!hasAnyDesignators()) return 0;
    
    // FIXME: This should do a binary search, not a linear one.
    for (unsigned i = 0, e = Designations.size(); i != e; ++i)
      if (Designations[i].InitIndex == Idx)
        return &Designations[i];
    return 0;
  }

  /// EraseDesignation - If there is a designator for the specified initializer
  /// index, remove it.
  void EraseDesignation(unsigned Idx) {
    Designation *D =const_cast<Designation*>(getDesignationForInitializer(Idx));
    if (D == 0) return;  // No designator.
    
    D->FreeExprs(Actions);
    unsigned SlotNo = D-&Designations[0];
    Designations.erase(Designations.begin()+SlotNo);
  }
  
};

} // end namespace clang

#endif
