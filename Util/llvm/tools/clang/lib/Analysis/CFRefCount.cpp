// CFRefCount.cpp - Transfer functions for tracking simple values -*- C++ -*--//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the methods for CFRefCount, which implements
//  a reference count checker for Core Foundation (Mac OS X).
//
//===----------------------------------------------------------------------===//

#include "GRSimpleVals.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Analysis/PathSensitive/GRState.h"
#include "clang/Analysis/PathSensitive/GRStateTrait.h"
#include "clang/Analysis/PathDiagnostic.h"
#include "clang/Analysis/LocalCheckers.h"
#include "clang/Analysis/PathDiagnostic.h"
#include "clang/Analysis/PathSensitive/BugReporter.h"
#include "clang/AST/DeclObjC.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/ImmutableMap.h"
#include "llvm/ADT/ImmutableList.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Compiler.h"
#include "llvm/ADT/STLExtras.h"
#include <ostream>
#include <sstream>
#include <stdarg.h>

using namespace clang;

//===----------------------------------------------------------------------===//
// Utility functions.
//===----------------------------------------------------------------------===//

using llvm::CStrInCStrNoCase;

// The "fundamental rule" for naming conventions of methods:
//  (url broken into two lines)
//  http://developer.apple.com/documentation/Cocoa/Conceptual/
//     MemoryMgmt/Tasks/MemoryManagementRules.html
//
// "You take ownership of an object if you create it using a method whose name
//  begins with “alloc” or “new” or contains “copy” (for example, alloc, 
//  newObject, or mutableCopy), or if you send it a retain message. You are
//  responsible for relinquishing ownership of objects you own using release
//  or autorelease. Any other time you receive an object, you must
//  not release it."
//
static bool followsFundamentalRule(const char* s) {
  while (*s == '_') ++s;  
  return CStrInCStrNoCase(s, "copy")
      || CStrInCStrNoCase(s, "new") == s 
      || CStrInCStrNoCase(s, "alloc") == s;
}

static bool followsReturnRule(const char* s) {
  while (*s == '_') ++s;  
  return followsFundamentalRule(s) || CStrInCStrNoCase(s, "init") == s;
}

//===----------------------------------------------------------------------===//
// Selector creation functions.
//===----------------------------------------------------------------------===//

static inline Selector GetNullarySelector(const char* name, ASTContext& Ctx) {
  IdentifierInfo* II = &Ctx.Idents.get(name);
  return Ctx.Selectors.getSelector(0, &II);
}

static inline Selector GetUnarySelector(const char* name, ASTContext& Ctx) {
  IdentifierInfo* II = &Ctx.Idents.get(name);
  return Ctx.Selectors.getSelector(1, &II);
}

//===----------------------------------------------------------------------===//
// Type querying functions.
//===----------------------------------------------------------------------===//

static bool hasPrefix(const char* s, const char* prefix) {
  if (!prefix)
    return true;
  
  char c = *s;
  char cP = *prefix;
  
  while (c != '\0' && cP != '\0') {
    if (c != cP) break;
    c = *(++s);
    cP = *(++prefix);
  }
  
  return cP == '\0';
}

static bool hasSuffix(const char* s, const char* suffix) {
  const char* loc = strstr(s, suffix);
  return loc && strcmp(suffix, loc) == 0;
}

static bool isRefType(QualType RetTy, const char* prefix,
                      ASTContext* Ctx = 0, const char* name = 0) {
  
  if (TypedefType* TD = dyn_cast<TypedefType>(RetTy.getTypePtr())) {
    const char* TDName = TD->getDecl()->getIdentifier()->getName();
    return hasPrefix(TDName, prefix) && hasSuffix(TDName, "Ref");
  }

  if (!Ctx || !name)
    return false;

  // Is the type void*?
  const PointerType* PT = RetTy->getAsPointerType();
  if (!(PT->getPointeeType().getUnqualifiedType() == Ctx->VoidTy))
    return false;

  // Does the name start with the prefix?
  return hasPrefix(name, prefix);
}

//===----------------------------------------------------------------------===//
// Primitives used for constructing summaries for function/method calls.
//===----------------------------------------------------------------------===//

namespace {
/// ArgEffect is used to summarize a function/method call's effect on a
/// particular argument.
enum ArgEffect { IncRef, DecRef, DoNothing, DoNothingByRef,
                 StopTracking, MayEscape, SelfOwn, Autorelease };

/// ArgEffects summarizes the effects of a function/method call on all of
/// its arguments.
typedef std::vector<std::pair<unsigned,ArgEffect> > ArgEffects;
}

namespace llvm {
template <> struct FoldingSetTrait<ArgEffects> {
  static void Profile(const ArgEffects& X, FoldingSetNodeID& ID) {
    for (ArgEffects::const_iterator I = X.begin(), E = X.end(); I!= E; ++I) {
      ID.AddInteger(I->first);
      ID.AddInteger((unsigned) I->second);
    }
  }    
};
} // end llvm namespace

namespace {

///  RetEffect is used to summarize a function/method call's behavior with
///  respect to its return value.  
class VISIBILITY_HIDDEN RetEffect {
public:
  enum Kind { NoRet, Alias, OwnedSymbol, OwnedAllocatedSymbol,
              NotOwnedSymbol, ReceiverAlias };
    
  enum ObjKind { CF, ObjC, AnyObj };  

private:
  Kind K;
  ObjKind O;
  unsigned index;

  RetEffect(Kind k, unsigned idx = 0) : K(k), O(AnyObj), index(idx) {}
  RetEffect(Kind k, ObjKind o) : K(k), O(o), index(0) {}
  
public:
  Kind getKind() const { return K; }

  ObjKind getObjKind() const { return O; }
  
  unsigned getIndex() const { 
    assert(getKind() == Alias);
    return index;
  }
  
  static RetEffect MakeAlias(unsigned Idx) {
    return RetEffect(Alias, Idx);
  }
  static RetEffect MakeReceiverAlias() {
    return RetEffect(ReceiverAlias);
  }  
  static RetEffect MakeOwned(ObjKind o, bool isAllocated = false) {
    return RetEffect(isAllocated ? OwnedAllocatedSymbol : OwnedSymbol, o);
  }  
  static RetEffect MakeNotOwned(ObjKind o) {
    return RetEffect(NotOwnedSymbol, o);
  }  
  static RetEffect MakeNoRet() {
    return RetEffect(NoRet);
  }
  
  void Profile(llvm::FoldingSetNodeID& ID) const {
    ID.AddInteger((unsigned)K);
    ID.AddInteger((unsigned)O);
    ID.AddInteger(index);
  }
};
  
  
class VISIBILITY_HIDDEN RetainSummary : public llvm::FoldingSetNode {
  /// Args - an ordered vector of (index, ArgEffect) pairs, where index
  ///  specifies the argument (starting from 0).  This can be sparsely
  ///  populated; arguments with no entry in Args use 'DefaultArgEffect'.
  ArgEffects* Args;
  
  /// DefaultArgEffect - The default ArgEffect to apply to arguments that
  ///  do not have an entry in Args.
  ArgEffect   DefaultArgEffect;
  
  /// Receiver - If this summary applies to an Objective-C message expression,
  ///  this is the effect applied to the state of the receiver.
  ArgEffect   Receiver;
  
  /// Ret - The effect on the return value.  Used to indicate if the
  ///  function/method call returns a new tracked symbol, returns an
  ///  alias of one of the arguments in the call, and so on.
  RetEffect   Ret;
  
  /// EndPath - Indicates that execution of this method/function should
  ///  terminate the simulation of a path.
  bool EndPath;
  
public:
  
  RetainSummary(ArgEffects* A, RetEffect R, ArgEffect defaultEff,
                ArgEffect ReceiverEff, bool endpath = false)
    : Args(A), DefaultArgEffect(defaultEff), Receiver(ReceiverEff), Ret(R),
      EndPath(endpath) {}  
  
  /// getArg - Return the argument effect on the argument specified by
  ///  idx (starting from 0).
  ArgEffect getArg(unsigned idx) const {

    if (!Args)
      return DefaultArgEffect;
    
    // If Args is present, it is likely to contain only 1 element.
    // Just do a linear search.  Do it from the back because functions with
    // large numbers of arguments will be tail heavy with respect to which
    // argument they actually modify with respect to the reference count.    
    for (ArgEffects::reverse_iterator I=Args->rbegin(), E=Args->rend();
           I!=E; ++I) {
      
      if (idx > I->first)
        return DefaultArgEffect;
      
      if (idx == I->first)
        return I->second;
    }
    
    return DefaultArgEffect;
  }
  
  /// getRetEffect - Returns the effect on the return value of the call.
  RetEffect getRetEffect() const {
    return Ret;
  }
  
  /// isEndPath - Returns true if executing the given method/function should
  ///  terminate the path.
  bool isEndPath() const { return EndPath; }
  
  /// getReceiverEffect - Returns the effect on the receiver of the call.
  ///  This is only meaningful if the summary applies to an ObjCMessageExpr*.
  ArgEffect getReceiverEffect() const {
    return Receiver;
  }
  
  typedef ArgEffects::const_iterator ExprIterator;
  
  ExprIterator begin_args() const { return Args->begin(); }
  ExprIterator end_args()   const { return Args->end(); }
  
  static void Profile(llvm::FoldingSetNodeID& ID, ArgEffects* A,
                      RetEffect RetEff, ArgEffect DefaultEff,
                      ArgEffect ReceiverEff, bool EndPath) {
    ID.AddPointer(A);
    ID.Add(RetEff);
    ID.AddInteger((unsigned) DefaultEff);
    ID.AddInteger((unsigned) ReceiverEff);
    ID.AddInteger((unsigned) EndPath);
  }
      
  void Profile(llvm::FoldingSetNodeID& ID) const {
    Profile(ID, Args, Ret, DefaultArgEffect, Receiver, EndPath);
  }
};
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Data structures for constructing summaries.
//===----------------------------------------------------------------------===//

namespace {
class VISIBILITY_HIDDEN ObjCSummaryKey {
  IdentifierInfo* II;
  Selector S;
public:    
  ObjCSummaryKey(IdentifierInfo* ii, Selector s)
    : II(ii), S(s) {}

  ObjCSummaryKey(ObjCInterfaceDecl* d, Selector s)
    : II(d ? d->getIdentifier() : 0), S(s) {}
  
  ObjCSummaryKey(Selector s)
    : II(0), S(s) {}
  
  IdentifierInfo* getIdentifier() const { return II; }
  Selector getSelector() const { return S; }
};
}

namespace llvm {
template <> struct DenseMapInfo<ObjCSummaryKey> {
  static inline ObjCSummaryKey getEmptyKey() {
    return ObjCSummaryKey(DenseMapInfo<IdentifierInfo*>::getEmptyKey(),
                          DenseMapInfo<Selector>::getEmptyKey());
  }
    
  static inline ObjCSummaryKey getTombstoneKey() {
    return ObjCSummaryKey(DenseMapInfo<IdentifierInfo*>::getTombstoneKey(),
                          DenseMapInfo<Selector>::getTombstoneKey());      
  }
  
  static unsigned getHashValue(const ObjCSummaryKey &V) {
    return (DenseMapInfo<IdentifierInfo*>::getHashValue(V.getIdentifier())
            & 0x88888888) 
        | (DenseMapInfo<Selector>::getHashValue(V.getSelector())
            & 0x55555555);
  }
  
  static bool isEqual(const ObjCSummaryKey& LHS, const ObjCSummaryKey& RHS) {
    return DenseMapInfo<IdentifierInfo*>::isEqual(LHS.getIdentifier(),
                                                  RHS.getIdentifier()) &&
           DenseMapInfo<Selector>::isEqual(LHS.getSelector(),
                                           RHS.getSelector());
  }
  
  static bool isPod() {
    return DenseMapInfo<ObjCInterfaceDecl*>::isPod() &&
           DenseMapInfo<Selector>::isPod();
  }
};
} // end llvm namespace
  
namespace {
class VISIBILITY_HIDDEN ObjCSummaryCache {
  typedef llvm::DenseMap<ObjCSummaryKey, RetainSummary*> MapTy;
  MapTy M;
public:
  ObjCSummaryCache() {}
  
  typedef MapTy::iterator iterator;
  
  iterator find(ObjCInterfaceDecl* D, Selector S) {
    
    // Do a lookup with the (D,S) pair.  If we find a match return
    // the iterator.
    ObjCSummaryKey K(D, S);
    MapTy::iterator I = M.find(K);
    
    if (I != M.end() || !D)
      return I;
    
    // Walk the super chain.  If we find a hit with a parent, we'll end
    // up returning that summary.  We actually allow that key (null,S), as
    // we cache summaries for the null ObjCInterfaceDecl* to allow us to
    // generate initial summaries without having to worry about NSObject
    // being declared.
    // FIXME: We may change this at some point.
    for (ObjCInterfaceDecl* C=D->getSuperClass() ;; C=C->getSuperClass()) {
      if ((I = M.find(ObjCSummaryKey(C, S))) != M.end())
        break;
      
      if (!C)
        return I;
    }
    
    // Cache the summary with original key to make the next lookup faster 
    // and return the iterator.
    M[K] = I->second;
    return I;
  }
  

  iterator find(Expr* Receiver, Selector S) {
    return find(getReceiverDecl(Receiver), S);
  }
  
  iterator find(IdentifierInfo* II, Selector S) {
    // FIXME: Class method lookup.  Right now we dont' have a good way
    // of going between IdentifierInfo* and the class hierarchy.
    iterator I = M.find(ObjCSummaryKey(II, S));
    return I == M.end() ? M.find(ObjCSummaryKey(S)) : I;
  }
  
  ObjCInterfaceDecl* getReceiverDecl(Expr* E) {
    
    const PointerType* PT = E->getType()->getAsPointerType();
    if (!PT) return 0;
    
    ObjCInterfaceType* OI = dyn_cast<ObjCInterfaceType>(PT->getPointeeType());
    if (!OI) return 0;
    
    return OI ? OI->getDecl() : 0;
  }
  
  iterator end() { return M.end(); }
  
  RetainSummary*& operator[](ObjCMessageExpr* ME) {
    
    Selector S = ME->getSelector();
    
    if (Expr* Receiver = ME->getReceiver()) {
      ObjCInterfaceDecl* OD = getReceiverDecl(Receiver);
      return OD ? M[ObjCSummaryKey(OD->getIdentifier(), S)] : M[S];
    }
    
    return M[ObjCSummaryKey(ME->getClassName(), S)];
  }
  
  RetainSummary*& operator[](ObjCSummaryKey K) {
    return M[K];
  }
  
  RetainSummary*& operator[](Selector S) {
    return M[ ObjCSummaryKey(S) ];
  }
};   
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Data structures for managing collections of summaries.
//===----------------------------------------------------------------------===//

namespace {
class VISIBILITY_HIDDEN RetainSummaryManager {

  //==-----------------------------------------------------------------==//
  //  Typedefs.
  //==-----------------------------------------------------------------==//
  
  typedef llvm::FoldingSet<llvm::FoldingSetNodeWrapper<ArgEffects> >
          ArgEffectsSetTy;
  
  typedef llvm::FoldingSet<RetainSummary>
          SummarySetTy;
  
  typedef llvm::DenseMap<FunctionDecl*, RetainSummary*>
          FuncSummariesTy;
  
  typedef ObjCSummaryCache ObjCMethodSummariesTy;
    
  //==-----------------------------------------------------------------==//
  //  Data.
  //==-----------------------------------------------------------------==//
  
  /// Ctx - The ASTContext object for the analyzed ASTs.
  ASTContext& Ctx;

  /// CFDictionaryCreateII - An IdentifierInfo* representing the indentifier
  ///  "CFDictionaryCreate".
  IdentifierInfo* CFDictionaryCreateII;
  
  /// GCEnabled - Records whether or not the analyzed code runs in GC mode.
  const bool GCEnabled;
  
  /// SummarySet - A FoldingSet of uniqued summaries.
  SummarySetTy SummarySet;
  
  /// FuncSummaries - A map from FunctionDecls to summaries.
  FuncSummariesTy FuncSummaries; 
  
  /// ObjCClassMethodSummaries - A map from selectors (for instance methods)
  ///  to summaries.
  ObjCMethodSummariesTy ObjCClassMethodSummaries;

  /// ObjCMethodSummaries - A map from selectors to summaries.
  ObjCMethodSummariesTy ObjCMethodSummaries;

  /// ArgEffectsSet - A FoldingSet of uniqued ArgEffects.
  ArgEffectsSetTy ArgEffectsSet;
  
  /// BPAlloc - A BumpPtrAllocator used for allocating summaries, ArgEffects,
  ///  and all other data used by the checker.
  llvm::BumpPtrAllocator BPAlloc;
  
  /// ScratchArgs - A holding buffer for construct ArgEffects.
  ArgEffects ScratchArgs;
  
  RetainSummary* StopSummary;
  
  //==-----------------------------------------------------------------==//
  //  Methods.
  //==-----------------------------------------------------------------==//
  
  /// getArgEffects - Returns a persistent ArgEffects object based on the
  ///  data in ScratchArgs.
  ArgEffects*   getArgEffects();

  enum UnaryFuncKind { cfretain, cfrelease, cfmakecollectable };  
  
public:
  RetainSummary* getUnarySummary(FunctionType* FT, UnaryFuncKind func);
  
  RetainSummary* getCFSummaryCreateRule(FunctionDecl* FD);
  RetainSummary* getCFSummaryGetRule(FunctionDecl* FD);  
  RetainSummary* getCFCreateGetRuleSummary(FunctionDecl* FD, const char* FName);
  
  RetainSummary* getPersistentSummary(ArgEffects* AE, RetEffect RetEff,
                                      ArgEffect ReceiverEff = DoNothing,
                                      ArgEffect DefaultEff = MayEscape,
                                      bool isEndPath = false);

  RetainSummary* getPersistentSummary(RetEffect RE,
                                      ArgEffect ReceiverEff = DoNothing,
                                      ArgEffect DefaultEff = MayEscape) {
    return getPersistentSummary(getArgEffects(), RE, ReceiverEff, DefaultEff);
  }
  
  RetainSummary* getPersistentStopSummary() {
    if (StopSummary)
      return StopSummary;
    
    StopSummary = getPersistentSummary(RetEffect::MakeNoRet(),
                                       StopTracking, StopTracking);

    return StopSummary;
  }  

  RetainSummary* getInitMethodSummary(ObjCMessageExpr* ME);

  void InitializeClassMethodSummaries();
  void InitializeMethodSummaries();
  
  bool isTrackedObjectType(QualType T);
  
private:
  
  void addClsMethSummary(IdentifierInfo* ClsII, Selector S,
                         RetainSummary* Summ) {
    ObjCClassMethodSummaries[ObjCSummaryKey(ClsII, S)] = Summ;
  }
  
  void addNSObjectClsMethSummary(Selector S, RetainSummary *Summ) {
    ObjCClassMethodSummaries[S] = Summ;
  }
    
  void addNSObjectMethSummary(Selector S, RetainSummary *Summ) {
    ObjCMethodSummaries[S] = Summ;
  }
  
  void addInstMethSummary(const char* Cls, RetainSummary* Summ, va_list argp) {
    
    IdentifierInfo* ClsII = &Ctx.Idents.get(Cls);
    llvm::SmallVector<IdentifierInfo*, 10> II;
    
    while (const char* s = va_arg(argp, const char*))
      II.push_back(&Ctx.Idents.get(s));
    
    Selector S = Ctx.Selectors.getSelector(II.size(), &II[0]);
    ObjCMethodSummaries[ObjCSummaryKey(ClsII, S)] = Summ;
  }
  
  void addInstMethSummary(const char* Cls, RetainSummary* Summ, ...) {
    va_list argp;
    va_start(argp, Summ);
    addInstMethSummary(Cls, Summ, argp);
    va_end(argp);    
  }
          
  void addPanicSummary(const char* Cls, ...) {
    RetainSummary* Summ = getPersistentSummary(0, RetEffect::MakeNoRet(),
                                               DoNothing,  DoNothing, true);
    va_list argp;
    va_start (argp, Cls);
    addInstMethSummary(Cls, Summ, argp);
    va_end(argp);
  }  
  
public:
  
  RetainSummaryManager(ASTContext& ctx, bool gcenabled)
   : Ctx(ctx),
     CFDictionaryCreateII(&ctx.Idents.get("CFDictionaryCreate")),
     GCEnabled(gcenabled), StopSummary(0) {

    InitializeClassMethodSummaries();
    InitializeMethodSummaries();
  }
  
  ~RetainSummaryManager();
  
  RetainSummary* getSummary(FunctionDecl* FD);  
  RetainSummary* getMethodSummary(ObjCMessageExpr* ME, ObjCInterfaceDecl* ID);
  RetainSummary* getClassMethodSummary(IdentifierInfo* ClsName, Selector S);
  
  bool isGCEnabled() const { return GCEnabled; }
};
  
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Implementation of checker data structures.
//===----------------------------------------------------------------------===//

RetainSummaryManager::~RetainSummaryManager() {
  
  // FIXME: The ArgEffects could eventually be allocated from BPAlloc, 
  //   mitigating the need to do explicit cleanup of the
  //   Argument-Effect summaries.
  
  for (ArgEffectsSetTy::iterator I = ArgEffectsSet.begin(), 
                                 E = ArgEffectsSet.end(); I!=E; ++I)
    I->getValue().~ArgEffects();
}

ArgEffects* RetainSummaryManager::getArgEffects() {

  if (ScratchArgs.empty())
    return NULL;
  
  // Compute a profile for a non-empty ScratchArgs.
  llvm::FoldingSetNodeID profile;
  profile.Add(ScratchArgs);
  void* InsertPos;
  
  // Look up the uniqued copy, or create a new one.
  llvm::FoldingSetNodeWrapper<ArgEffects>* E =
    ArgEffectsSet.FindNodeOrInsertPos(profile, InsertPos);
  
  if (E) {
    ScratchArgs.clear();
    return &E->getValue();
  }
  
  E = (llvm::FoldingSetNodeWrapper<ArgEffects>*)
        BPAlloc.Allocate<llvm::FoldingSetNodeWrapper<ArgEffects> >();
                       
  new (E) llvm::FoldingSetNodeWrapper<ArgEffects>(ScratchArgs);
  ArgEffectsSet.InsertNode(E, InsertPos);

  ScratchArgs.clear();
  return &E->getValue();
}

RetainSummary*
RetainSummaryManager::getPersistentSummary(ArgEffects* AE, RetEffect RetEff,
                                           ArgEffect ReceiverEff,
                                           ArgEffect DefaultEff,
                                           bool isEndPath) {
  
  // Generate a profile for the summary.
  llvm::FoldingSetNodeID profile;
  RetainSummary::Profile(profile, AE, RetEff, DefaultEff, ReceiverEff,
                         isEndPath);
  
  // Look up the uniqued summary, or create one if it doesn't exist.
  void* InsertPos;  
  RetainSummary* Summ = SummarySet.FindNodeOrInsertPos(profile, InsertPos);
  
  if (Summ)
    return Summ;
  
  // Create the summary and return it.
  Summ = (RetainSummary*) BPAlloc.Allocate<RetainSummary>();
  new (Summ) RetainSummary(AE, RetEff, DefaultEff, ReceiverEff, isEndPath);
  SummarySet.InsertNode(Summ, InsertPos);
  
  return Summ;
}

//===----------------------------------------------------------------------===//
// Predicates.
//===----------------------------------------------------------------------===//

bool RetainSummaryManager::isTrackedObjectType(QualType T) {
  if (!Ctx.isObjCObjectPointerType(T))
    return false;

  // Does it subclass NSObject?
  ObjCInterfaceType* OT = dyn_cast<ObjCInterfaceType>(T.getTypePtr());

  // We assume that id<..>, id, and "Class" all represent tracked objects.
  if (!OT)
    return true;

  // Does the object type subclass NSObject?
  // FIXME: We can memoize here if this gets too expensive.  
  IdentifierInfo* NSObjectII = &Ctx.Idents.get("NSObject");
  ObjCInterfaceDecl* ID = OT->getDecl();  

  for ( ; ID ; ID = ID->getSuperClass())
    if (ID->getIdentifier() == NSObjectII)
      return true;
  
  return false;
}

//===----------------------------------------------------------------------===//
// Summary creation for functions (largely uses of Core Foundation).
//===----------------------------------------------------------------------===//

static bool isRetain(FunctionDecl* FD, const char* FName) {
  const char* loc = strstr(FName, "Retain");
  return loc && loc[sizeof("Retain")-1] == '\0';
}

static bool isRelease(FunctionDecl* FD, const char* FName) {
  const char* loc = strstr(FName, "Release");
  return loc && loc[sizeof("Release")-1] == '\0';
}

RetainSummary* RetainSummaryManager::getSummary(FunctionDecl* FD) {

  SourceLocation Loc = FD->getLocation();
  
  if (!Loc.isFileID())
    return NULL;
  
  // Look up a summary in our cache of FunctionDecls -> Summaries.
  FuncSummariesTy::iterator I = FuncSummaries.find(FD);

  if (I != FuncSummaries.end())
    return I->second;

  // No summary.  Generate one.
  RetainSummary *S = 0;
  
  do {
    // We generate "stop" summaries for implicitly defined functions.
    if (FD->isImplicit()) {
      S = getPersistentStopSummary();
      break;
    }
    
    // [PR 3337] Use 'getDesugaredType' to strip away any typedefs on the
    // function's type.
    FunctionType* FT = cast<FunctionType>(FD->getType()->getDesugaredType());
    const char* FName = FD->getIdentifier()->getName();
    
    // Inspect the result type.
    QualType RetTy = FT->getResultType();
    
    // FIXME: This should all be refactored into a chain of "summary lookup"
    //  filters.
    if (strcmp(FName, "IOServiceGetMatchingServices") == 0) {
      // FIXES: <rdar://problem/6326900>
      // This should be addressed using a API table.  This strcmp is also
      // a little gross, but there is no need to super optimize here.
      assert (ScratchArgs.empty());
      ScratchArgs.push_back(std::make_pair(1, DecRef));
      S = getPersistentSummary(RetEffect::MakeNoRet(), DoNothing, DoNothing);
      break;
    }
    
    // Handle: id NSMakeCollectable(CFTypeRef)
    if (strcmp(FName, "NSMakeCollectable") == 0) {
      S = (RetTy == Ctx.getObjCIdType())
          ? getUnarySummary(FT, cfmakecollectable)
          : getPersistentStopSummary();
        
      break;
    }

    if (RetTy->isPointerType()) {
      // For CoreFoundation ('CF') types.
      if (isRefType(RetTy, "CF", &Ctx, FName)) {
        if (isRetain(FD, FName))
          S = getUnarySummary(FT, cfretain);
        else if (strstr(FName, "MakeCollectable"))
          S = getUnarySummary(FT, cfmakecollectable);
        else 
          S = getCFCreateGetRuleSummary(FD, FName);

        break;
      }

      // For CoreGraphics ('CG') types.
      if (isRefType(RetTy, "CG", &Ctx, FName)) {
        if (isRetain(FD, FName))
          S = getUnarySummary(FT, cfretain);
        else
          S = getCFCreateGetRuleSummary(FD, FName);

        break;
      }

      // For the Disk Arbitration API (DiskArbitration/DADisk.h)
      if (isRefType(RetTy, "DADisk") ||
          isRefType(RetTy, "DADissenter") ||
          isRefType(RetTy, "DASessionRef")) {
        S = getCFCreateGetRuleSummary(FD, FName);
        break;
      }
      
      break;
    }

    // Check for release functions, the only kind of functions that we care
    // about that don't return a pointer type.
    if (FName[0] == 'C' && (FName[1] == 'F' || FName[1] == 'G')) {
      if (isRelease(FD, FName+2))
        S = getUnarySummary(FT, cfrelease);
      else {
        assert (ScratchArgs.empty());
        // Remaining CoreFoundation and CoreGraphics functions.
        // We use to assume that they all strictly followed the ownership idiom
        // and that ownership cannot be transferred.  While this is technically
        // correct, many methods allow a tracked object to escape.  For example:
        //
        //   CFMutableDictionaryRef x = CFDictionaryCreateMutable(...);        
        //   CFDictionaryAddValue(y, key, x);
        //   CFRelease(x); 
        //   ... it is okay to use 'x' since 'y' has a reference to it
        //
        // We handle this and similar cases with the follow heuristic.  If the
        // function name contains "InsertValue", "SetValue" or "AddValue" then
        // we assume that arguments may "escape."
        //
        ArgEffect E = (CStrInCStrNoCase(FName, "InsertValue") ||
                       CStrInCStrNoCase(FName, "AddValue") ||
                       CStrInCStrNoCase(FName, "SetValue"))
                      ? MayEscape : DoNothing;
        
        S = getPersistentSummary(RetEffect::MakeNoRet(), DoNothing, E);
      }
    }
  }
  while (0);

  FuncSummaries[FD] = S;
  return S;  
}

RetainSummary*
RetainSummaryManager::getCFCreateGetRuleSummary(FunctionDecl* FD,
                                                const char* FName) {
  
  if (strstr(FName, "Create") || strstr(FName, "Copy"))
    return getCFSummaryCreateRule(FD);
  
  if (strstr(FName, "Get"))
    return getCFSummaryGetRule(FD);
  
  return 0;
}

RetainSummary*
RetainSummaryManager::getUnarySummary(FunctionType* FT, UnaryFuncKind func) {
  // Sanity check that this is *really* a unary function.  This can
  // happen if people do weird things.
  FunctionTypeProto* FTP = dyn_cast<FunctionTypeProto>(FT);
  if (!FTP || FTP->getNumArgs() != 1)
    return getPersistentStopSummary();
  
  assert (ScratchArgs.empty());
  
  switch (func) {
    case cfretain: {      
      ScratchArgs.push_back(std::make_pair(0, IncRef));
      return getPersistentSummary(RetEffect::MakeAlias(0),
                                  DoNothing, DoNothing);
    }
      
    case cfrelease: {
      ScratchArgs.push_back(std::make_pair(0, DecRef));
      return getPersistentSummary(RetEffect::MakeNoRet(),
                                  DoNothing, DoNothing);
    }
      
    case cfmakecollectable: {
      if (GCEnabled)
        ScratchArgs.push_back(std::make_pair(0, DecRef));
      
      return getPersistentSummary(RetEffect::MakeAlias(0),
                                  DoNothing, DoNothing);    
    }
      
    default:
      assert (false && "Not a supported unary function.");
      return 0;
  }
}

RetainSummary* RetainSummaryManager::getCFSummaryCreateRule(FunctionDecl* FD) {
  assert (ScratchArgs.empty());
  
  if (FD->getIdentifier() == CFDictionaryCreateII) {
    ScratchArgs.push_back(std::make_pair(1, DoNothingByRef));
    ScratchArgs.push_back(std::make_pair(2, DoNothingByRef));
  }
  
  return getPersistentSummary(RetEffect::MakeOwned(RetEffect::CF, true));
}

RetainSummary* RetainSummaryManager::getCFSummaryGetRule(FunctionDecl* FD) {
  assert (ScratchArgs.empty());  
  return getPersistentSummary(RetEffect::MakeNotOwned(RetEffect::CF),
                              DoNothing, DoNothing);
}

//===----------------------------------------------------------------------===//
// Summary creation for Selectors.
//===----------------------------------------------------------------------===//

RetainSummary*
RetainSummaryManager::getInitMethodSummary(ObjCMessageExpr* ME) {
  assert(ScratchArgs.empty());
    
  RetainSummary* Summ =
    getPersistentSummary(RetEffect::MakeReceiverAlias());
  
  ObjCMethodSummaries[ME] = Summ;
  return Summ;
}


RetainSummary*
RetainSummaryManager::getMethodSummary(ObjCMessageExpr* ME,
                                       ObjCInterfaceDecl* ID) {

  Selector S = ME->getSelector();
  
  // Look up a summary in our summary cache.  
  ObjCMethodSummariesTy::iterator I = ObjCMethodSummaries.find(ID, S);
  
  if (I != ObjCMethodSummaries.end())
    return I->second;

  // "initXXX": pass-through for receiver.
  const char* s = S.getIdentifierInfoForSlot(0)->getName();
  assert (ScratchArgs.empty());
  
  if (strncmp(s, "init", 4) == 0 || strncmp(s, "_init", 5) == 0)
    return getInitMethodSummary(ME);
  
  // Look for methods that return an owned object.
  if (!isTrackedObjectType(Ctx.getCanonicalType(ME->getType())))
    return 0;

  if (followsFundamentalRule(s)) {    
    RetEffect E = isGCEnabled() ? RetEffect::MakeNoRet()
                                : RetEffect::MakeOwned(RetEffect::ObjC, true);
    RetainSummary* Summ = getPersistentSummary(E);
    ObjCMethodSummaries[ME] = Summ;
    return Summ;
  }
  
  return 0;
}

RetainSummary*
RetainSummaryManager::getClassMethodSummary(IdentifierInfo* ClsName,
                                            Selector S) {
  
  // FIXME: Eventually we should properly do class method summaries, but
  // it requires us being able to walk the type hierarchy.  Unfortunately,
  // we cannot do this with just an IdentifierInfo* for the class name.
  
  // Look up a summary in our cache of Selectors -> Summaries.
  ObjCMethodSummariesTy::iterator I = ObjCClassMethodSummaries.find(ClsName, S);
  
  if (I != ObjCClassMethodSummaries.end())
    return I->second;
  
  return 0;
}

void RetainSummaryManager::InitializeClassMethodSummaries() {
  
  assert (ScratchArgs.empty());
  
  RetEffect E = isGCEnabled() ? RetEffect::MakeNoRet()
                              : RetEffect::MakeOwned(RetEffect::ObjC, true);  
  
  RetainSummary* Summ = getPersistentSummary(E);
  
  // Create the summaries for "alloc", "new", and "allocWithZone:" for
  // NSObject and its derivatives.
  addNSObjectClsMethSummary(GetNullarySelector("alloc", Ctx), Summ);
  addNSObjectClsMethSummary(GetNullarySelector("new", Ctx), Summ);
  addNSObjectClsMethSummary(GetUnarySelector("allocWithZone", Ctx), Summ);
  
  // Create the [NSAssertionHandler currentHander] summary.  
  addClsMethSummary(&Ctx.Idents.get("NSAssertionHandler"),
                GetNullarySelector("currentHandler", Ctx),
                getPersistentSummary(RetEffect::MakeNotOwned(RetEffect::ObjC)));
  
  // Create the [NSAutoreleasePool addObject:] summary.
  ScratchArgs.push_back(std::make_pair(0, Autorelease));
  addClsMethSummary(&Ctx.Idents.get("NSAutoreleasePool"),
                    GetUnarySelector("addObject", Ctx),
                    getPersistentSummary(RetEffect::MakeNoRet(),
                                         DoNothing, DoNothing));
}

void RetainSummaryManager::InitializeMethodSummaries() {
  
  assert (ScratchArgs.empty());  
  
  // Create the "init" selector.  It just acts as a pass-through for the
  // receiver.
  RetainSummary* InitSumm = getPersistentSummary(RetEffect::MakeReceiverAlias());
  addNSObjectMethSummary(GetNullarySelector("init", Ctx), InitSumm);
  
  // The next methods are allocators.
  RetEffect E = isGCEnabled() ? RetEffect::MakeNoRet()
                              : RetEffect::MakeOwned(RetEffect::ObjC, true);
  
  RetainSummary* Summ = getPersistentSummary(E);  
  
  // Create the "copy" selector.  
  addNSObjectMethSummary(GetNullarySelector("copy", Ctx), Summ);  

  // Create the "mutableCopy" selector.
  addNSObjectMethSummary(GetNullarySelector("mutableCopy", Ctx), Summ);
  
  // Create the "retain" selector.
  E = RetEffect::MakeReceiverAlias();
  Summ = getPersistentSummary(E, isGCEnabled() ? DoNothing : IncRef);
  addNSObjectMethSummary(GetNullarySelector("retain", Ctx), Summ);
  
  // Create the "release" selector.
  Summ = getPersistentSummary(E, isGCEnabled() ? DoNothing : DecRef);
  addNSObjectMethSummary(GetNullarySelector("release", Ctx), Summ);
  
  // Create the "drain" selector.
  Summ = getPersistentSummary(E, isGCEnabled() ? DoNothing : DecRef);
  addNSObjectMethSummary(GetNullarySelector("drain", Ctx), Summ);

  // Create the "autorelease" selector.
  Summ = getPersistentSummary(E, Autorelease);
  addNSObjectMethSummary(GetNullarySelector("autorelease", Ctx), Summ);
  
  // For NSWindow, allocated objects are (initially) self-owned.  
  RetainSummary *NSWindowSumm =
    getPersistentSummary(RetEffect::MakeReceiverAlias(), SelfOwn);
  
  addInstMethSummary("NSWindow", NSWindowSumm, "initWithContentRect",
                     "styleMask", "backing", "defer", NULL);
  
  addInstMethSummary("NSWindow", NSWindowSumm, "initWithContentRect",
                     "styleMask", "backing", "defer", "screen", NULL);
    
  // For NSPanel (which subclasses NSWindow), allocated objects are not
  //  self-owned.
  addInstMethSummary("NSPanel", InitSumm, "initWithContentRect",
                     "styleMask", "backing", "defer", NULL);
  
  addInstMethSummary("NSPanel", InitSumm, "initWithContentRect",
                     "styleMask", "backing", "defer", "screen", NULL);

  // Create NSAssertionHandler summaries.
  addPanicSummary("NSAssertionHandler", "handleFailureInFunction", "file",
                  "lineNumber", "description", NULL); 
  
  addPanicSummary("NSAssertionHandler", "handleFailureInMethod", "object",
                  "file", "lineNumber", "description", NULL);
}

//===----------------------------------------------------------------------===//
// Reference-counting logic (typestate + counts).
//===----------------------------------------------------------------------===//

namespace {
  
class VISIBILITY_HIDDEN RefVal {
public:  
  enum Kind {
    Owned = 0, // Owning reference.    
    NotOwned,  // Reference is not owned by still valid (not freed).    
    Released,  // Object has been released.
    ReturnedOwned, // Returned object passes ownership to caller.
    ReturnedNotOwned, // Return object does not pass ownership to caller.
    ErrorUseAfterRelease, // Object used after released.    
    ErrorReleaseNotOwned, // Release of an object that was not owned.
    ErrorLeak,  // A memory leak due to excessive reference counts.
    ErrorLeakReturned // A memory leak due to the returning method not having
                      // the correct naming conventions.            
  };

private:  
  Kind kind;
  RetEffect::ObjKind okind;
  unsigned Cnt;
  QualType T;

  RefVal(Kind k, RetEffect::ObjKind o, unsigned cnt, QualType t)
    : kind(k), okind(o), Cnt(cnt), T(t) {}

  RefVal(Kind k, unsigned cnt = 0)
    : kind(k), okind(RetEffect::AnyObj), Cnt(cnt) {}

public:    
  Kind getKind() const { return kind; }
  
  RetEffect::ObjKind getObjKind() const { return okind; }

  unsigned getCount() const { return Cnt; }  
  QualType getType() const { return T; }
  
  // Useful predicates.
  
  static bool isError(Kind k) { return k >= ErrorUseAfterRelease; }
  
  static bool isLeak(Kind k) { return k >= ErrorLeak; }
  
  bool isOwned() const {
    return getKind() == Owned;
  }
  
  bool isNotOwned() const {
    return getKind() == NotOwned;
  }
  
  bool isReturnedOwned() const {
    return getKind() == ReturnedOwned;
  }
  
  bool isReturnedNotOwned() const {
    return getKind() == ReturnedNotOwned;
  }
  
  bool isNonLeakError() const {
    Kind k = getKind();
    return isError(k) && !isLeak(k);
  }
  
  // State creation: normal state.
  
  static RefVal makeOwned(RetEffect::ObjKind o, QualType t,
                          unsigned Count = 1) {
    return RefVal(Owned, o, Count, t);
  }
  
  static RefVal makeNotOwned(RetEffect::ObjKind o, QualType t,
                             unsigned Count = 0) {
    return RefVal(NotOwned, o, Count, t);
  }

  static RefVal makeReturnedOwned(unsigned Count) {
    return RefVal(ReturnedOwned, Count);
  }
  
  static RefVal makeReturnedNotOwned() {
    return RefVal(ReturnedNotOwned);
  }
  
  // Comparison, profiling, and pretty-printing.
  
  bool operator==(const RefVal& X) const {
    return kind == X.kind && Cnt == X.Cnt && T == X.T;
  }
  
  RefVal operator-(size_t i) const {
    return RefVal(getKind(), getObjKind(), getCount() - i, getType());
  }
  
  RefVal operator+(size_t i) const {
    return RefVal(getKind(), getObjKind(), getCount() + i, getType());
  }
  
  RefVal operator^(Kind k) const {
    return RefVal(k, getObjKind(), getCount(), getType());
  }
  
  void Profile(llvm::FoldingSetNodeID& ID) const {
    ID.AddInteger((unsigned) kind);
    ID.AddInteger(Cnt);
    ID.Add(T);
  }

  void print(std::ostream& Out) const;
};
  
void RefVal::print(std::ostream& Out) const {
  if (!T.isNull())
    Out << "Tracked Type:" << T.getAsString() << '\n';
    
  switch (getKind()) {
    default: assert(false);
    case Owned: { 
      Out << "Owned";
      unsigned cnt = getCount();
      if (cnt) Out << " (+ " << cnt << ")";
      break;
    }
      
    case NotOwned: {
      Out << "NotOwned";
      unsigned cnt = getCount();
      if (cnt) Out << " (+ " << cnt << ")";
      break;
    }
      
    case ReturnedOwned: { 
      Out << "ReturnedOwned";
      unsigned cnt = getCount();
      if (cnt) Out << " (+ " << cnt << ")";
      break;
    }
      
    case ReturnedNotOwned: {
      Out << "ReturnedNotOwned";
      unsigned cnt = getCount();
      if (cnt) Out << " (+ " << cnt << ")";
      break;
    }
            
    case Released:
      Out << "Released";
      break;
      
    case ErrorLeak:
      Out << "Leaked";
      break;            
      
    case ErrorLeakReturned:
      Out << "Leaked (Bad naming)";
      break;
      
    case ErrorUseAfterRelease:
      Out << "Use-After-Release [ERROR]";
      break;
      
    case ErrorReleaseNotOwned:
      Out << "Release of Not-Owned [ERROR]";
      break;
  }
}
  
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// RefBindings - State used to track object reference counts.
//===----------------------------------------------------------------------===//
  
typedef llvm::ImmutableMap<SymbolRef, RefVal> RefBindings;
static int RefBIndex = 0;

namespace clang {
  template<>
  struct GRStateTrait<RefBindings> : public GRStatePartialTrait<RefBindings> {
    static inline void* GDMIndex() { return &RefBIndex; }  
  };
}

//===----------------------------------------------------------------------===//
// ARBindings - State used to track objects in autorelease pools.
//===----------------------------------------------------------------------===//

typedef llvm::ImmutableSet<SymbolRef> ARPoolContents;
typedef llvm::ImmutableList< std::pair<SymbolRef, ARPoolContents*> > ARBindings;
static int AutoRBIndex = 0;

namespace clang {
  template<>
  struct GRStateTrait<ARBindings> : public GRStatePartialTrait<ARBindings> {
    static inline void* GDMIndex() { return &AutoRBIndex; }  
  };
}

//===----------------------------------------------------------------------===//
// Transfer functions.
//===----------------------------------------------------------------------===//

namespace {
  
class VISIBILITY_HIDDEN CFRefCount : public GRSimpleVals {
public:
  // Type definitions.  
  typedef llvm::DenseMap<GRExprEngine::NodeTy*,std::pair<Expr*, SymbolRef> >
          ReleasesNotOwnedTy;

  typedef ReleasesNotOwnedTy UseAfterReleasesTy;
    
  typedef llvm::DenseMap<GRExprEngine::NodeTy*,
                         std::vector<std::pair<SymbolRef,bool> >*>
          LeaksTy;

  class BindingsPrinter : public GRState::Printer {
  public:
    virtual void Print(std::ostream& Out, const GRState* state,
                       const char* nl, const char* sep);
  };

private:
  RetainSummaryManager Summaries;  
  const LangOptions&   LOpts;

  UseAfterReleasesTy   UseAfterReleases;
  ReleasesNotOwnedTy   ReleasesNotOwned;
  LeaksTy              Leaks;
  
  RefBindings Update(RefBindings B, SymbolRef sym, RefVal V, ArgEffect E,
                     RefVal::Kind& hasErr, RefBindings::Factory& RefBFactory);
  
  RefVal::Kind& Update(GRStateRef& state, SymbolRef sym, RefVal V,
                       ArgEffect E, RefVal::Kind& hasErr) {
    
    state = state.set<RefBindings>(Update(state.get<RefBindings>(), sym, V, 
                                          E, hasErr,
                                          state.get_context<RefBindings>()));
    return hasErr;
  }
  
  void ProcessNonLeakError(ExplodedNodeSet<GRState>& Dst,
                           GRStmtNodeBuilder<GRState>& Builder,
                           Expr* NodeExpr, Expr* ErrorExpr,                        
                           ExplodedNode<GRState>* Pred,
                           const GRState* St,
                           RefVal::Kind hasErr, SymbolRef Sym);
  
  std::pair<GRStateRef, bool>
  HandleSymbolDeath(GRStateManager& VMgr, const GRState* St,
                    const Decl* CD, SymbolRef sid, RefVal V, bool& hasLeak);
  
public:
  
  CFRefCount(ASTContext& Ctx, bool gcenabled, const LangOptions& lopts)
    : Summaries(Ctx, gcenabled),
      LOpts(lopts) {}
  
  virtual ~CFRefCount() {
    for (LeaksTy::iterator I = Leaks.begin(), E = Leaks.end(); I!=E; ++I)
      delete I->second;
  }
  
  virtual void RegisterChecks(GRExprEngine& Eng);
 
  virtual void RegisterPrinters(std::vector<GRState::Printer*>& Printers) {
    Printers.push_back(new BindingsPrinter());
  }
  
  bool isGCEnabled() const { return Summaries.isGCEnabled(); }
  const LangOptions& getLangOptions() const { return LOpts; }
  
  // Calls.

  void EvalSummary(ExplodedNodeSet<GRState>& Dst,
                   GRExprEngine& Eng,
                   GRStmtNodeBuilder<GRState>& Builder,
                   Expr* Ex,
                   Expr* Receiver,
                   RetainSummary* Summ,
                   ExprIterator arg_beg, ExprIterator arg_end,                             
                   ExplodedNode<GRState>* Pred);
    
  virtual void EvalCall(ExplodedNodeSet<GRState>& Dst,
                        GRExprEngine& Eng,
                        GRStmtNodeBuilder<GRState>& Builder,
                        CallExpr* CE, SVal L,
                        ExplodedNode<GRState>* Pred);  
  
  
  virtual void EvalObjCMessageExpr(ExplodedNodeSet<GRState>& Dst,
                                   GRExprEngine& Engine,
                                   GRStmtNodeBuilder<GRState>& Builder,
                                   ObjCMessageExpr* ME,
                                   ExplodedNode<GRState>* Pred);
  
  bool EvalObjCMessageExprAux(ExplodedNodeSet<GRState>& Dst,
                              GRExprEngine& Engine,
                              GRStmtNodeBuilder<GRState>& Builder,
                              ObjCMessageExpr* ME,
                              ExplodedNode<GRState>* Pred);

  // Stores.
  
  virtual void EvalStore(ExplodedNodeSet<GRState>& Dst,
                         GRExprEngine& Engine,
                         GRStmtNodeBuilder<GRState>& Builder,
                         Expr* E, ExplodedNode<GRState>* Pred,
                         const GRState* St, SVal TargetLV, SVal Val);
  // End-of-path.
  
  virtual void EvalEndPath(GRExprEngine& Engine,
                           GREndPathNodeBuilder<GRState>& Builder);
  
  virtual void EvalDeadSymbols(ExplodedNodeSet<GRState>& Dst,
                               GRExprEngine& Engine,
                               GRStmtNodeBuilder<GRState>& Builder,
                               ExplodedNode<GRState>* Pred,
                               Stmt* S, const GRState* state,
                               SymbolReaper& SymReaper);

  // Return statements.
  
  virtual void EvalReturn(ExplodedNodeSet<GRState>& Dst,
                          GRExprEngine& Engine,
                          GRStmtNodeBuilder<GRState>& Builder,
                          ReturnStmt* S,
                          ExplodedNode<GRState>* Pred);

  // Assumptions.

  virtual const GRState* EvalAssume(GRStateManager& VMgr,
                                       const GRState* St, SVal Cond,
                                       bool Assumption, bool& isFeasible);

  // Error iterators.

  typedef UseAfterReleasesTy::iterator use_after_iterator;  
  typedef ReleasesNotOwnedTy::iterator bad_release_iterator;
  typedef LeaksTy::iterator            leaks_iterator;
  
  use_after_iterator use_after_begin() { return UseAfterReleases.begin(); }
  use_after_iterator use_after_end() { return UseAfterReleases.end(); }
  
  bad_release_iterator bad_release_begin() { return ReleasesNotOwned.begin(); }
  bad_release_iterator bad_release_end() { return ReleasesNotOwned.end(); }
  
  leaks_iterator leaks_begin() { return Leaks.begin(); }
  leaks_iterator leaks_end() { return Leaks.end(); }
};

} // end anonymous namespace




void CFRefCount::BindingsPrinter::Print(std::ostream& Out, const GRState* state,
                                        const char* nl, const char* sep) {
    
  RefBindings B = state->get<RefBindings>();
  
  if (!B.isEmpty())
    Out << sep << nl;
  
  for (RefBindings::iterator I=B.begin(), E=B.end(); I!=E; ++I) {
    Out << (*I).first << " : ";
    (*I).second.print(Out);
    Out << nl;
  }
}

static inline ArgEffect GetArgE(RetainSummary* Summ, unsigned idx) {
  return Summ ? Summ->getArg(idx) : MayEscape;
}

static inline RetEffect GetRetEffect(RetainSummary* Summ) {
  return Summ ? Summ->getRetEffect() : RetEffect::MakeNoRet();
}

static inline ArgEffect GetReceiverE(RetainSummary* Summ) {
  return Summ ? Summ->getReceiverEffect() : DoNothing;
}

static inline bool IsEndPath(RetainSummary* Summ) {
  return Summ ? Summ->isEndPath() : false;
}

void CFRefCount::ProcessNonLeakError(ExplodedNodeSet<GRState>& Dst,
                                     GRStmtNodeBuilder<GRState>& Builder,
                                     Expr* NodeExpr, Expr* ErrorExpr,                        
                                     ExplodedNode<GRState>* Pred,
                                     const GRState* St,
                                     RefVal::Kind hasErr, SymbolRef Sym) {
  Builder.BuildSinks = true;
  GRExprEngine::NodeTy* N  = Builder.MakeNode(Dst, NodeExpr, Pred, St);

  if (!N) return;
    
  switch (hasErr) {
    default: assert(false);
    case RefVal::ErrorUseAfterRelease:
      UseAfterReleases[N] = std::make_pair(ErrorExpr, Sym);
      break;
      
    case RefVal::ErrorReleaseNotOwned:
      ReleasesNotOwned[N] = std::make_pair(ErrorExpr, Sym);
      break;
  }
}

/// GetReturnType - Used to get the return type of a message expression or
///  function call with the intention of affixing that type to a tracked symbol.
///  While the the return type can be queried directly from RetEx, when
///  invoking class methods we augment to the return type to be that of
///  a pointer to the class (as opposed it just being id).
static QualType GetReturnType(Expr* RetE, ASTContext& Ctx) {

  QualType RetTy = RetE->getType();

  // FIXME: We aren't handling id<...>.
  const PointerType* PT = RetTy->getAsPointerType();
  if (!PT)
    return RetTy;
    
  // If RetEx is not a message expression just return its type.
  // If RetEx is a message expression, return its types if it is something
  /// more specific than id.
  
  ObjCMessageExpr* ME = dyn_cast<ObjCMessageExpr>(RetE);
  
  if (!ME || !Ctx.isObjCIdType(PT->getPointeeType()))
    return RetTy;
  
  ObjCInterfaceDecl* D = ME->getClassInfo().first;  

  // At this point we know the return type of the message expression is id.
  // If we have an ObjCInterceDecl, we know this is a call to a class method
  // whose type we can resolve.  In such cases, promote the return type to
  // Class*.  
  return !D ? RetTy : Ctx.getPointerType(Ctx.getObjCInterfaceType(D));
}


void CFRefCount::EvalSummary(ExplodedNodeSet<GRState>& Dst,
                             GRExprEngine& Eng,
                             GRStmtNodeBuilder<GRState>& Builder,
                             Expr* Ex,
                             Expr* Receiver,
                             RetainSummary* Summ,
                             ExprIterator arg_beg, ExprIterator arg_end,                             
                             ExplodedNode<GRState>* Pred) {
  
  // Get the state.
  GRStateRef state(Builder.GetState(Pred), Eng.getStateManager());
  ASTContext& Ctx = Eng.getStateManager().getContext();

  // Evaluate the effect of the arguments.
  RefVal::Kind hasErr = (RefVal::Kind) 0;
  unsigned idx = 0;
  Expr* ErrorExpr = NULL;
  SymbolRef ErrorSym = 0;                                        
  
  for (ExprIterator I = arg_beg; I != arg_end; ++I, ++idx) {    
    SVal V = state.GetSVal(*I);
    
    if (isa<loc::SymbolVal>(V)) {
      SymbolRef Sym = cast<loc::SymbolVal>(V).getSymbol();
      if (RefBindings::data_type* T = state.get<RefBindings>(Sym))
        if (Update(state, Sym, *T, GetArgE(Summ, idx), hasErr)) {
          ErrorExpr = *I;
          ErrorSym = Sym;
          break;
        }
    }  
    else if (isa<Loc>(V)) {
      if (loc::MemRegionVal* MR = dyn_cast<loc::MemRegionVal>(&V)) {

        if (GetArgE(Summ, idx) == DoNothingByRef)
          continue;
        
        // Invalidate the value of the variable passed by reference.
        
        // FIXME: Either this logic should also be replicated in GRSimpleVals
        //  or should be pulled into a separate "constraint engine."
        
        // FIXME: We can have collisions on the conjured symbol if the
        //  expression *I also creates conjured symbols.  We probably want
        //  to identify conjured symbols by an expression pair: the enclosing
        //  expression (the context) and the expression itself.  This should
        //  disambiguate conjured symbols. 
        
        const TypedRegion* R = dyn_cast<TypedRegion>(MR->getRegion());
        
        // Blast through AnonTypedRegions to get the original region type.
        while (R) {
          const AnonTypedRegion* ATR = dyn_cast<AnonTypedRegion>(R);
          if (!ATR) break;
          R = dyn_cast<TypedRegion>(ATR->getSuperRegion());
        }
        
        if (R) {
          
          // Is the invalidated variable something that we were tracking?
          SVal X = state.GetSVal(Loc::MakeVal(R));
          
          if (isa<loc::SymbolVal>(X)) {
            SymbolRef Sym = cast<loc::SymbolVal>(X).getSymbol();
            state = state.remove<RefBindings>(Sym);
          }
          
          // Set the value of the variable to be a conjured symbol.
          unsigned Count = Builder.getCurrentBlockCount();
          QualType T = R->getRValueType(Ctx);
          
          // FIXME: handle structs.
          if (Loc::IsLocType(T) || (T->isIntegerType() && T->isScalarType())) {
            SymbolRef NewSym =
              Eng.getSymbolManager().getConjuredSymbol(*I, T, Count);
            
            state = state.BindLoc(Loc::MakeVal(R),
                                  Loc::IsLocType(T)
                                  ? cast<SVal>(loc::SymbolVal(NewSym))
                                  : cast<SVal>(nonloc::SymbolVal(NewSym)));
          }
          else {
            state = state.BindLoc(*MR, UnknownVal());
          }
        }
        else
          state = state.BindLoc(*MR, UnknownVal());
      }
      else {
        // Nuke all other arguments passed by reference.
        state = state.Unbind(cast<Loc>(V));
      }
    }
    else if (isa<nonloc::LocAsInteger>(V))
      state = state.Unbind(cast<nonloc::LocAsInteger>(V).getLoc());
  } 
  
  // Evaluate the effect on the message receiver.  
  if (!ErrorExpr && Receiver) {
    SVal V = state.GetSVal(Receiver);
    if (isa<loc::SymbolVal>(V)) {
      SymbolRef Sym = cast<loc::SymbolVal>(V).getSymbol();
      if (const RefVal* T = state.get<RefBindings>(Sym))
        if (Update(state, Sym, *T, GetReceiverE(Summ), hasErr)) {
          ErrorExpr = Receiver;
          ErrorSym = Sym;
        }
    }
  }
  
  // Process any errors.  
  if (hasErr) {
    ProcessNonLeakError(Dst, Builder, Ex, ErrorExpr, Pred, state,
                        hasErr, ErrorSym);
    return;
  }
  
  // Consult the summary for the return value.  
  RetEffect RE = GetRetEffect(Summ);
  
  switch (RE.getKind()) {
    default:
      assert (false && "Unhandled RetEffect."); break;
      
    case RetEffect::NoRet: {
      
      // Make up a symbol for the return value (not reference counted).
      // FIXME: This is basically copy-and-paste from GRSimpleVals.  We 
      //  should compose behavior, not copy it.
      
      // FIXME: We eventually should handle structs and other compound types
      // that are returned by value.
      
      QualType T = Ex->getType();
      
      if (Loc::IsLocType(T) || (T->isIntegerType() && T->isScalarType())) {
        unsigned Count = Builder.getCurrentBlockCount();
        SymbolRef Sym = Eng.getSymbolManager().getConjuredSymbol(Ex, Count);
        
        SVal X = Loc::IsLocType(Ex->getType())
               ? cast<SVal>(loc::SymbolVal(Sym)) 
               : cast<SVal>(nonloc::SymbolVal(Sym));
        
        state = state.BindExpr(Ex, X, false);
      }      
      
      break;
    }
      
    case RetEffect::Alias: {
      unsigned idx = RE.getIndex();
      assert (arg_end >= arg_beg);
      assert (idx < (unsigned) (arg_end - arg_beg));
      SVal V = state.GetSVal(*(arg_beg+idx));
      state = state.BindExpr(Ex, V, false);
      break;
    }
      
    case RetEffect::ReceiverAlias: {
      assert (Receiver);
      SVal V = state.GetSVal(Receiver);
      state = state.BindExpr(Ex, V, false);
      break;
    }
      
    case RetEffect::OwnedAllocatedSymbol:
    case RetEffect::OwnedSymbol: {
      unsigned Count = Builder.getCurrentBlockCount();
      SymbolRef Sym = Eng.getSymbolManager().getConjuredSymbol(Ex, Count);
      QualType RetT = GetReturnType(Ex, Eng.getContext());      
      state =
        state.set<RefBindings>(Sym, RefVal::makeOwned(RE.getObjKind(), RetT));      
      state = state.BindExpr(Ex, loc::SymbolVal(Sym), false);

      // FIXME: Add a flag to the checker where allocations are allowed to fail.      
      if (RE.getKind() == RetEffect::OwnedAllocatedSymbol) {
        bool isFeasible;
        state = state.Assume(loc::SymbolVal(Sym), true, isFeasible);
        assert(isFeasible && "Cannot assume fresh symbol is non-null.");        
      }
      
      break;
    }
      
    case RetEffect::NotOwnedSymbol: {
      unsigned Count = Builder.getCurrentBlockCount();
      SymbolRef Sym = Eng.getSymbolManager().getConjuredSymbol(Ex, Count);
      QualType RetT = GetReturnType(Ex, Eng.getContext());
      
      state =
        state.set<RefBindings>(Sym, RefVal::makeNotOwned(RE.getObjKind(),RetT));
      state = state.BindExpr(Ex, loc::SymbolVal(Sym), false);
      break;
    }
  }
  
  // Is this a sink?
  if (IsEndPath(Summ))
    Builder.MakeSinkNode(Dst, Ex, Pred, state);
  else
    Builder.MakeNode(Dst, Ex, Pred, state);
}


void CFRefCount::EvalCall(ExplodedNodeSet<GRState>& Dst,
                          GRExprEngine& Eng,
                          GRStmtNodeBuilder<GRState>& Builder,
                          CallExpr* CE, SVal L,
                          ExplodedNode<GRState>* Pred) {

  RetainSummary* Summ = !isa<loc::FuncVal>(L) ? 0
                      : Summaries.getSummary(cast<loc::FuncVal>(L).getDecl());
  
  EvalSummary(Dst, Eng, Builder, CE, 0, Summ,
              CE->arg_begin(), CE->arg_end(), Pred);
}

void CFRefCount::EvalObjCMessageExpr(ExplodedNodeSet<GRState>& Dst,
                                     GRExprEngine& Eng,
                                     GRStmtNodeBuilder<GRState>& Builder,
                                     ObjCMessageExpr* ME,
                                     ExplodedNode<GRState>* Pred) {  
  RetainSummary* Summ;
  
  if (Expr* Receiver = ME->getReceiver()) {
    // We need the type-information of the tracked receiver object
    // Retrieve it from the state.
    ObjCInterfaceDecl* ID = 0;

    // FIXME: Wouldn't it be great if this code could be reduced?  It's just
    // a chain of lookups.
    const GRState* St = Builder.GetState(Pred);
    SVal V = Eng.getStateManager().GetSVal(St, Receiver );

    if (isa<loc::SymbolVal>(V)) {
      SymbolRef Sym = cast<loc::SymbolVal>(V).getSymbol();
      
      if (const RefVal* T  = St->get<RefBindings>(Sym)) {
        QualType Ty = T->getType();
        
        if (const PointerType* PT = Ty->getAsPointerType()) {
          QualType PointeeTy = PT->getPointeeType();
          
          if (ObjCInterfaceType* IT = dyn_cast<ObjCInterfaceType>(PointeeTy))
            ID = IT->getDecl();
        }
      }
    }
    
    Summ = Summaries.getMethodSummary(ME, ID);

    // Special-case: are we sending a mesage to "self"?
    //  This is a hack.  When we have full-IP this should be removed.
    if (!Summ) {
      ObjCMethodDecl* MD = 
        dyn_cast<ObjCMethodDecl>(&Eng.getGraph().getCodeDecl());
      
      if (MD) {
        if (Expr* Receiver = ME->getReceiver()) {
          SVal X = Eng.getStateManager().GetSVal(St, Receiver);
          if (loc::MemRegionVal* L = dyn_cast<loc::MemRegionVal>(&X))
            if (L->getRegion() == Eng.getStateManager().getSelfRegion(St)) {
              // Create a summmary where all of the arguments "StopTracking".
              Summ = Summaries.getPersistentSummary(RetEffect::MakeNoRet(),
                                                    DoNothing,
                                                    StopTracking);
            }
        }
      }
    }
  }
  else
    Summ = Summaries.getClassMethodSummary(ME->getClassName(),
                                           ME->getSelector());

  EvalSummary(Dst, Eng, Builder, ME, ME->getReceiver(), Summ,
              ME->arg_begin(), ME->arg_end(), Pred);
}
  
// Stores.

void CFRefCount::EvalStore(ExplodedNodeSet<GRState>& Dst,
                           GRExprEngine& Eng,
                           GRStmtNodeBuilder<GRState>& Builder,
                           Expr* E, ExplodedNode<GRState>* Pred,
                           const GRState* St, SVal TargetLV, SVal Val) {
  
  // Check if we have a binding for "Val" and if we are storing it to something
  // we don't understand or otherwise the value "escapes" the function.
  
  if (!isa<loc::SymbolVal>(Val))
    return;
  
  // Are we storing to something that causes the value to "escape"?
  
  bool escapes = false;
  
  // A value escapes in three possible cases (this may change):
  //
  // (1) we are binding to something that is not a memory region.
  // (2) we are binding to a memregion that does not have stack storage
  // (3) we are binding to a memregion with stack storage that the store
  //     does not understand.
  
  SymbolRef Sym = cast<loc::SymbolVal>(Val).getSymbol();
  GRStateRef state(St, Eng.getStateManager());

  if (!isa<loc::MemRegionVal>(TargetLV))
    escapes = true;
  else {
    const MemRegion* R = cast<loc::MemRegionVal>(TargetLV).getRegion();
    escapes = !Eng.getStateManager().hasStackStorage(R);
    
    if (!escapes) {
      // To test (3), generate a new state with the binding removed.  If it is
      // the same state, then it escapes (since the store cannot represent
      // the binding).
      GRStateRef stateNew = state.BindLoc(cast<Loc>(TargetLV), Val);
      escapes = (stateNew == state);
    }
  }
  
  if (!escapes)
    return;

  // Do we have a reference count binding?
  // FIXME: Is this step even needed?  We do blow away the binding anyway.
  if (!state.get<RefBindings>(Sym))
    return;
  
  // Nuke the binding.
  state = state.remove<RefBindings>(Sym);

  // Hand of the remaining logic to the parent implementation.
  GRSimpleVals::EvalStore(Dst, Eng, Builder, E, Pred, state, TargetLV, Val);
}

// End-of-path.


std::pair<GRStateRef,bool>
CFRefCount::HandleSymbolDeath(GRStateManager& VMgr,
                              const GRState* St, const Decl* CD,
                              SymbolRef sid,
                              RefVal V, bool& hasLeak) {

  GRStateRef state(St, VMgr);
  assert ((!V.isReturnedOwned() || CD) &&
          "CodeDecl must be available for reporting ReturnOwned errors.");

  if (V.isReturnedOwned() && V.getCount() == 0)
    if (const ObjCMethodDecl* MD = dyn_cast<ObjCMethodDecl>(CD)) {
      std::string s = MD->getSelector().getAsString();
      if (!followsReturnRule(s.c_str())) {
        hasLeak = true;
        state = state.set<RefBindings>(sid, V ^ RefVal::ErrorLeakReturned);
        return std::make_pair(state, true);
      }
    }
  
  // All other cases.
  
  hasLeak = V.isOwned() || 
            ((V.isNotOwned() || V.isReturnedOwned()) && V.getCount() > 0);

  if (!hasLeak)
    return std::make_pair(state.remove<RefBindings>(sid), false);
  
  return std::make_pair(state.set<RefBindings>(sid, V ^ RefVal::ErrorLeak),
                        false);
}

void CFRefCount::EvalEndPath(GRExprEngine& Eng,
                             GREndPathNodeBuilder<GRState>& Builder) {
  
  const GRState* St = Builder.getState();
  RefBindings B = St->get<RefBindings>();
  
  llvm::SmallVector<std::pair<SymbolRef, bool>, 10> Leaked;
  const Decl* CodeDecl = &Eng.getGraph().getCodeDecl();
  
  for (RefBindings::iterator I = B.begin(), E = B.end(); I != E; ++I) {
    bool hasLeak = false;
    
    std::pair<GRStateRef, bool> X =
      HandleSymbolDeath(Eng.getStateManager(), St, CodeDecl,
                        (*I).first, (*I).second, hasLeak);
    
    St = X.first;
    if (hasLeak) Leaked.push_back(std::make_pair((*I).first, X.second));
  }

  if (Leaked.empty())
    return;
  
  ExplodedNode<GRState>* N = Builder.MakeNode(St);  
  
  if (!N)
    return;
    
  std::vector<std::pair<SymbolRef,bool> >*& LeaksAtNode = Leaks[N];
  assert (!LeaksAtNode);
  LeaksAtNode = new std::vector<std::pair<SymbolRef,bool> >();
  
  for (llvm::SmallVector<std::pair<SymbolRef,bool>, 10>::iterator
       I = Leaked.begin(), E = Leaked.end(); I != E; ++I)
    (*LeaksAtNode).push_back(*I);
}

// Dead symbols.

void CFRefCount::EvalDeadSymbols(ExplodedNodeSet<GRState>& Dst,
                                 GRExprEngine& Eng,
                                 GRStmtNodeBuilder<GRState>& Builder,
                                 ExplodedNode<GRState>* Pred,
                                 Stmt* S,
                                 const GRState* St,
                                 SymbolReaper& SymReaper) {
    
  // FIXME: a lot of copy-and-paste from EvalEndPath.  Refactor.
  
  RefBindings B = St->get<RefBindings>();
  llvm::SmallVector<std::pair<SymbolRef,bool>, 10> Leaked;
  
  for (SymbolReaper::dead_iterator I = SymReaper.dead_begin(),
        E = SymReaper.dead_end(); I != E; ++I) {
    
    const RefVal* T = B.lookup(*I);
    if (!T) continue;
    
    bool hasLeak = false;
    
    std::pair<GRStateRef, bool> X
      = HandleSymbolDeath(Eng.getStateManager(), St, 0, *I, *T, hasLeak);
    
    St = X.first;
    
    if (hasLeak)
      Leaked.push_back(std::make_pair(*I,X.second));    
  }
  
  if (Leaked.empty())
    return;    
  
  ExplodedNode<GRState>* N = Builder.MakeNode(Dst, S, Pred, St);  
  
  if (!N)
    return;
  
  std::vector<std::pair<SymbolRef,bool> >*& LeaksAtNode = Leaks[N];
  assert (!LeaksAtNode);
  LeaksAtNode = new std::vector<std::pair<SymbolRef,bool> >();
  
  for (llvm::SmallVector<std::pair<SymbolRef,bool>, 10>::iterator
       I = Leaked.begin(), E = Leaked.end(); I != E; ++I)
    (*LeaksAtNode).push_back(*I);    
}

 // Return statements.

void CFRefCount::EvalReturn(ExplodedNodeSet<GRState>& Dst,
                            GRExprEngine& Eng,
                            GRStmtNodeBuilder<GRState>& Builder,
                            ReturnStmt* S,
                            ExplodedNode<GRState>* Pred) {
  
  Expr* RetE = S->getRetValue();
  if (!RetE) return;
  
  GRStateRef state(Builder.GetState(Pred), Eng.getStateManager());
  SVal V = state.GetSVal(RetE);
  
  if (!isa<loc::SymbolVal>(V))
    return;
  
  // Get the reference count binding (if any).
  SymbolRef Sym = cast<loc::SymbolVal>(V).getSymbol();
  const RefVal* T = state.get<RefBindings>(Sym);
  
  if (!T)
    return;
  
  // Change the reference count.  
  RefVal X = *T;  
  
  switch (X.getKind()) {      
    case RefVal::Owned: { 
      unsigned cnt = X.getCount();
      assert (cnt > 0);
      X = RefVal::makeReturnedOwned(cnt - 1);
      break;
    }
      
    case RefVal::NotOwned: {
      unsigned cnt = X.getCount();
      X = cnt ? RefVal::makeReturnedOwned(cnt - 1)
              : RefVal::makeReturnedNotOwned();
      break;
    }
      
    default: 
      return;
  }
  
  // Update the binding.
  state = state.set<RefBindings>(Sym, X);
  Builder.MakeNode(Dst, S, Pred, state);
}

// Assumptions.

const GRState* CFRefCount::EvalAssume(GRStateManager& VMgr,
                                         const GRState* St,
                                         SVal Cond, bool Assumption,
                                         bool& isFeasible) {

  // FIXME: We may add to the interface of EvalAssume the list of symbols
  //  whose assumptions have changed.  For now we just iterate through the
  //  bindings and check if any of the tracked symbols are NULL.  This isn't
  //  too bad since the number of symbols we will track in practice are 
  //  probably small and EvalAssume is only called at branches and a few
  //  other places.
  RefBindings B = St->get<RefBindings>();
  
  if (B.isEmpty())
    return St;
  
  bool changed = false;
  
  GRStateRef state(St, VMgr);
  RefBindings::Factory& RefBFactory = state.get_context<RefBindings>();

  for (RefBindings::iterator I=B.begin(), E=B.end(); I!=E; ++I) {    
    // Check if the symbol is null (or equal to any constant).
    // If this is the case, stop tracking the symbol.
    if (VMgr.getSymVal(St, I.getKey())) {
      changed = true;
      B = RefBFactory.Remove(B, I.getKey());
    }
  }
  
  if (changed)
    state = state.set<RefBindings>(B);
  
  return state;
}

RefBindings CFRefCount::Update(RefBindings B, SymbolRef sym,
                               RefVal V, ArgEffect E,
                               RefVal::Kind& hasErr,
                               RefBindings::Factory& RefBFactory) {
  
  // FIXME: This dispatch can potentially be sped up by unifiying it into
  //  a single switch statement.  Opt for simplicity for now.
  
  switch (E) {
    default:
      assert (false && "Unhandled CFRef transition.");

    case MayEscape:
      if (V.getKind() == RefVal::Owned) {
        V = V ^ RefVal::NotOwned;
        break;
      }
      // Fall-through.
    case DoNothingByRef:
    case DoNothing:
      if (!isGCEnabled() && V.getKind() == RefVal::Released) {
        V = V ^ RefVal::ErrorUseAfterRelease;
        hasErr = V.getKind();
        break;
      }      
      return B;

    case Autorelease:
      if (isGCEnabled()) return B;      
      // Fall-through.      
    case StopTracking:
      return RefBFactory.Remove(B, sym);

    case IncRef:      
      switch (V.getKind()) {
        default:
          assert(false);

        case RefVal::Owned:
        case RefVal::NotOwned:
          V = V + 1;
          break;          
        case RefVal::Released:
          if (isGCEnabled())
            V = V ^ RefVal::Owned;
          else {          
            V = V ^ RefVal::ErrorUseAfterRelease;
            hasErr = V.getKind();
          }
          break;
      }      
      break;
      
    case SelfOwn:
      V = V ^ RefVal::NotOwned;
      // Fall-through.      
    case DecRef:
      switch (V.getKind()) {
        default:
          assert (false);

        case RefVal::Owned:
          V = V.getCount() > 1 ? V - 1 : V ^ RefVal::Released;
          break;
          
        case RefVal::NotOwned:
          if (V.getCount() > 0)
            V = V - 1;
          else {
            V = V ^ RefVal::ErrorReleaseNotOwned;
            hasErr = V.getKind();
          }          
          break;

        case RefVal::Released:
          V = V ^ RefVal::ErrorUseAfterRelease;
          hasErr = V.getKind();
          break;          
      }      
      break;
  }
  return RefBFactory.Add(B, sym, V);
}

//===----------------------------------------------------------------------===//
// Error reporting.
//===----------------------------------------------------------------------===//

namespace {
  
  //===-------------===//
  // Bug Descriptions. //
  //===-------------===//  
  
  class VISIBILITY_HIDDEN CFRefBug : public BugTypeCacheLocation {
  protected:
    CFRefCount& TF;
    
  public:
    CFRefBug(CFRefCount& tf) : TF(tf) {}
    
    CFRefCount& getTF() { return TF; }
    const CFRefCount& getTF() const { return TF; }

    virtual bool isLeak() const { return false; }

    const char* getCategory() const { 
      return "Memory (Core Foundation/Objective-C)";
    }
  };
  
  class VISIBILITY_HIDDEN UseAfterRelease : public CFRefBug {
  public:
    UseAfterRelease(CFRefCount& tf) : CFRefBug(tf) {}
    
    virtual const char* getName() const {
      return "use-after-release";
    }
    virtual const char* getDescription() const {
      return "Reference-counted object is used after it is released.";
    }
    
    virtual void EmitWarnings(BugReporter& BR);
  };
  
  class VISIBILITY_HIDDEN BadRelease : public CFRefBug {
  public:
    BadRelease(CFRefCount& tf) : CFRefBug(tf) {}
    
    virtual const char* getName() const {
      return "bad release";
    }
    virtual const char* getDescription() const {
      return "Incorrect decrement of the reference count of a "
      "CoreFoundation object: "
      "The object is not owned at this point by the caller.";
    }
    
    virtual void EmitWarnings(BugReporter& BR);
  };
  
  class VISIBILITY_HIDDEN Leak : public CFRefBug {
    bool isReturn;
  public:
    Leak(CFRefCount& tf) : CFRefBug(tf) {}
    
    void setIsReturn(bool x) { isReturn = x; }
    
    virtual const char* getName() const {
      
      if (!isReturn) {
        if (getTF().isGCEnabled())
          return "leak (GC)";
        
        if (getTF().getLangOptions().getGCMode() == LangOptions::HybridGC)
          return "leak (hybrid MM, non-GC)";
        
        assert (getTF().getLangOptions().getGCMode() == LangOptions::NonGC);
        return "leak";
      }
      else {
        if (getTF().isGCEnabled())
          return "[naming convention] leak of returned object (GC)";
        
        if (getTF().getLangOptions().getGCMode() == LangOptions::HybridGC)
          return "[naming convention] leak of returned object (hybrid MM, "
                 "non-GC)";
        
        assert (getTF().getLangOptions().getGCMode() == LangOptions::NonGC);
        return "[naming convention] leak of returned object";        
      }
    }

    virtual void EmitWarnings(BugReporter& BR);
    virtual void GetErrorNodes(std::vector<ExplodedNode<GRState>*>& Nodes);
    virtual bool isLeak() const { return true; }
    virtual bool isCached(BugReport& R);
  };
  
  //===---------===//
  // Bug Reports.  //
  //===---------===//
  
  class VISIBILITY_HIDDEN CFRefReport : public RangedBugReport {
    SymbolRef Sym;
  public:
    CFRefReport(CFRefBug& D, ExplodedNode<GRState> *n, SymbolRef sym)
      : RangedBugReport(D, n), Sym(sym) {}
        
    virtual ~CFRefReport() {}
    
    CFRefBug& getBugType() {
      return (CFRefBug&) RangedBugReport::getBugType();
    }
    const CFRefBug& getBugType() const {
      return (const CFRefBug&) RangedBugReport::getBugType();
    }
    
    virtual void getRanges(BugReporter& BR, const SourceRange*& beg,           
                           const SourceRange*& end) {
      
      if (!getBugType().isLeak())
        RangedBugReport::getRanges(BR, beg, end);
      else
        beg = end = 0;
    }
    
    SymbolRef getSymbol() const { return Sym; }
    
    PathDiagnosticPiece* getEndPath(BugReporter& BR,
                                    const ExplodedNode<GRState>* N);
    
    std::pair<const char**,const char**> getExtraDescriptiveText();
    
    PathDiagnosticPiece* VisitNode(const ExplodedNode<GRState>* N,
                                   const ExplodedNode<GRState>* PrevN,
                                   const ExplodedGraph<GRState>& G,
                                   BugReporter& BR);
  };
  
  
} // end anonymous namespace

void CFRefCount::RegisterChecks(GRExprEngine& Eng) {
  Eng.Register(new UseAfterRelease(*this));
  Eng.Register(new BadRelease(*this));
  Eng.Register(new Leak(*this));
}


static const char* Msgs[] = {
  "Code is compiled in garbage collection only mode"  // GC only
  "  (the bug occurs with garbage collection enabled).",
  
  "Code is compiled without garbage collection.", // No GC.
  
  "Code is compiled for use with and without garbage collection (GC)."
  "  The bug occurs with GC enabled.", // Hybrid, with GC.
  
  "Code is compiled for use with and without garbage collection (GC)."
  "  The bug occurs in non-GC mode."  // Hyrbird, without GC/
};

std::pair<const char**,const char**> CFRefReport::getExtraDescriptiveText() {
  CFRefCount& TF = static_cast<CFRefBug&>(getBugType()).getTF();

  switch (TF.getLangOptions().getGCMode()) {
    default:
      assert(false);
          
    case LangOptions::GCOnly:
      assert (TF.isGCEnabled());
      return std::make_pair(&Msgs[0], &Msgs[0]+1);      

    case LangOptions::NonGC:
      assert (!TF.isGCEnabled());
      return std::make_pair(&Msgs[1], &Msgs[1]+1);
    
    case LangOptions::HybridGC:
      if (TF.isGCEnabled())
        return std::make_pair(&Msgs[2], &Msgs[2]+1);
      else
        return std::make_pair(&Msgs[3], &Msgs[3]+1);
  }
}

PathDiagnosticPiece* CFRefReport::VisitNode(const ExplodedNode<GRState>* N,
                                            const ExplodedNode<GRState>* PrevN,
                                            const ExplodedGraph<GRState>& G,
                                            BugReporter& BR) {

  // Check if the type state has changed.  
  GRStateManager &StMgr = cast<GRBugReporter>(BR).getStateManager();
  GRStateRef PrevSt(PrevN->getState(), StMgr);
  GRStateRef CurrSt(N->getState(), StMgr);

  const RefVal* CurrT = CurrSt.get<RefBindings>(Sym);  
  if (!CurrT) return NULL;

  const RefVal& CurrV = *CurrT;
  const RefVal* PrevT = PrevSt.get<RefBindings>(Sym);

  if (!PrevT) {
    std::string sbuf;
    llvm::raw_string_ostream os(sbuf);
    
    Stmt* S = cast<PostStmt>(N->getLocation()).getStmt();

    if (CallExpr *CE = dyn_cast<CallExpr>(S)) {
      // Get the name of the callee (if it is available).
      SVal X = CurrSt.GetSVal(CE->getCallee());        
      if (loc::FuncVal* FV = dyn_cast<loc::FuncVal>(&X))
        os << "Call to function '" << FV->getDecl()->getNameAsString() <<'\'';
      else
        os << "function call";      
    }          
    else {
      assert (isa<ObjCMessageExpr>(S));
      os << "Method";
    }
    
    if (CurrV.getObjKind() == RetEffect::CF) {
      os << " returns a Core Foundation object with a ";
    }
    else {
      assert (CurrV.getObjKind() == RetEffect::ObjC);
      os << " returns an Objective-C object with a ";
    }
    
    if (CurrV.isOwned()) {
      os << "+1 retain count (owning reference).";
      
      if (static_cast<CFRefBug&>(getBugType()).getTF().isGCEnabled()) {
        assert(CurrV.getObjKind() == RetEffect::CF);
        os << "  "
          "Core Foundation objects are not automatically garbage collected.";
      }
    }
    else {
      assert (CurrV.isNotOwned());
      os << "+0 retain count (non-owning reference).";
    }
    
    FullSourceLoc Pos(S->getLocStart(), BR.getContext().getSourceManager());
    PathDiagnosticPiece* P = new PathDiagnosticPiece(Pos, os.str());
    
    if (Expr* Exp = dyn_cast<Expr>(S))
      P->addRange(Exp->getSourceRange());
    
    return P;    
  }
  
  // Determine if the typestate has changed.  
  RefVal PrevV = *PrevT;
  
  if (PrevV == CurrV)
    return NULL;
  
  // The typestate has changed.
  std::string sbuf;
  llvm::raw_string_ostream os(sbuf);
  
  switch (CurrV.getKind()) {
    case RefVal::Owned:
    case RefVal::NotOwned:

      if (PrevV.getCount() == CurrV.getCount())
        return 0;
      
      if (PrevV.getCount() > CurrV.getCount())
        os << "Reference count decremented.";
      else
        os << "Reference count incremented.";
      
      if (unsigned Count = CurrV.getCount()) {
        os << " Object has +" << Count;
        
        if (Count > 1)
          os << " retain counts.";
        else
          os << " retain count.";
      }
      
      break;
      
    case RefVal::Released:
      os << "Object released.";
      break;
      
    case RefVal::ReturnedOwned:
      os << "Object returned to caller as an owning reference (single retain "
            "count transferred to caller).";
      break;
      
    case RefVal::ReturnedNotOwned:
      os << "Object returned to caller with a +0 (non-owning) retain count.";
      break;

    default:
      return NULL;
  }
  
  Stmt* S = cast<PostStmt>(N->getLocation()).getStmt();    
  FullSourceLoc Pos(S->getLocStart(), BR.getContext().getSourceManager());
  PathDiagnosticPiece* P = new PathDiagnosticPiece(Pos, os.str());
  
  // Add the range by scanning the children of the statement for any bindings
  // to Sym.
  for (Stmt::child_iterator I = S->child_begin(), E = S->child_end(); I!=E; ++I)
    if (Expr* Exp = dyn_cast_or_null<Expr>(*I)) {
      SVal X = CurrSt.GetSVal(Exp);      
      if (loc::SymbolVal* SV = dyn_cast<loc::SymbolVal>(&X))
        if (SV->getSymbol() == Sym) P->addRange(Exp->getSourceRange()); break;
    }
  
  return P;
}

namespace {
class VISIBILITY_HIDDEN FindUniqueBinding :
  public StoreManager::BindingsHandler {
    SymbolRef Sym;
    MemRegion* Binding;
    bool First;
    
  public:
    FindUniqueBinding(SymbolRef sym) : Sym(sym), Binding(0), First(true) {}
    
  bool HandleBinding(StoreManager& SMgr, Store store, MemRegion* R, SVal val) {
    if (const loc::SymbolVal* SV = dyn_cast<loc::SymbolVal>(&val)) {
      if (SV->getSymbol() != Sym) 
        return true;
    }
    else if (const nonloc::SymbolVal* SV=dyn_cast<nonloc::SymbolVal>(&val)) {
      if (SV->getSymbol() != Sym)
        return true;
    }
    else
      return true;

    if (Binding) {
      First = false;
      return false;
    }
    else
      Binding = R;
    
    return true;    
  }
    
  operator bool() { return First && Binding; }
  MemRegion* getRegion() { return Binding; }
};  
}

static std::pair<const ExplodedNode<GRState>*,const MemRegion*>
GetAllocationSite(GRStateManager* StateMgr, const ExplodedNode<GRState>* N,
                  SymbolRef Sym) {

  // Find both first node that referred to the tracked symbol and the
  // memory location that value was store to.
  const ExplodedNode<GRState>* Last = N;
  const MemRegion* FirstBinding = 0;  
  
  while (N) {
    const GRState* St = N->getState();
    RefBindings B = St->get<RefBindings>();
    
    if (!B.lookup(Sym))
      break;

    if (StateMgr) {
      FindUniqueBinding FB(Sym);
      StateMgr->iterBindings(St, FB);      
      if (FB) FirstBinding = FB.getRegion();      
    }
    
    Last = N;
    N = N->pred_empty() ? NULL : *(N->pred_begin());    
  }
  
  return std::make_pair(Last, FirstBinding);
}

PathDiagnosticPiece*
CFRefReport::getEndPath(BugReporter& br, const ExplodedNode<GRState>* EndN) {

  GRBugReporter& BR = cast<GRBugReporter>(br);
  
  // Tell the BugReporter to report cases when the tracked symbol is
  // assigned to different variables, etc.
  cast<GRBugReporter>(BR).addNotableSymbol(Sym);
  
  if (!getBugType().isLeak())
    return RangedBugReport::getEndPath(BR, EndN);
  
  // We are a leak.  Walk up the graph to get to the first node where the
  // symbol appeared, and also get the first VarDecl that tracked object
  // is stored to.
  const ExplodedNode<GRState>* AllocNode = 0;
  const MemRegion* FirstBinding = 0;

  llvm::tie(AllocNode, FirstBinding) =
    GetAllocationSite(&BR.getStateManager(), EndN, Sym);
  
  // Get the allocate site.  
  assert (AllocNode);
  Stmt* FirstStmt = cast<PostStmt>(AllocNode->getLocation()).getStmt();

  SourceManager& SMgr = BR.getContext().getSourceManager();
  unsigned AllocLine =SMgr.getInstantiationLineNumber(FirstStmt->getLocStart());

  // Get the leak site.  We may have multiple ExplodedNodes (one with the
  // leak) that occur on the same line number; if the node with the leak
  // has any immediate predecessor nodes with the same line number, find
  // any transitive-successors that have a different statement and use that
  // line number instead.  This avoids emiting a diagnostic like:
  //
  //    // 'y' is leaked.
  //  int x = foo(y);
  //
  //  instead we want:
  //
  //  int x = foo(y);
  //   // 'y' is leaked.
  
  Stmt* S = getStmt(BR);  // This is the statement where the leak occured.
  assert (S);
  unsigned EndLine = SMgr.getInstantiationLineNumber(S->getLocStart());

  // Look in the *trimmed* graph at the immediate predecessor of EndN.  Does
  // it occur on the same line?
  PathDiagnosticPiece::DisplayHint Hint = PathDiagnosticPiece::Above;
  
  assert (!EndN->pred_empty()); // Not possible to have 0 predecessors.
  const ExplodedNode<GRState> *Pred = *(EndN->pred_begin());
  ProgramPoint PredPos = Pred->getLocation();
  
  if (PostStmt* PredPS = dyn_cast<PostStmt>(&PredPos)) {

    Stmt* SPred = PredPS->getStmt();
    
    // Predecessor at same line?
    if (SMgr.getInstantiationLineNumber(SPred->getLocStart()) != EndLine) {
      Hint = PathDiagnosticPiece::Below;
      S = SPred;
    }
  }
  
  // Generate the diagnostic.
  FullSourceLoc L( S->getLocStart(), SMgr);
  std::ostringstream os;
  
  os << "Object allocated on line " << AllocLine;
  
  if (FirstBinding)
    os << " and stored into '" << FirstBinding->getString() << '\'';  

  
  // Get the retain count.
  const RefVal* RV = EndN->getState()->get<RefBindings>(Sym);
  
  if (RV->getKind() == RefVal::ErrorLeakReturned) {
    // FIXME: Per comments in rdar://6320065, "create" only applies to CF
    // ojbects.  Only "copy", "alloc", "retain" and "new" transfer ownership
    // to the caller for NS objects.
    ObjCMethodDecl& MD = cast<ObjCMethodDecl>(BR.getGraph().getCodeDecl());
    os << " is returned from a method whose name ('"
       << MD.getSelector().getAsString()
       << "') does not contain 'copy' or otherwise starts with"
          " 'new' or 'alloc'.  This violates the naming convention rules given"
          " in the Memory Management Guide for Cocoa (object leaked).";
  }
  else
    os << " is no longer referenced after this point and has a retain count of"
          " +"
       << RV->getCount() << " (object leaked).";
  
  return new PathDiagnosticPiece(L, os.str(), Hint);
}

void UseAfterRelease::EmitWarnings(BugReporter& BR) {

  for (CFRefCount::use_after_iterator I = TF.use_after_begin(),
        E = TF.use_after_end(); I != E; ++I) {
    
    CFRefReport report(*this, I->first, I->second.second);
    report.addRange(I->second.first->getSourceRange());    
    BR.EmitWarning(report);    
  }
}

void BadRelease::EmitWarnings(BugReporter& BR) {
  
  for (CFRefCount::bad_release_iterator I = TF.bad_release_begin(),
       E = TF.bad_release_end(); I != E; ++I) {
    
    CFRefReport report(*this, I->first, I->second.second);
    report.addRange(I->second.first->getSourceRange());    
    BR.EmitWarning(report);    
  }  
}

void Leak::EmitWarnings(BugReporter& BR) {
  
  for (CFRefCount::leaks_iterator I = TF.leaks_begin(),
       E = TF.leaks_end(); I != E; ++I) {
    
    std::vector<std::pair<SymbolRef, bool> >& SymV = *(I->second);
    unsigned n = SymV.size();
    
    for (unsigned i = 0; i < n; ++i) {
      setIsReturn(SymV[i].second);
      CFRefReport report(*this, I->first, SymV[i].first);
      BR.EmitWarning(report);
    }
  }  
}

void Leak::GetErrorNodes(std::vector<ExplodedNode<GRState>*>& Nodes) {
  for (CFRefCount::leaks_iterator I=TF.leaks_begin(), E=TF.leaks_end();
       I!=E; ++I)
    Nodes.push_back(I->first);
}

bool Leak::isCached(BugReport& R) {
  
  // Most bug reports are cached at the location where they occured.
  // With leaks, we want to unique them by the location where they were
  // allocated, and only report a single path.
  
  SymbolRef Sym = static_cast<CFRefReport&>(R).getSymbol();

  const ExplodedNode<GRState>* AllocNode =
      GetAllocationSite(0, R.getEndNode(), Sym).first;
  
  if (!AllocNode)
    return false;
  
  return BugTypeCacheLocation::isCached(AllocNode->getLocation());
}

//===----------------------------------------------------------------------===//
// Transfer function creation for external clients.
//===----------------------------------------------------------------------===//

GRTransferFuncs* clang::MakeCFRefCountTF(ASTContext& Ctx, bool GCEnabled,
                                         const LangOptions& lopts) {
  return new CFRefCount(Ctx, GCEnabled, lopts);
}  
