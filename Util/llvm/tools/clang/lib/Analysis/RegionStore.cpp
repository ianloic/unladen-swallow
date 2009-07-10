//== RegionStore.cpp - Field-sensitive store model --------------*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a basic region store model. In this model, we do have field
// sensitivity. But we assume nothing about the heap shape. So recursive data
// structures are largely ignored. Basically we do 1-limiting analysis.
// Parameter pointers are assumed with no aliasing. Pointee objects of
// parameters are created lazily.
//
//===----------------------------------------------------------------------===//
#include "clang/Analysis/PathSensitive/MemRegion.h"
#include "clang/Analysis/PathSensitive/GRState.h"
#include "clang/Analysis/PathSensitive/GRStateTrait.h"
#include "clang/Analysis/Analyses/LiveVariables.h"
#include "clang/Basic/TargetInfo.h"

#include "llvm/ADT/ImmutableMap.h"
#include "llvm/ADT/ImmutableList.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Compiler.h"

using namespace clang;

// Actual Store type.
typedef llvm::ImmutableMap<const MemRegion*, SVal> RegionBindingsTy;

//===----------------------------------------------------------------------===//
// Fine-grained control of RegionStoreManager.
//===----------------------------------------------------------------------===//

namespace {
struct VISIBILITY_HIDDEN minimal_features_tag {};
struct VISIBILITY_HIDDEN maximal_features_tag {};  
  
class VISIBILITY_HIDDEN RegionStoreFeatures {
  bool SupportsFields;
  bool SupportsRemaining;
  
public:
  RegionStoreFeatures(minimal_features_tag) :
    SupportsFields(false), SupportsRemaining(false) {}
  
  RegionStoreFeatures(maximal_features_tag) :
    SupportsFields(true), SupportsRemaining(false) {}
  
  void enableFields(bool t) { SupportsFields = t; }
  
  bool supportsFields() const { return SupportsFields; }
  bool supportsRemaining() const { return SupportsRemaining; }
};
}

//===----------------------------------------------------------------------===//
// Region "Views"
//===----------------------------------------------------------------------===//
//
//  MemRegions can be layered on top of each other.  This GDM entry tracks
//  what are the MemRegions that layer a given MemRegion.
//
typedef llvm::ImmutableSet<const MemRegion*> RegionViews;
namespace { class VISIBILITY_HIDDEN RegionViewMap {}; }
static int RegionViewMapIndex = 0;
namespace clang {
  template<> struct GRStateTrait<RegionViewMap> 
    : public GRStatePartialTrait<llvm::ImmutableMap<const MemRegion*,
                                                    RegionViews> > {
                                                      
    static void* GDMIndex() { return &RegionViewMapIndex; }
  };
}

// RegionCasts records the current cast type of a region.
namespace { class VISIBILITY_HIDDEN RegionCasts {}; }
static int RegionCastsIndex = 0;
namespace clang {
  template<> struct GRStateTrait<RegionCasts>
    : public GRStatePartialTrait<llvm::ImmutableMap<const MemRegion*, 
                                                    QualType> > {
    static void* GDMIndex() { return &RegionCastsIndex; }
  };
}

//===----------------------------------------------------------------------===//
// Region "Extents"
//===----------------------------------------------------------------------===//
//
//  MemRegions represent chunks of memory with a size (their "extent").  This
//  GDM entry tracks the extents for regions.  Extents are in bytes.
//
namespace { class VISIBILITY_HIDDEN RegionExtents {}; }
static int RegionExtentsIndex = 0;
namespace clang {
  template<> struct GRStateTrait<RegionExtents>
    : public GRStatePartialTrait<llvm::ImmutableMap<const MemRegion*, SVal> > {
    static void* GDMIndex() { return &RegionExtentsIndex; }
  };
}

//===----------------------------------------------------------------------===//
// Regions with default values.
//===----------------------------------------------------------------------===//
//
// This GDM entry tracks what regions have a default value if they have no bound
// value and have not been killed.
//
namespace { class VISIBILITY_HIDDEN RegionDefaultValue {}; }
static int RegionDefaultValueIndex = 0;
namespace clang {
 template<> struct GRStateTrait<RegionDefaultValue>
   : public GRStatePartialTrait<llvm::ImmutableMap<const MemRegion*, SVal> > {
   static void* GDMIndex() { return &RegionDefaultValueIndex; }
 };
}

//===----------------------------------------------------------------------===//
// Main RegionStore logic.
//===----------------------------------------------------------------------===//

namespace {

class VISIBILITY_HIDDEN RegionStoreSubRegionMap : public SubRegionMap {
  typedef llvm::DenseMap<const MemRegion*,
                         llvm::ImmutableSet<const MemRegion*> > Map;
  
  llvm::ImmutableSet<const MemRegion*>::Factory F;
  Map M;

public:
  void add(const MemRegion* Parent, const MemRegion* SubRegion) {
    Map::iterator I = M.find(Parent);
    M.insert(std::make_pair(Parent, 
             F.Add(I == M.end() ? F.GetEmptySet() : I->second, SubRegion)));
  }
    
  ~RegionStoreSubRegionMap() {}
  
  bool iterSubRegions(const MemRegion* Parent, Visitor& V) const {
    Map::iterator I = M.find(Parent);

    if (I == M.end())
      return true;
    
    llvm::ImmutableSet<const MemRegion*> S = I->second;
    for (llvm::ImmutableSet<const MemRegion*>::iterator SI=S.begin(),SE=S.end();
         SI != SE; ++SI) {
      if (!V.Visit(Parent, *SI))
        return false;
    }
    
    return true;
  }
};  

class VISIBILITY_HIDDEN RegionStoreManager : public StoreManager {
  const RegionStoreFeatures Features;
  RegionBindingsTy::Factory RBFactory;
  RegionViews::Factory RVFactory;

  const MemRegion* SelfRegion;
  const ImplicitParamDecl *SelfDecl;

public:
  RegionStoreManager(GRStateManager& mgr, const RegionStoreFeatures &f) 
    : StoreManager(mgr, true),
      Features(f),
      RBFactory(mgr.getAllocator()),
      RVFactory(mgr.getAllocator()),
      SelfRegion(0), SelfDecl(0) {
    if (const ObjCMethodDecl* MD =
          dyn_cast<ObjCMethodDecl>(&StateMgr.getCodeDecl()))
      SelfDecl = MD->getSelfDecl();
  }

  virtual ~RegionStoreManager() {}

  SubRegionMap* getSubRegionMap(const GRState *state);
  
  /// getLValueString - Returns an SVal representing the lvalue of a
  ///  StringLiteral.  Within RegionStore a StringLiteral has an
  ///  associated StringRegion, and the lvalue of a StringLiteral is
  ///  the lvalue of that region.
  SVal getLValueString(const GRState *state, const StringLiteral* S);

  /// getLValueCompoundLiteral - Returns an SVal representing the
  ///   lvalue of a compound literal.  Within RegionStore a compound
  ///   literal has an associated region, and the lvalue of the
  ///   compound literal is the lvalue of that region.
  SVal getLValueCompoundLiteral(const GRState *state, const CompoundLiteralExpr*);

  /// getLValueVar - Returns an SVal that represents the lvalue of a
  ///  variable.  Within RegionStore a variable has an associated
  ///  VarRegion, and the lvalue of the variable is the lvalue of that region.
  SVal getLValueVar(const GRState *state, const VarDecl* VD);
  
  SVal getLValueIvar(const GRState *state, const ObjCIvarDecl* D, SVal Base);

  SVal getLValueField(const GRState *state, SVal Base, const FieldDecl* D);
  
  SVal getLValueFieldOrIvar(const GRState *state, SVal Base, const Decl* D);

  SVal getLValueElement(const GRState *state, QualType elementType,
                        SVal Base, SVal Offset);


  /// ArrayToPointer - Emulates the "decay" of an array to a pointer
  ///  type.  'Array' represents the lvalue of the array being decayed
  ///  to a pointer, and the returned SVal represents the decayed
  ///  version of that lvalue (i.e., a pointer to the first element of
  ///  the array).  This is called by GRExprEngine when evaluating
  ///  casts from arrays to pointers.
  SVal ArrayToPointer(Loc Array);

  SVal EvalBinOp(const GRState *state, BinaryOperator::Opcode Op,Loc L,
                 NonLoc R, QualType resultTy);

  Store getInitialStore() { return RBFactory.GetEmptyMap().getRoot(); }
  
  /// getSelfRegion - Returns the region for the 'self' (Objective-C) or
  ///  'this' object (C++).  When used when analyzing a normal function this
  ///  method returns NULL.
  const MemRegion* getSelfRegion(Store) {
    if (!SelfDecl)
      return 0;
    
    if (!SelfRegion) {
      const ObjCMethodDecl *MD = cast<ObjCMethodDecl>(&StateMgr.getCodeDecl());
      SelfRegion = MRMgr.getObjCObjectRegion(MD->getClassInterface(),
                                             MRMgr.getHeapRegion());
    }
    
    return SelfRegion;
  }
 
  //===-------------------------------------------------------------------===//
  // Binding values to regions.
  //===-------------------------------------------------------------------===//

  const GRState *Bind(const GRState *state, Loc LV, SVal V);

  const GRState *BindCompoundLiteral(const GRState *state,
                                 const CompoundLiteralExpr* CL, SVal V);
  
  const GRState *BindDecl(const GRState *state, const VarDecl* VD, SVal InitVal);

  const GRState *BindDeclWithNoInit(const GRState *state, const VarDecl* VD) {
    return state;
  }

  /// BindStruct - Bind a compound value to a structure.
  const GRState *BindStruct(const GRState *, const TypedRegion* R, SVal V);
    
  const GRState *BindArray(const GRState *state, const TypedRegion* R, SVal V);
  
  /// KillStruct - Set the entire struct to unknown. 
  const GRState *KillStruct(const GRState *state, const TypedRegion* R);

  const GRState *setDefaultValue(const GRState *state, const MemRegion* R, SVal V);

  Store Remove(Store store, Loc LV);

  //===------------------------------------------------------------------===//
  // Loading values from regions.
  //===------------------------------------------------------------------===//
  
  /// The high level logic for this method is this:
  /// Retrieve (L)
  ///   if L has binding
  ///     return L's binding
  ///   else if L is in killset
  ///     return unknown
  ///   else
  ///     if L is on stack or heap
  ///       return undefined
  ///     else
  ///       return symbolic
  SVal Retrieve(const GRState *state, Loc L, QualType T = QualType());

  SVal RetrieveElement(const GRState* state, const ElementRegion* R);

  SVal RetrieveField(const GRState* state, const FieldRegion* R);

  /// Retrieve the values in a struct and return a CompoundVal, used when doing
  /// struct copy: 
  /// struct s x, y; 
  /// x = y;
  /// y's value is retrieved by this method.
  SVal RetrieveStruct(const GRState *St, const TypedRegion* R);
  
  SVal RetrieveArray(const GRState *St, const TypedRegion* R);

  //===------------------------------------------------------------------===//
  // State pruning.
  //===------------------------------------------------------------------===//
  
  /// RemoveDeadBindings - Scans the RegionStore of 'state' for dead values.
  ///  It returns a new Store with these values removed.
  Store RemoveDeadBindings(const GRState *state, Stmt* Loc, SymbolReaper& SymReaper,
                          llvm::SmallVectorImpl<const MemRegion*>& RegionRoots);

  //===------------------------------------------------------------------===//
  // Region "extents".
  //===------------------------------------------------------------------===//
  
  const GRState *setExtent(const GRState *state, const MemRegion* R, SVal Extent);
  SVal getSizeInElements(const GRState *state, const MemRegion* R);

  //===------------------------------------------------------------------===//
  // Region "views".
  //===------------------------------------------------------------------===//
  
  const GRState *AddRegionView(const GRState *state, const MemRegion* View,
                           const MemRegion* Base);

  const GRState *RemoveRegionView(const GRState *state, const MemRegion* View,
                              const MemRegion* Base);

  //===------------------------------------------------------------------===//
  // Utility methods.
  //===------------------------------------------------------------------===//
  
  const GRState *setCastType(const GRState *state, const MemRegion* R,
                             QualType T);

  static inline RegionBindingsTy GetRegionBindings(Store store) {
   return RegionBindingsTy(static_cast<const RegionBindingsTy::TreeTy*>(store));
  }

  void print(Store store, llvm::raw_ostream& Out, const char* nl,
             const char *sep);

  void iterBindings(Store store, BindingsHandler& f) {
    // FIXME: Implement.
  }

  // FIXME: Remove.
  BasicValueFactory& getBasicVals() {
      return StateMgr.getBasicVals();
  }
  
  // FIXME: Remove.
  ASTContext& getContext() { return StateMgr.getContext(); }
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// RegionStore creation.
//===----------------------------------------------------------------------===//

StoreManager *clang::CreateRegionStoreManager(GRStateManager& StMgr) {
  RegionStoreFeatures F = maximal_features_tag();
  return new RegionStoreManager(StMgr, F);
}

StoreManager *clang::CreateFieldsOnlyRegionStoreManager(GRStateManager &StMgr) {
  RegionStoreFeatures F = minimal_features_tag();
  F.enableFields(true);
  return new RegionStoreManager(StMgr, F);
}

SubRegionMap* RegionStoreManager::getSubRegionMap(const GRState *state) {
  RegionBindingsTy B = GetRegionBindings(state->getStore());
  RegionStoreSubRegionMap *M = new RegionStoreSubRegionMap();
  
  for (RegionBindingsTy::iterator I=B.begin(), E=B.end(); I!=E; ++I) {
    if (const SubRegion* R = dyn_cast<SubRegion>(I.getKey()))
      M->add(R->getSuperRegion(), R);
  }
  
  return M;
}

//===----------------------------------------------------------------------===//
// getLValueXXX methods.
//===----------------------------------------------------------------------===//

/// getLValueString - Returns an SVal representing the lvalue of a
///  StringLiteral.  Within RegionStore a StringLiteral has an
///  associated StringRegion, and the lvalue of a StringLiteral is the
///  lvalue of that region.
SVal RegionStoreManager::getLValueString(const GRState *St, 
                                         const StringLiteral* S) {
  return loc::MemRegionVal(MRMgr.getStringRegion(S));
}

/// getLValueVar - Returns an SVal that represents the lvalue of a
///  variable.  Within RegionStore a variable has an associated
///  VarRegion, and the lvalue of the variable is the lvalue of that region.
SVal RegionStoreManager::getLValueVar(const GRState *St, const VarDecl* VD) {
  return loc::MemRegionVal(MRMgr.getVarRegion(VD));
}

/// getLValueCompoundLiteral - Returns an SVal representing the lvalue
///   of a compound literal.  Within RegionStore a compound literal
///   has an associated region, and the lvalue of the compound literal
///   is the lvalue of that region.
SVal
RegionStoreManager::getLValueCompoundLiteral(const GRState *St,
					     const CompoundLiteralExpr* CL) {
  return loc::MemRegionVal(MRMgr.getCompoundLiteralRegion(CL));
}

SVal RegionStoreManager::getLValueIvar(const GRState *St, const ObjCIvarDecl* D,
                                       SVal Base) {
  return getLValueFieldOrIvar(St, Base, D);
}

SVal RegionStoreManager::getLValueField(const GRState *St, SVal Base,
                                        const FieldDecl* D) {
  return getLValueFieldOrIvar(St, Base, D);
}

SVal RegionStoreManager::getLValueFieldOrIvar(const GRState *St, SVal Base,
                                              const Decl* D) {
  if (Base.isUnknownOrUndef())
    return Base;

  Loc BaseL = cast<Loc>(Base);
  const MemRegion* BaseR = 0;

  switch (BaseL.getSubKind()) {
  case loc::MemRegionKind:
    BaseR = cast<loc::MemRegionVal>(BaseL).getRegion();
    break;

  case loc::GotoLabelKind:
    // These are anormal cases. Flag an undefined value.
    return UndefinedVal();

  case loc::ConcreteIntKind:
    // While these seem funny, this can happen through casts.
    // FIXME: What we should return is the field offset.  For example,
    //  add the field offset to the integer value.  That way funny things
    //  like this work properly:  &(((struct foo *) 0xa)->f)
    return Base;

  default:
    assert(0 && "Unhandled Base.");
    return Base;
  }
  
  // NOTE: We must have this check first because ObjCIvarDecl is a subclass
  // of FieldDecl.
  if (const ObjCIvarDecl *ID = dyn_cast<ObjCIvarDecl>(D))
    return loc::MemRegionVal(MRMgr.getObjCIvarRegion(ID, BaseR));

  return loc::MemRegionVal(MRMgr.getFieldRegion(cast<FieldDecl>(D), BaseR));
}

SVal RegionStoreManager::getLValueElement(const GRState *St,
                                          QualType elementType,
                                          SVal Base, SVal Offset) {

  // If the base is an unknown or undefined value, just return it back.
  // FIXME: For absolute pointer addresses, we just return that value back as
  //  well, although in reality we should return the offset added to that
  //  value.
  if (Base.isUnknownOrUndef() || isa<loc::ConcreteInt>(Base))
    return Base;

  // Only handle integer offsets... for now.
  if (!isa<nonloc::ConcreteInt>(Offset))
    return UnknownVal();

  const MemRegion* BaseRegion = cast<loc::MemRegionVal>(Base).getRegion();

  // Pointer of any type can be cast and used as array base.
  const ElementRegion *ElemR = dyn_cast<ElementRegion>(BaseRegion);
  
  if (!ElemR) {
    //
    // If the base region is not an ElementRegion, create one.
    // This can happen in the following example:
    //
    //   char *p = __builtin_alloc(10);
    //   p[1] = 8;
    //
    //  Observe that 'p' binds to an AllocaRegion.
    //

    // Offset might be unsigned. We have to convert it to signed ConcreteInt.
    if (nonloc::ConcreteInt* CI = dyn_cast<nonloc::ConcreteInt>(&Offset)) {
      const llvm::APSInt& OffI = CI->getValue();
      if (OffI.isUnsigned()) {
        llvm::APSInt Tmp = OffI;
        Tmp.setIsSigned(true);
        Offset = ValMgr.makeIntVal(Tmp);
      }
    }
    return loc::MemRegionVal(MRMgr.getElementRegion(elementType, Offset,
                                                    BaseRegion, getContext()));
  }
  
  SVal BaseIdx = ElemR->getIndex();
  
  if (!isa<nonloc::ConcreteInt>(BaseIdx))
    return UnknownVal();
  
  const llvm::APSInt& BaseIdxI = cast<nonloc::ConcreteInt>(BaseIdx).getValue();
  const llvm::APSInt& OffI = cast<nonloc::ConcreteInt>(Offset).getValue();
  assert(BaseIdxI.isSigned());
  
  // FIXME: This appears to be the assumption of this code.  We should review
  // whether or not BaseIdxI.getBitWidth() < OffI.getBitWidth().  If it
  // can't we need to put a comment here.  If it can, we should handle it.
  assert(BaseIdxI.getBitWidth() >= OffI.getBitWidth());

  const MemRegion *ArrayR = ElemR->getSuperRegion();
  SVal NewIdx;
  
  if (OffI.isUnsigned() || OffI.getBitWidth() < BaseIdxI.getBitWidth()) {
    // 'Offset' might be unsigned.  We have to convert it to signed and
    // possibly extend it.
    llvm::APSInt Tmp = OffI;
    
    if (OffI.getBitWidth() < BaseIdxI.getBitWidth())
        Tmp.extend(BaseIdxI.getBitWidth());
    
    Tmp.setIsSigned(true);
    Tmp += BaseIdxI; // Compute the new offset.    
    NewIdx = ValMgr.makeIntVal(Tmp);    
  }
  else
    NewIdx = nonloc::ConcreteInt(getBasicVals().getValue(BaseIdxI + OffI));

  return loc::MemRegionVal(MRMgr.getElementRegion(elementType, NewIdx, ArrayR,
						  getContext()));
}

//===----------------------------------------------------------------------===//
// Extents for regions.
//===----------------------------------------------------------------------===//

SVal RegionStoreManager::getSizeInElements(const GRState *state,
                                           const MemRegion* R) {
  if (const VarRegion* VR = dyn_cast<VarRegion>(R)) {
    // Get the type of the variable.
    QualType T = VR->getDesugaredValueType(getContext());

    // FIXME: Handle variable-length arrays.
    if (isa<VariableArrayType>(T))
      return UnknownVal();
    
    if (const ConstantArrayType* CAT = dyn_cast<ConstantArrayType>(T)) {
      // return the size as signed integer.
      return ValMgr.makeIntVal(CAT->getSize(), false);
    }

    const QualType* CastTy = state->get<RegionCasts>(VR);

    // If the VarRegion is cast to other type, compute the size with respect to
    // that type.
    if (CastTy) {
      QualType EleTy =cast<PointerType>(CastTy->getTypePtr())->getPointeeType();
      QualType VarTy = VR->getValueType(getContext());
      uint64_t EleSize = getContext().getTypeSize(EleTy);
      uint64_t VarSize = getContext().getTypeSize(VarTy);
      assert(VarSize != 0);
      return ValMgr.makeIntVal(VarSize/EleSize, false);
    }

    // Clients can use ordinary variables as if they were arrays.  These
    // essentially are arrays of size 1.
    return ValMgr.makeIntVal(1, false);
  }

  if (const StringRegion* SR = dyn_cast<StringRegion>(R)) {
    const StringLiteral* Str = SR->getStringLiteral();
    // We intentionally made the size value signed because it participates in 
    // operations with signed indices.
    return ValMgr.makeIntVal(Str->getByteLength()+1, false);
  }

  if (const FieldRegion* FR = dyn_cast<FieldRegion>(R)) {
    // FIXME: Unsupported yet.
    FR = 0;
    return UnknownVal();
  }

  if (isa<SymbolicRegion>(R)) {
    return UnknownVal();
  }

  if (isa<AllocaRegion>(R)) {
    return UnknownVal();
  }

  if (isa<ElementRegion>(R)) {
    return UnknownVal();
  }

  assert(0 && "Other regions are not supported yet.");
  return UnknownVal();
}

const GRState *RegionStoreManager::setExtent(const GRState *state,
                                             const MemRegion *region,
                                             SVal extent) {
  return state->set<RegionExtents>(region, extent);
}

//===----------------------------------------------------------------------===//
// Location and region casting.
//===----------------------------------------------------------------------===//

/// ArrayToPointer - Emulates the "decay" of an array to a pointer
///  type.  'Array' represents the lvalue of the array being decayed
///  to a pointer, and the returned SVal represents the decayed
///  version of that lvalue (i.e., a pointer to the first element of
///  the array).  This is called by GRExprEngine when evaluating casts
///  from arrays to pointers.
SVal RegionStoreManager::ArrayToPointer(Loc Array) {
  if (!isa<loc::MemRegionVal>(Array))
    return UnknownVal();
  
  const MemRegion* R = cast<loc::MemRegionVal>(&Array)->getRegion();
  const TypedRegion* ArrayR = dyn_cast<TypedRegion>(R);
  
  if (!ArrayR)
    return UnknownVal();
  
  // Strip off typedefs from the ArrayRegion's ValueType.
  QualType T = ArrayR->getValueType(getContext())->getDesugaredType();
  ArrayType *AT = cast<ArrayType>(T);
  T = AT->getElementType();
  
  nonloc::ConcreteInt Idx(getBasicVals().getZeroWithPtrWidth(false));
  ElementRegion* ER = MRMgr.getElementRegion(T, Idx, ArrayR, getContext());
  
  return loc::MemRegionVal(ER);                    
}

//===----------------------------------------------------------------------===//
// Pointer arithmetic.
//===----------------------------------------------------------------------===//

SVal RegionStoreManager::EvalBinOp(const GRState *state, 
                                   BinaryOperator::Opcode Op, Loc L, NonLoc R,
                                   QualType resultTy) {
  // Assume the base location is MemRegionVal.
  if (!isa<loc::MemRegionVal>(L))
    return UnknownVal();

  const MemRegion* MR = cast<loc::MemRegionVal>(L).getRegion();
  const ElementRegion *ER = 0;

  // If the operand is a symbolic or alloca region, create the first element
  // region on it.
  if (const SymbolicRegion *SR = dyn_cast<SymbolicRegion>(MR)) {
    QualType T;
    // If the SymbolicRegion was cast to another type, use that type.
    if (const QualType *t = state->get<RegionCasts>(SR)) {
      T = *t;
    } else {
      // Otherwise use the symbol's type.
      SymbolRef Sym = SR->getSymbol();
      T = Sym->getType(getContext());
    }
    QualType EleTy = T->getAsPointerType()->getPointeeType();

    SVal ZeroIdx = ValMgr.makeZeroArrayIndex();
    ER = MRMgr.getElementRegion(EleTy, ZeroIdx, SR, getContext());
  } 
  else if (const AllocaRegion *AR = dyn_cast<AllocaRegion>(MR)) {
    // Get the alloca region's current cast type.


    GRStateTrait<RegionCasts>::lookup_type T = state->get<RegionCasts>(AR);
    assert(T && "alloca region has no type.");
    QualType EleTy = cast<PointerType>(T->getTypePtr())->getPointeeType();
    SVal ZeroIdx = ValMgr.makeZeroArrayIndex();
    ER = MRMgr.getElementRegion(EleTy, ZeroIdx, AR, getContext());
  } 
  else if (isa<FieldRegion>(MR)) {
    // Not track pointer arithmetic on struct fields.
    return UnknownVal();
  }
  else {
    ER = cast<ElementRegion>(MR);
  }

  SVal Idx = ER->getIndex();

  nonloc::ConcreteInt* Base = dyn_cast<nonloc::ConcreteInt>(&Idx);
  nonloc::ConcreteInt* Offset = dyn_cast<nonloc::ConcreteInt>(&R);

  // Only support concrete integer indexes for now.
  if (Base && Offset) {
    // FIXME: For now, convert the signedness and bitwidth of offset in case
    //  they don't match.  This can result from pointer arithmetic.  In reality,
    //  we should figure out what are the proper semantics and implement them.
    // 
    //  This addresses the test case test/Analysis/ptr-arith.c
    //
    nonloc::ConcreteInt OffConverted(getBasicVals().Convert(Base->getValue(),
                                                           Offset->getValue()));
    SVal NewIdx = Base->evalBinOp(ValMgr, Op, OffConverted);
    const MemRegion* NewER =
      MRMgr.getElementRegion(ER->getElementType(), NewIdx,ER->getSuperRegion(),
			     getContext());
    return ValMgr.makeLoc(NewER);

  }
  
  return UnknownVal();
}

//===----------------------------------------------------------------------===//
// Loading values from regions.
//===----------------------------------------------------------------------===//

SVal RegionStoreManager::Retrieve(const GRState *state, Loc L, QualType T) {

  assert(!isa<UnknownVal>(L) && "location unknown");
  assert(!isa<UndefinedVal>(L) && "location undefined");

  // FIXME: Is this even possible?  Shouldn't this be treated as a null
  //  dereference at a higher level?
  if (isa<loc::ConcreteInt>(L))
    return UndefinedVal();

  const MemRegion *MR = cast<loc::MemRegionVal>(L).getRegion();

  // FIXME: return symbolic value for these cases.
  // Example:
  // void f(int* p) { int x = *p; }
  // char* p = alloca();
  // read(p);
  // c = *p;
  if (isa<SymbolicRegion>(MR) || isa<AllocaRegion>(MR))
    return UnknownVal();

  // FIXME: Perhaps this method should just take a 'const MemRegion*' argument
  //  instead of 'Loc', and have the other Loc cases handled at a higher level.
  const TypedRegion *R = cast<TypedRegion>(MR);
  assert(R && "bad region");

  // FIXME: We should eventually handle funny addressing.  e.g.:
  //
  //   int x = ...;
  //   int *p = &x;
  //   char *q = (char*) p;
  //   char c = *q;  // returns the first byte of 'x'.
  //
  // Such funny addressing will occur due to layering of regions.

  QualType RTy = R->getValueType(getContext());

  if (RTy->isStructureType())
    return RetrieveStruct(state, R);

  if (RTy->isArrayType())
    return RetrieveArray(state, R);

  // FIXME: handle Vector types.
  if (RTy->isVectorType())
      return UnknownVal();

  if (const FieldRegion* FR = dyn_cast<FieldRegion>(R))
    return RetrieveField(state, FR);

  if (const ElementRegion* ER = dyn_cast<ElementRegion>(R))
    return RetrieveElement(state, ER);
  
  RegionBindingsTy B = GetRegionBindings(state->getStore());
  RegionBindingsTy::data_type* V = B.lookup(R);

  // Check if the region has a binding.
  if (V)
    return *V;

  if (const ObjCIvarRegion *IVR = dyn_cast<ObjCIvarRegion>(R)) {
    const MemRegion *SR = IVR->getSuperRegion();

    // If the super region is 'self' then return the symbol representing
    // the value of the ivar upon entry to the method.
    if (SR == SelfRegion) {
      // FIXME: Do we need to handle the case where the super region
      // has a view?  We want to canonicalize the bindings.
      return ValMgr.getRegionValueSymbolVal(R);
    }
    
    // Otherwise, we need a new symbol.  For now return Unknown.
    return UnknownVal();
  }

  // The location does not have a bound value.  This means that it has
  // the value it had upon its creation and/or entry to the analyzed
  // function/method.  These are either symbolic values or 'undefined'.

  // We treat function parameters as symbolic values.
  if (const VarRegion* VR = dyn_cast<VarRegion>(R)) {
    const VarDecl *VD = VR->getDecl();
    
    if (VD == SelfDecl)
      return loc::MemRegionVal(getSelfRegion(0));
    
    if (VR->hasGlobalsOrParametersStorage())
      return ValMgr.getRegionValueSymbolValOrUnknown(VR, VD->getType());
  }  

  if (R->hasHeapOrStackStorage()) {
    // All stack variables are considered to have undefined values
    // upon creation.  All heap allocated blocks are considered to
    // have undefined values as well unless they are explicitly bound
    // to specific values.
    return UndefinedVal();
  }

  // If the region is already cast to another type, use that type to create the
  // symbol value.
  if (const QualType *p = state->get<RegionCasts>(R)) {
    QualType T = *p;
    RTy = T->getAsPointerType()->getPointeeType();
  }

  // All other values are symbolic.
  return ValMgr.getRegionValueSymbolValOrUnknown(R, RTy);
}

SVal RegionStoreManager::RetrieveElement(const GRState* state,
                                         const ElementRegion* R) {
  // Check if the region has a binding.
  RegionBindingsTy B = GetRegionBindings(state->getStore());
  if (const SVal* V = B.lookup(R))
    return *V;

  const MemRegion* superR = R->getSuperRegion();

  // Check if the region is an element region of a string literal.
  if (const StringRegion *StrR=dyn_cast<StringRegion>(superR)) {
    const StringLiteral *Str = StrR->getStringLiteral();
    SVal Idx = R->getIndex();
    if (nonloc::ConcreteInt *CI = dyn_cast<nonloc::ConcreteInt>(&Idx)) {
      int64_t i = CI->getValue().getSExtValue();
      char c;
      if (i == Str->getByteLength())
        c = '\0';
      else
        c = Str->getStrData()[i];
      return ValMgr.makeIntVal(c, getContext().CharTy);
    }
  }

  // Check if the super region has a default value.
  if (const SVal *D = state->get<RegionDefaultValue>(superR)) {
    if (D->hasConjuredSymbol())
      return ValMgr.getRegionValueSymbolVal(R);
    else
      return *D;
  }

  // Check if the super region has a binding.
  if (B.lookup(superR)) {
    // We do not extract the bit value from super region for now.
    return UnknownVal();
  }
  
  if (R->hasHeapStorage()) {
    // FIXME: If the region has heap storage and we know nothing special
    // about its bindings, should we instead return UnknownVal?  Seems like
    // we should only return UndefinedVal in the cases where we know the value
    // will be undefined.
    return UndefinedVal();
  }

  if (R->hasStackStorage() && !R->hasParametersStorage()) {
    // Currently we don't reason specially about Clang-style vectors.  Check
    // if superR is a vector and if so return Unknown.
    if (const TypedRegion *typedSuperR = dyn_cast<TypedRegion>(superR)) {
      if (typedSuperR->getValueType(getContext())->isVectorType())
        return UnknownVal();
    }

    return UndefinedVal();
  }

  QualType Ty = R->getValueType(getContext());

  // If the region is already cast to another type, use that type to create the
  // symbol value.
  if (const QualType *p = state->get<RegionCasts>(R))
    Ty = (*p)->getAsPointerType()->getPointeeType();

  return ValMgr.getRegionValueSymbolValOrUnknown(R, Ty);
}

SVal RegionStoreManager::RetrieveField(const GRState* state, 
                                       const FieldRegion* R) {
  QualType Ty = R->getValueType(getContext());

  // Check if the region has a binding.
  RegionBindingsTy B = GetRegionBindings(state->getStore());
  if (const SVal* V = B.lookup(R))
    return *V;

  const MemRegion* superR = R->getSuperRegion();
  if (const SVal* D = state->get<RegionDefaultValue>(superR)) {
    if (D->hasConjuredSymbol())
      return ValMgr.getRegionValueSymbolVal(R);

    if (D->isZeroConstant())
      return ValMgr.makeZeroVal(Ty);

    if (D->isUnknown())
      return *D;

    assert(0 && "Unknown default value");
  }

  // FIXME: Is this correct?  Should it be UnknownVal?
  if (R->hasHeapStorage())
    return UndefinedVal();
  
  if (R->hasStackStorage() && !R->hasParametersStorage())
    return UndefinedVal();

  // If the region is already cast to another type, use that type to create the
  // symbol value.
  if (const QualType *p = state->get<RegionCasts>(R)) {
    QualType tmp = *p;
    Ty = tmp->getAsPointerType()->getPointeeType();
  }

  // All other values are symbolic.
  return ValMgr.getRegionValueSymbolValOrUnknown(R, Ty);
}

SVal RegionStoreManager::RetrieveStruct(const GRState *state, 
					const TypedRegion* R){
  QualType T = R->getValueType(getContext());
  assert(T->isStructureType());

  const RecordType* RT = T->getAsStructureType();
  RecordDecl* RD = RT->getDecl();
  assert(RD->isDefinition());

  llvm::ImmutableList<SVal> StructVal = getBasicVals().getEmptySValList();

  // FIXME: We shouldn't use a std::vector.  If RecordDecl doesn't have a
  // reverse iterator, we should implement one.
  std::vector<FieldDecl *> Fields(RD->field_begin(), RD->field_end());

  for (std::vector<FieldDecl *>::reverse_iterator Field = Fields.rbegin(),
                                               FieldEnd = Fields.rend();
       Field != FieldEnd; ++Field) {
    FieldRegion* FR = MRMgr.getFieldRegion(*Field, R);
    QualType FTy = (*Field)->getType();
    SVal FieldValue = Retrieve(state, loc::MemRegionVal(FR), FTy);
    StructVal = getBasicVals().consVals(FieldValue, StructVal);
  }

  return ValMgr.makeCompoundVal(T, StructVal);
}

SVal RegionStoreManager::RetrieveArray(const GRState *state,
                                       const TypedRegion * R) {

  QualType T = R->getValueType(getContext());
  ConstantArrayType* CAT = cast<ConstantArrayType>(T.getTypePtr());

  llvm::ImmutableList<SVal> ArrayVal = getBasicVals().getEmptySValList();
  llvm::APSInt Size(CAT->getSize(), false);
  llvm::APSInt i = getBasicVals().getZeroWithPtrWidth(false);

  for (; i < Size; ++i) {
    SVal Idx = ValMgr.makeIntVal(i);
    ElementRegion* ER = MRMgr.getElementRegion(CAT->getElementType(), Idx, R,
					       getContext());
    QualType ETy = ER->getElementType();
    SVal ElementVal = Retrieve(state, loc::MemRegionVal(ER), ETy);
    ArrayVal = getBasicVals().consVals(ElementVal, ArrayVal);
  }

  return ValMgr.makeCompoundVal(T, ArrayVal);
}

//===----------------------------------------------------------------------===//
// Binding values to regions.
//===----------------------------------------------------------------------===//

Store RegionStoreManager::Remove(Store store, Loc L) {
  const MemRegion* R = 0;
  
  if (isa<loc::MemRegionVal>(L))
    R = cast<loc::MemRegionVal>(L).getRegion();
  
  if (R) {
    RegionBindingsTy B = GetRegionBindings(store);  
    return RBFactory.Remove(B, R).getRoot();
  }
  
  return store;
}

const GRState *RegionStoreManager::Bind(const GRState *state, Loc L, SVal V) {
  if (isa<loc::ConcreteInt>(L))
    return state;

  // If we get here, the location should be a region.
  const MemRegion* R = cast<loc::MemRegionVal>(L).getRegion();
  
  // Check if the region is a struct region.
  if (const TypedRegion* TR = dyn_cast<TypedRegion>(R))
    if (TR->getValueType(getContext())->isStructureType())
      return BindStruct(state, TR, V);
  
  RegionBindingsTy B = GetRegionBindings(state->getStore());
  
  B = RBFactory.Add(B, R, V);
  
  return state->makeWithStore(B.getRoot());
}

const GRState *RegionStoreManager::BindDecl(const GRState *state, 
                                            const VarDecl* VD, SVal InitVal) {

  QualType T = VD->getType();
  VarRegion* VR = MRMgr.getVarRegion(VD);

  if (T->isArrayType())
    return BindArray(state, VR, InitVal);
  if (T->isStructureType())
    return BindStruct(state, VR, InitVal);

  return Bind(state, ValMgr.makeLoc(VR), InitVal);
}

// FIXME: this method should be merged into Bind().
const GRState *
RegionStoreManager::BindCompoundLiteral(const GRState *state,
                                        const CompoundLiteralExpr* CL,
                                        SVal V) {
  
  CompoundLiteralRegion* R = MRMgr.getCompoundLiteralRegion(CL);
  return Bind(state, loc::MemRegionVal(R), V);
}

const GRState *RegionStoreManager::BindArray(const GRState *state,
                                              const TypedRegion* R,
                                             SVal Init) {

  QualType T = R->getValueType(getContext());
  ConstantArrayType* CAT = cast<ConstantArrayType>(T.getTypePtr());
  QualType ElementTy = CAT->getElementType();

  llvm::APSInt Size(CAT->getSize(), false);
  llvm::APSInt i(llvm::APInt::getNullValue(Size.getBitWidth()), false);

  // Check if the init expr is a StringLiteral.
  if (isa<loc::MemRegionVal>(Init)) {
    const MemRegion* InitR = cast<loc::MemRegionVal>(Init).getRegion();
    const StringLiteral* S = cast<StringRegion>(InitR)->getStringLiteral();
    const char* str = S->getStrData();
    unsigned len = S->getByteLength();
    unsigned j = 0;

    // Copy bytes from the string literal into the target array. Trailing bytes
    // in the array that are not covered by the string literal are initialized
    // to zero.
    for (; i < Size; ++i, ++j) {
      if (j >= len)
        break;

      SVal Idx = ValMgr.makeIntVal(i);
      ElementRegion* ER = MRMgr.getElementRegion(ElementTy, Idx,R,getContext());

      SVal V = ValMgr.makeIntVal(str[j], sizeof(char)*8, true);
      state = Bind(state, loc::MemRegionVal(ER), V);
    }

    return state;
  }

  nonloc::CompoundVal& CV = cast<nonloc::CompoundVal>(Init);
  nonloc::CompoundVal::iterator VI = CV.begin(), VE = CV.end();

  for (; i < Size; ++i, ++VI) {
    // The init list might be shorter than the array length.
    if (VI == VE)
      break;

    SVal Idx = ValMgr.makeIntVal(i);
    ElementRegion* ER = MRMgr.getElementRegion(ElementTy, Idx, R, getContext());

    if (CAT->getElementType()->isStructureType())
      state = BindStruct(state, ER, *VI);
    else
      state = Bind(state, ValMgr.makeLoc(ER), *VI);
  }

  // If the init list is shorter than the array length, set the array default
  // value.
  if (i < Size) {
    if (ElementTy->isIntegerType()) {
      SVal V = ValMgr.makeZeroVal(ElementTy);
      state = setDefaultValue(state, R, V);
    }
  }

  return state;
}

const GRState *
RegionStoreManager::BindStruct(const GRState *state, const TypedRegion* R,
                               SVal V) {
  
  if (!Features.supportsFields())
    return state;
  
  QualType T = R->getValueType(getContext());
  assert(T->isStructureType());

  const RecordType* RT = T->getAsRecordType();
  RecordDecl* RD = RT->getDecl();

  if (!RD->isDefinition())
    return state;

  // We may get non-CompoundVal accidentally due to imprecise cast logic.
  // Ignore them and kill the field values.
  if (V.isUnknown() || !isa<nonloc::CompoundVal>(V))
    return KillStruct(state, R);

  nonloc::CompoundVal& CV = cast<nonloc::CompoundVal>(V);
  nonloc::CompoundVal::iterator VI = CV.begin(), VE = CV.end();

  RecordDecl::field_iterator FI, FE;

  for (FI = RD->field_begin(), FE = RD->field_end(); FI != FE; ++FI, ++VI) {

    if (VI == VE)
      break;

    QualType FTy = (*FI)->getType();
    FieldRegion* FR = MRMgr.getFieldRegion(*FI, R);

    if (Loc::IsLocType(FTy) || FTy->isIntegerType())
      state = Bind(state, ValMgr.makeLoc(FR), *VI);    
    else if (FTy->isArrayType())
      state = BindArray(state, FR, *VI);
    else if (FTy->isStructureType())
      state = BindStruct(state, FR, *VI);
  }

  // There may be fewer values in the initialize list than the fields of struct.
  if (FI != FE)
    state = setDefaultValue(state, R, ValMgr.makeIntVal(0, false));

  return state;
}

const GRState *RegionStoreManager::KillStruct(const GRState *state,
                                              const TypedRegion* R){

  // Set the default value of the struct region to "unknown".
  state = state->set<RegionDefaultValue>(R, UnknownVal());

  // Remove all bindings for the subregions of the struct.
  Store store = state->getStore();
  RegionBindingsTy B = GetRegionBindings(store);
  for (RegionBindingsTy::iterator I = B.begin(), E = B.end(); I != E; ++I) {
    const MemRegion* R = I.getKey();
    if (const SubRegion* subRegion = dyn_cast<SubRegion>(R))
      if (subRegion->isSubRegionOf(R))
        store = Remove(store, ValMgr.makeLoc(subRegion));
  }

  return state->makeWithStore(store);
}

//===----------------------------------------------------------------------===//
// Region views.
//===----------------------------------------------------------------------===//

const GRState *RegionStoreManager::AddRegionView(const GRState *state,
                                             const MemRegion* View,
                                             const MemRegion* Base) {

  // First, retrieve the region view of the base region.
  const RegionViews* d = state->get<RegionViewMap>(Base);
  RegionViews L = d ? *d : RVFactory.GetEmptySet();

  // Now add View to the region view.
  L = RVFactory.Add(L, View);

  // Create a new state with the new region view.
  return state->set<RegionViewMap>(Base, L);
}

const GRState *RegionStoreManager::RemoveRegionView(const GRState *state,
                                                const MemRegion* View,
                                                const MemRegion* Base) {
  // Retrieve the region view of the base region.
  const RegionViews* d = state->get<RegionViewMap>(Base);

  // If the base region has no view, return.
  if (!d)
    return state;

  // Remove the view.
  return state->set<RegionViewMap>(Base, RVFactory.Remove(*d, View));
}

const GRState *RegionStoreManager::setCastType(const GRState *state, 
					       const MemRegion* R, QualType T) {
  return state->set<RegionCasts>(R, T);
}

const GRState *RegionStoreManager::setDefaultValue(const GRState *state,
                                               const MemRegion* R, SVal V) {
  return state->set<RegionDefaultValue>(R, V);
}

//===----------------------------------------------------------------------===//
// State pruning.
//===----------------------------------------------------------------------===//

static void UpdateLiveSymbols(SVal X, SymbolReaper& SymReaper) {
  if (loc::MemRegionVal *XR = dyn_cast<loc::MemRegionVal>(&X)) {
    const MemRegion *R = XR->getRegion();
    
    while (R) {
      if (const SymbolicRegion *SR = dyn_cast<SymbolicRegion>(R)) {
        SymReaper.markLive(SR->getSymbol());
        return;
      }
      
      if (const SubRegion *SR = dyn_cast<SubRegion>(R)) {
        R = SR->getSuperRegion();
        continue;
      }
      
      break;
    }
    
    return;
  }
  
  for (SVal::symbol_iterator SI=X.symbol_begin(), SE=X.symbol_end();SI!=SE;++SI)
    SymReaper.markLive(*SI);
}

Store RegionStoreManager::RemoveDeadBindings(const GRState *state, Stmt* Loc, 
                                             SymbolReaper& SymReaper,
                           llvm::SmallVectorImpl<const MemRegion*>& RegionRoots)
{  
  Store store = state->getStore();
  RegionBindingsTy B = GetRegionBindings(store);
  
  // Lazily constructed backmap from MemRegions to SubRegions.
  typedef llvm::ImmutableSet<const MemRegion*> SubRegionsTy;
  typedef llvm::ImmutableMap<const MemRegion*, SubRegionsTy> SubRegionsMapTy;
  
  // FIXME: As a future optimization we can modifiy BumpPtrAllocator to have
  // the ability to reuse memory.  This way we can keep TmpAlloc around as
  // an instance variable of RegionStoreManager (avoiding repeated malloc
  // overhead).
  llvm::BumpPtrAllocator TmpAlloc;
  
  // Factory objects.
  SubRegionsMapTy::Factory SubRegMapF(TmpAlloc);
  SubRegionsTy::Factory SubRegF(TmpAlloc);
  
  // The backmap from regions to subregions.
  SubRegionsMapTy SubRegMap = SubRegMapF.GetEmptyMap();
  
  // Do a pass over the regions in the store.  For VarRegions we check if
  // the variable is still live and if so add it to the list of live roots.
  // For other regions we populate our region backmap.  
  llvm::SmallVector<const MemRegion*, 10> IntermediateRoots;
  
  for (RegionBindingsTy::iterator I = B.begin(), E = B.end(); I != E; ++I) {
    IntermediateRoots.push_back(I.getKey());
  }
  
  while (!IntermediateRoots.empty()) {
    const MemRegion* R = IntermediateRoots.back();
    IntermediateRoots.pop_back();
    
    if (const VarRegion* VR = dyn_cast<VarRegion>(R)) {
      if (SymReaper.isLive(Loc, VR->getDecl())) {
        RegionRoots.push_back(VR); // This is a live "root".
      }
    } 
    else if (const SymbolicRegion* SR = dyn_cast<SymbolicRegion>(R)) {
      if (SymReaper.isLive(SR->getSymbol()))
        RegionRoots.push_back(SR);
    }
    else {
      // Get the super region for R.
      const MemRegion* superR = cast<SubRegion>(R)->getSuperRegion();
      
      // Get the current set of subregions for SuperR.
      const SubRegionsTy* SRptr = SubRegMap.lookup(superR);
      SubRegionsTy SRs = SRptr ? *SRptr : SubRegF.GetEmptySet();
      
      // Add R to the subregions of SuperR.
      SubRegMap = SubRegMapF.Add(SubRegMap, superR, SubRegF.Add(SRs, R));
      
      // Super region may be VarRegion or subregion of another VarRegion. Add it
      // to the work list.
      if (isa<SubRegion>(superR))
        IntermediateRoots.push_back(superR);
    }
  }
  
  // Process the worklist of RegionRoots.  This performs a "mark-and-sweep"
  // of the store.  We want to find all live symbols and dead regions.  
  llvm::SmallPtrSet<const MemRegion*, 10> Marked;
  
  while (!RegionRoots.empty()) {
    // Dequeue the next region on the worklist.
    const MemRegion* R = RegionRoots.back();
    RegionRoots.pop_back();
    
    // Check if we have already processed this region.
    if (Marked.count(R)) continue;
    
    // Mark this region as processed.  This is needed for termination in case
    // a region is referenced more than once.
    Marked.insert(R);
    
    // Mark the symbol for any live SymbolicRegion as "live".  This means we
    // should continue to track that symbol.
    if (const SymbolicRegion* SymR = dyn_cast<SymbolicRegion>(R))
      SymReaper.markLive(SymR->getSymbol());
    
    // Get the data binding for R (if any).
    RegionBindingsTy::data_type* Xptr = B.lookup(R);
    if (Xptr) {
      SVal X = *Xptr;
      UpdateLiveSymbols(X, SymReaper); // Update the set of live symbols.
      
      // If X is a region, then add it to the RegionRoots.
      if (const MemRegion *RX = X.getAsRegion()) {
        RegionRoots.push_back(RX);

        // Mark the super region of the RX as live.
        // e.g.: int x; char *y = (char*) &x; if (*y) ... 
        // 'y' => element region. 'x' is its super region.
        // We only add one level super region for now.
        // FIXME: maybe multiple level of super regions should be added.
        if (const SubRegion *SR = dyn_cast<SubRegion>(RX)) {
          RegionRoots.push_back(SR->getSuperRegion());
        }
      }
    }
    
    // Get the subregions of R.  These are RegionRoots as well since they
    // represent values that are also bound to R.
    const SubRegionsTy* SRptr = SubRegMap.lookup(R);      
    if (!SRptr) continue;
    SubRegionsTy SR = *SRptr;
    
    for (SubRegionsTy::iterator I=SR.begin(), E=SR.end(); I!=E; ++I)
      RegionRoots.push_back(*I);

  }
  
  // We have now scanned the store, marking reachable regions and symbols
  // as live.  We now remove all the regions that are dead from the store
  // as well as update DSymbols with the set symbols that are now dead.  
  for (RegionBindingsTy::iterator I = B.begin(), E = B.end(); I != E; ++I) {
    const MemRegion* R = I.getKey();
    // If this region live?  Is so, none of its symbols are dead.
    if (Marked.count(R))
      continue;
    
    // Remove this dead region from the store.
    store = Remove(store, ValMgr.makeLoc(R));
    
    // Mark all non-live symbols that this region references as dead.
    if (const SymbolicRegion* SymR = dyn_cast<SymbolicRegion>(R))
      SymReaper.maybeDead(SymR->getSymbol());
    
    SVal X = I.getData();
    SVal::symbol_iterator SI = X.symbol_begin(), SE = X.symbol_end();
    for (; SI != SE; ++SI) SymReaper.maybeDead(*SI);
  }
  
  return store;
}

//===----------------------------------------------------------------------===//
// Utility methods.
//===----------------------------------------------------------------------===//

void RegionStoreManager::print(Store store, llvm::raw_ostream& OS,
                               const char* nl, const char *sep) {
  RegionBindingsTy B = GetRegionBindings(store);
  OS << "Store:" << nl;
  
  for (RegionBindingsTy::iterator I = B.begin(), E = B.end(); I != E; ++I) {
    OS << ' '; I.getKey()->print(OS); OS << " : ";
    I.getData().print(OS); OS << nl;
  }
}
