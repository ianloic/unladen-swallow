//===--- MinimalAction.cpp - Implement the MinimalAction class ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the MinimalAction interface.
//
//===----------------------------------------------------------------------===//

#include "clang/Parse/Parser.h"
#include "clang/Parse/DeclSpec.h"
#include "clang/Parse/Scope.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/RecyclingAllocator.h"
using namespace clang;

/// TypeNameInfo - A link exists here for each scope that an identifier is
/// defined.
namespace {
  struct TypeNameInfo {
    TypeNameInfo *Prev;
    bool isTypeName;
    
    TypeNameInfo(bool istypename, TypeNameInfo *prev) {
      isTypeName = istypename;
      Prev = prev;
    }
  };

  struct TypeNameInfoTable {
    llvm::RecyclingAllocator<llvm::BumpPtrAllocator, TypeNameInfo> Allocator;
    
    void AddEntry(bool isTypename, IdentifierInfo *II) {
      TypeNameInfo *TI = Allocator.Allocate<TypeNameInfo>();
      new (TI) TypeNameInfo(1, II->getFETokenInfo<TypeNameInfo>());
      II->setFETokenInfo(TI);
    }
    
    void DeleteEntry(TypeNameInfo *Entry) {
      Entry->~TypeNameInfo();
      Allocator.Deallocate(Entry);
    }
  };
}

static TypeNameInfoTable *getTable(void *TP) {
  return static_cast<TypeNameInfoTable*>(TP);
}

MinimalAction::MinimalAction(Preprocessor &pp) 
  : Idents(pp.getIdentifierTable()), PP(pp) {
  TypeNameInfoTablePtr = new TypeNameInfoTable();
}

MinimalAction::~MinimalAction() {
  delete getTable(TypeNameInfoTablePtr);
}

void MinimalAction::ActOnTranslationUnitScope(SourceLocation Loc, Scope *S) {
  TUScope = S;
  if (!PP.getLangOptions().ObjC1) return;


  TypeNameInfoTable &TNIT = *getTable(TypeNameInfoTablePtr);
  
  // Recognize the ObjC built-in type identifiers as types. 
  TNIT.AddEntry(true, &Idents.get("id"));
  TNIT.AddEntry(true, &Idents.get("SEL"));
  TNIT.AddEntry(true, &Idents.get("Class"));
  TNIT.AddEntry(true, &Idents.get("Protocol"));
}

/// isTypeName - This looks at the IdentifierInfo::FETokenInfo field to
/// determine whether the name is a type name (objc class name or typedef) or
/// not in this scope.
///
/// FIXME: Use the passed CXXScopeSpec for accurate C++ type checking.
Action::TypeTy *
MinimalAction::getTypeName(IdentifierInfo &II, Scope *S,
                           const CXXScopeSpec *SS) {
  if (TypeNameInfo *TI = II.getFETokenInfo<TypeNameInfo>())
    if (TI->isTypeName)
      return TI;
  return 0;
}

/// isCurrentClassName - Always returns false, because MinimalAction
/// does not support C++ classes with constructors.
bool MinimalAction::isCurrentClassName(const IdentifierInfo &, Scope *,
                                       const CXXScopeSpec *) {
  return false;
}

  /// isTemplateName - Determines whether the identifier II is a
  /// template name in the current scope, and returns the template
  /// declaration if II names a template. An optional CXXScope can be
  /// passed to indicate the C++ scope in which the identifier will be
  /// found. 
Action::DeclTy *MinimalAction::isTemplateName(IdentifierInfo &II, Scope *S,
                                              const CXXScopeSpec *SS ) {
  return 0;
}

/// ActOnDeclarator - If this is a typedef declarator, we modify the
/// IdentifierInfo::FETokenInfo field to keep track of this fact, until S is
/// popped.
Action::DeclTy *
MinimalAction::ActOnDeclarator(Scope *S, Declarator &D, DeclTy *LastInGroup) {
  IdentifierInfo *II = D.getIdentifier();
  
  // If there is no identifier associated with this declarator, bail out.
  if (II == 0) return 0;
  
  TypeNameInfo *weCurrentlyHaveTypeInfo = II->getFETokenInfo<TypeNameInfo>();
  bool isTypeName =
    D.getDeclSpec().getStorageClassSpec() == DeclSpec::SCS_typedef;

  // this check avoids creating TypeNameInfo objects for the common case.
  // It does need to handle the uncommon case of shadowing a typedef name with a
  // non-typedef name. e.g. { typedef int a; a xx; { int a; } }
  if (weCurrentlyHaveTypeInfo || isTypeName) {
    // Allocate and add the 'TypeNameInfo' "decl".
    getTable(TypeNameInfoTablePtr)->AddEntry(isTypeName, II);
  
    // Remember that this needs to be removed when the scope is popped.
    S->AddDecl(II);
  } 
  return 0;
}

Action::DeclTy *
MinimalAction::ActOnStartClassInterface(SourceLocation AtInterfaceLoc,
                                        IdentifierInfo *ClassName,
                                        SourceLocation ClassLoc,
                                        IdentifierInfo *SuperName,
                                        SourceLocation SuperLoc,
                                        DeclTy * const *ProtoRefs,
                                        unsigned NumProtocols,
                                        SourceLocation EndProtoLoc,
                                        AttributeList *AttrList) {
  // Allocate and add the 'TypeNameInfo' "decl".
  getTable(TypeNameInfoTablePtr)->AddEntry(true, ClassName);
  return 0;
}

/// ActOnForwardClassDeclaration - 
/// Scope will always be top level file scope. 
Action::DeclTy *
MinimalAction::ActOnForwardClassDeclaration(SourceLocation AtClassLoc,
                                IdentifierInfo **IdentList, unsigned NumElts) {
  for (unsigned i = 0; i != NumElts; ++i) {
    // Allocate and add the 'TypeNameInfo' "decl".
    getTable(TypeNameInfoTablePtr)->AddEntry(true, IdentList[i]);
  
    // Remember that this needs to be removed when the scope is popped.
    TUScope->AddDecl(IdentList[i]);
  }
  return 0;
}

/// ActOnPopScope - When a scope is popped, if any typedefs are now
/// out-of-scope, they are removed from the IdentifierInfo::FETokenInfo field.
void MinimalAction::ActOnPopScope(SourceLocation Loc, Scope *S) {
  TypeNameInfoTable &Table = *getTable(TypeNameInfoTablePtr);
  
  for (Scope::decl_iterator I = S->decl_begin(), E = S->decl_end();
       I != E; ++I) {
    IdentifierInfo &II = *static_cast<IdentifierInfo*>(*I);
    TypeNameInfo *TI = II.getFETokenInfo<TypeNameInfo>();
    assert(TI && "This decl didn't get pushed??");
    
    if (TI) {
      TypeNameInfo *Next = TI->Prev;
      Table.DeleteEntry(TI);
      
      II.setFETokenInfo(Next);
    }
  }
}
