//===--------------------- SemaLookup.cpp - Name Lookup  ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements name lookup for C, C++, Objective-C, and
//  Objective-C++.
//
//===----------------------------------------------------------------------===//
#include "Sema.h"
#include "SemaInherit.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/Parse/DeclSpec.h"
#include "clang/Basic/LangOptions.h"
#include "llvm/ADT/STLExtras.h"
#include <set>

using namespace clang;

/// MaybeConstructOverloadSet - Name lookup has determined that the
/// elements in [I, IEnd) have the name that we are looking for, and
/// *I is a match for the namespace. This routine returns an
/// appropriate Decl for name lookup, which may either be *I or an
/// OverloadeFunctionDecl that represents the overloaded functions in
/// [I, IEnd). 
///
/// The existance of this routine is temporary; users of LookupResult
/// should be able to handle multiple results, to deal with cases of
/// ambiguity and overloaded functions without needing to create a
/// Decl node.
template<typename DeclIterator>
static Decl *
MaybeConstructOverloadSet(ASTContext &Context, 
                          DeclIterator I, DeclIterator IEnd) {
  assert(I != IEnd && "Iterator range cannot be empty");
  assert(!isa<OverloadedFunctionDecl>(*I) && 
         "Cannot have an overloaded function");

  if (isa<FunctionDecl>(*I)) {
    // If we found a function, there might be more functions. If
    // so, collect them into an overload set.
    DeclIterator Last = I;
    OverloadedFunctionDecl *Ovl = 0;
    for (++Last; Last != IEnd && isa<FunctionDecl>(*Last); ++Last) {
      if (!Ovl) {
        // FIXME: We leak this overload set. Eventually, we want to
        // stop building the declarations for these overload sets, so
        // there will be nothing to leak.
        Ovl = OverloadedFunctionDecl::Create(Context, (*I)->getDeclContext(),
                                             (*I)->getDeclName());
        Ovl->addOverload(cast<FunctionDecl>(*I));
      }
      Ovl->addOverload(cast<FunctionDecl>(*Last));
    }
    
    // If we had more than one function, we built an overload
    // set. Return it.
    if (Ovl)
      return Ovl;
  }
  
  return *I;
}

/// @brief Moves the name-lookup results from Other to this LookupResult.
Sema::LookupResult
Sema::LookupResult::CreateLookupResult(ASTContext &Context, 
                                       IdentifierResolver::iterator F, 
                                       IdentifierResolver::iterator L) {
  LookupResult Result;
  Result.Context = &Context;

  if (F != L && isa<FunctionDecl>(*F)) {
    IdentifierResolver::iterator Next = F;
    ++Next;
    if (Next != L && isa<FunctionDecl>(*Next)) {
      Result.StoredKind = OverloadedDeclFromIdResolver;
      Result.First = F.getAsOpaqueValue();
      Result.Last = L.getAsOpaqueValue();
      return Result;
    }
  } 
    
  Result.StoredKind = SingleDecl;
  Result.First = reinterpret_cast<uintptr_t>(*F);
  Result.Last = 0;
  return Result;
}

Sema::LookupResult
Sema::LookupResult::CreateLookupResult(ASTContext &Context, 
                                       DeclContext::lookup_iterator F, 
                                       DeclContext::lookup_iterator L) {
  LookupResult Result;
  Result.Context = &Context;

  if (F != L && isa<FunctionDecl>(*F)) {
    DeclContext::lookup_iterator Next = F;
    ++Next;
    if (Next != L && isa<FunctionDecl>(*Next)) {
      Result.StoredKind = OverloadedDeclFromDeclContext;
      Result.First = reinterpret_cast<uintptr_t>(F);
      Result.Last = reinterpret_cast<uintptr_t>(L);
      return Result;
    }
  }
  
  Result.StoredKind = SingleDecl;
  Result.First = reinterpret_cast<uintptr_t>(*F);
  Result.Last = 0;
  return Result;
}

/// @brief Determine the result of name lookup.
Sema::LookupResult::LookupKind Sema::LookupResult::getKind() const {
  switch (StoredKind) {
  case SingleDecl:
    return (reinterpret_cast<Decl *>(First) != 0)? Found : NotFound;

  case OverloadedDeclFromIdResolver:
  case OverloadedDeclFromDeclContext:
    return FoundOverloaded;

  case AmbiguousLookup:
    return Last? AmbiguousBaseSubobjectTypes : AmbiguousBaseSubobjects;
  }

  // We can't ever get here.
  return NotFound;
}

/// @brief Converts the result of name lookup into a single (possible
/// NULL) pointer to a declaration.
///
/// The resulting declaration will either be the declaration we found
/// (if only a single declaration was found), an
/// OverloadedFunctionDecl (if an overloaded function was found), or
/// NULL (if no declaration was found). This conversion must not be
/// used anywhere where name lookup could result in an ambiguity. 
///
/// The OverloadedFunctionDecl conversion is meant as a stop-gap
/// solution, since it causes the OverloadedFunctionDecl to be
/// leaked. FIXME: Eventually, there will be a better way to iterate
/// over the set of overloaded functions returned by name lookup.
Decl *Sema::LookupResult::getAsDecl() const {
  switch (StoredKind) {
  case SingleDecl:
    return reinterpret_cast<Decl *>(First);

  case OverloadedDeclFromIdResolver:
    return MaybeConstructOverloadSet(*Context,
                         IdentifierResolver::iterator::getFromOpaqueValue(First),
                         IdentifierResolver::iterator::getFromOpaqueValue(Last));

  case OverloadedDeclFromDeclContext:
    return MaybeConstructOverloadSet(*Context, 
                           reinterpret_cast<DeclContext::lookup_iterator>(First),
                           reinterpret_cast<DeclContext::lookup_iterator>(Last));

  case AmbiguousLookup:
    assert(false && 
           "Name lookup returned an ambiguity that could not be handled");
    break;
  }

  return 0;
}

/// @brief Retrieves the BasePaths structure describing an ambiguous
/// name lookup.
BasePaths *Sema::LookupResult::getBasePaths() const {
  assert((StoredKind == AmbiguousLookup) && 
         "getBasePaths can only be used on an ambiguous lookup");
  return reinterpret_cast<BasePaths *>(First);
}

Sema::LookupResult::iterator::reference 
Sema::LookupResult::iterator::operator*() const {
  switch (Result->StoredKind) {
  case SingleDecl:
    return reinterpret_cast<Decl*>(Current);

  case OverloadedDeclFromIdResolver:
    return *IdentifierResolver::iterator::getFromOpaqueValue(Current);

  case OverloadedDeclFromDeclContext:
    return *reinterpret_cast<DeclContext::lookup_iterator>(Current);

  case AmbiguousLookup:
    assert(false && "Cannot look into ambiguous lookup results");
    break;
  }

  return 0;
}

Sema::LookupResult::iterator& Sema::LookupResult::iterator::operator++() {
  switch (Result->StoredKind) {
  case SingleDecl:
    Current = reinterpret_cast<uintptr_t>((Decl*)0);
    break;

  case OverloadedDeclFromIdResolver: {
    IdentifierResolver::iterator I 
      = IdentifierResolver::iterator::getFromOpaqueValue(Current);
    ++I;
    Current = I.getAsOpaqueValue();
    break;
  }

  case OverloadedDeclFromDeclContext: {
    DeclContext::lookup_iterator I 
      = reinterpret_cast<DeclContext::lookup_iterator>(Current);
    ++I;
    Current = reinterpret_cast<uintptr_t>(I);
    break;
  }

  case AmbiguousLookup:
    assert(false && "Cannot look into ambiguous lookup results");
    break;
  }

  return *this;
}

Sema::LookupResult::iterator Sema::LookupResult::begin() {
  assert(StoredKind != AmbiguousLookup && "Lookup into an ambiguous result");
  return iterator(this, First);
}

Sema::LookupResult::iterator Sema::LookupResult::end() {
  assert(StoredKind != AmbiguousLookup && "Lookup into an ambiguous result");
  return iterator(this, Last);
}

// Retrieve the set of identifier namespaces that correspond to a
// specific kind of name lookup.
inline unsigned 
getIdentifierNamespacesFromLookupNameKind(Sema::LookupNameKind NameKind, 
                                          bool CPlusPlus) {
  unsigned IDNS = 0;
  switch (NameKind) {
  case Sema::LookupOrdinaryName:
    IDNS = Decl::IDNS_Ordinary;
    if (CPlusPlus)
      IDNS |= Decl::IDNS_Tag | Decl::IDNS_Member;
    break;

  case Sema::LookupTagName:
    IDNS = Decl::IDNS_Tag;
    break;

  case Sema::LookupMemberName:
    IDNS = Decl::IDNS_Member;
    if (CPlusPlus)
      IDNS |= Decl::IDNS_Tag | Decl::IDNS_Ordinary;    
    break;

  case Sema::LookupNestedNameSpecifierName:
  case Sema::LookupNamespaceName:
    IDNS = Decl::IDNS_Ordinary | Decl::IDNS_Tag | Decl::IDNS_Member;
    break;
  }
  return IDNS;
}

/// @brief Perform unqualified name lookup starting from a given
/// scope.
///
/// Unqualified name lookup (C++ [basic.lookup.unqual], C99 6.2.1) is
/// used to find names within the current scope. For example, 'x' in
/// @code
/// int x;
/// int f() {
///   return x; // unqualified name look finds 'x' in the global scope
/// }
/// @endcode
///
/// Different lookup criteria can find different names. For example, a
/// particular scope can have both a struct and a function of the same
/// name, and each can be found by certain lookup criteria. For more
/// information about lookup criteria, see the documentation for the
/// class LookupCriteria.
///
/// @param S        The scope from which unqualified name lookup will
/// begin. If the lookup criteria permits, name lookup may also search
/// in the parent scopes.
///
/// @param Name     The name of the entity that we are searching for.
///
/// @param Criteria The criteria that this routine will use to
/// determine which names are visible and which names will be
/// found. Note that name lookup will find a name that is visible by
/// the given criteria, but the entity itself may not be semantically
/// correct or even the kind of entity expected based on the
/// lookup. For example, searching for a nested-name-specifier name
/// might result in an EnumDecl, which is visible but is not permitted
/// as a nested-name-specifier in C++03.
///
/// @returns The result of name lookup, which includes zero or more
/// declarations and possibly additional information used to diagnose
/// ambiguities.
Sema::LookupResult 
Sema::LookupName(Scope *S, DeclarationName Name, LookupNameKind NameKind,
                 bool RedeclarationOnly) {
  if (!Name) return LookupResult::CreateLookupResult(Context, 0);

  if (!getLangOptions().CPlusPlus) {
    // Unqualified name lookup in C/Objective-C is purely lexical, so
    // search in the declarations attached to the name.
    unsigned IDNS = 0;
    switch (NameKind) {
    case Sema::LookupOrdinaryName:
      IDNS = Decl::IDNS_Ordinary;
      break;

    case Sema::LookupTagName:
      IDNS = Decl::IDNS_Tag;
      break;

    case Sema::LookupMemberName:
      IDNS = Decl::IDNS_Member;
      break;

    case Sema::LookupNestedNameSpecifierName:
    case Sema::LookupNamespaceName:
      assert(false && "C does not perform these kinds of name lookup");
      break;
    }

    // Scan up the scope chain looking for a decl that matches this
    // identifier that is in the appropriate namespace.  This search
    // should not take long, as shadowing of names is uncommon, and
    // deep shadowing is extremely uncommon.
    for (IdentifierResolver::iterator I = IdResolver.begin(Name),
                                   IEnd = IdResolver.end(); 
         I != IEnd; ++I)
      if ((*I)->isInIdentifierNamespace(IDNS))
        return LookupResult::CreateLookupResult(Context, *I);
  } else {
    unsigned IDNS 
      = getIdentifierNamespacesFromLookupNameKind(NameKind, 
                                                  getLangOptions().CPlusPlus);

    // Unqualified name lookup in C++ requires looking into scopes
    // that aren't strictly lexical, and therefore we walk through the
    // context as well as walking through the scopes.

    // FIXME: does "true" for LookInParentCtx actually make sense?
    IdentifierResolver::iterator I = IdResolver.begin(Name),
                              IEnd = IdResolver.end();
    for (; S; S = S->getParent()) {
      // Check whether the IdResolver has anything in this scope.
      for (; I != IEnd && S->isDeclScope(*I); ++I) {
        if (isAcceptableLookupResult(*I, NameKind, IDNS)) {
          // We found something.  Look for anything else in our scope
          // with this same name and in an acceptable identifier
          // namespace, so that we can construct an overload set if we
          // need to.
          IdentifierResolver::iterator LastI = I;
          for (++LastI; LastI != IEnd; ++LastI) {
            if (!S->isDeclScope(*LastI))
              break;
          }
          return LookupResult::CreateLookupResult(Context, I, LastI);
        }
      }
      
      // If there is an entity associated with this scope, it's a
      // DeclContext. We might need to perform qualified lookup into
      // it.
      // FIXME: We're performing redundant lookups here, where the
      // scope stack mirrors the semantic nested of classes and
      // namespaces. We can save some work by checking the lexical
      // scope against the semantic scope and avoiding any lookups
      // when they are the same.
      // FIXME: In some cases, we know that every name that could be
      // found by this qualified name lookup will also be on the
      // identifier chain. For example, inside a class without any
      // base classes, we never need to perform qualified lookup
      // because all of the members are on top of the identifier
      // chain. However, we cannot perform this optimization when the
      // lexical and semantic scopes don't line up, e.g., in an
      // out-of-line member definition.
      DeclContext *Ctx = static_cast<DeclContext *>(S->getEntity());
      while (Ctx && Ctx->isFunctionOrMethod())
        Ctx = Ctx->getParent();
      while (Ctx && (Ctx->isNamespace() || Ctx->isRecord())) {
        // Look for declarations of this name in this scope.
        if (LookupResult Result = LookupQualifiedName(Ctx, Name, NameKind, 
                                                      RedeclarationOnly))
          return Result;
        
        if (RedeclarationOnly && !Ctx->isTransparentContext())
          return LookupResult::CreateLookupResult(Context, 0);

        Ctx = Ctx->getParent();
      }
    }
  }

  // If we didn't find a use of this identifier, and if the identifier
  // corresponds to a compiler builtin, create the decl object for the builtin
  // now, injecting it into translation unit scope, and return it.
  if (NameKind == LookupOrdinaryName) {
    IdentifierInfo *II = Name.getAsIdentifierInfo();
    if (II) {
      // If this is a builtin on this (or all) targets, create the decl.
      if (unsigned BuiltinID = II->getBuiltinID())
        return LookupResult::CreateLookupResult(Context,
                            LazilyCreateBuiltin((IdentifierInfo *)II, BuiltinID,
                                                S));
    }
    if (getLangOptions().ObjC1 && II) {
      // @interface and @compatibility_alias introduce typedef-like names.
      // Unlike typedef's, they can only be introduced at file-scope (and are 
      // therefore not scoped decls). They can, however, be shadowed by
      // other names in IDNS_Ordinary.
      ObjCInterfaceDeclsTy::iterator IDI = ObjCInterfaceDecls.find(II);
      if (IDI != ObjCInterfaceDecls.end())
        return LookupResult::CreateLookupResult(Context, IDI->second);
      ObjCAliasTy::iterator I = ObjCAliasDecls.find(II);
      if (I != ObjCAliasDecls.end())
        return LookupResult::CreateLookupResult(Context, 
                                                I->second->getClassInterface());
    }
  }
  return LookupResult::CreateLookupResult(Context, 0);
}

/// @brief Perform qualified name lookup into a given context.
///
/// Qualified name lookup (C++ [basic.lookup.qual]) is used to find
/// names when the context of those names is explicit specified, e.g.,
/// "std::vector" or "x->member".
///
/// Different lookup criteria can find different names. For example, a
/// particular scope can have both a struct and a function of the same
/// name, and each can be found by certain lookup criteria. For more
/// information about lookup criteria, see the documentation for the
/// class LookupCriteria.
///
/// @param LookupCtx The context in which qualified name lookup will
/// search. If the lookup criteria permits, name lookup may also search
/// in the parent contexts or (for C++ classes) base classes.
///
/// @param Name     The name of the entity that we are searching for.
///
/// @param Criteria The criteria that this routine will use to
/// determine which names are visible and which names will be
/// found. Note that name lookup will find a name that is visible by
/// the given criteria, but the entity itself may not be semantically
/// correct or even the kind of entity expected based on the
/// lookup. For example, searching for a nested-name-specifier name
/// might result in an EnumDecl, which is visible but is not permitted
/// as a nested-name-specifier in C++03.
///
/// @returns The result of name lookup, which includes zero or more
/// declarations and possibly additional information used to diagnose
/// ambiguities.
Sema::LookupResult
Sema::LookupQualifiedName(DeclContext *LookupCtx, DeclarationName Name,
                          LookupNameKind NameKind, bool RedeclarationOnly) {
  assert(LookupCtx && "Sema::LookupQualifiedName requires a lookup context");
  
  if (!Name) return LookupResult::CreateLookupResult(Context, 0);

  // If we're performing qualified name lookup (e.g., lookup into a
  // struct), find fields as part of ordinary name lookup.
  unsigned IDNS
    = getIdentifierNamespacesFromLookupNameKind(NameKind, 
                                                getLangOptions().CPlusPlus);
  if (NameKind == LookupOrdinaryName)
    IDNS |= Decl::IDNS_Member;

  // Perform qualified name lookup into the LookupCtx.
  DeclContext::lookup_iterator I, E;
  for (llvm::tie(I, E) = LookupCtx->lookup(Name); I != E; ++I)
    if (isAcceptableLookupResult(*I, NameKind, IDNS))
      return LookupResult::CreateLookupResult(Context, I, E);

  // If this isn't a C++ class or we aren't allowed to look into base
  // classes, we're done.
  if (RedeclarationOnly || !isa<CXXRecordDecl>(LookupCtx))
    return LookupResult::CreateLookupResult(Context, 0);

  // Perform lookup into our base classes.
  BasePaths Paths;
  Paths.setOrigin(Context.getTypeDeclType(cast<RecordDecl>(LookupCtx)));

  // Look for this member in our base classes
  if (!LookupInBases(cast<CXXRecordDecl>(LookupCtx), 
                     MemberLookupCriteria(Name, NameKind, IDNS), Paths))
    return LookupResult::CreateLookupResult(Context, 0);

  // C++ [class.member.lookup]p2:
  //   [...] If the resulting set of declarations are not all from
  //   sub-objects of the same type, or the set has a nonstatic member
  //   and includes members from distinct sub-objects, there is an
  //   ambiguity and the program is ill-formed. Otherwise that set is
  //   the result of the lookup.
  // FIXME: support using declarations!
  QualType SubobjectType;
  int SubobjectNumber = 0;
  for (BasePaths::paths_iterator Path = Paths.begin(), PathEnd = Paths.end();
       Path != PathEnd; ++Path) {
    const BasePathElement &PathElement = Path->back();

    // Determine whether we're looking at a distinct sub-object or not.
    if (SubobjectType.isNull()) {
      // This is the first subobject we've looked at. Record it's type.
      SubobjectType = Context.getCanonicalType(PathElement.Base->getType());
      SubobjectNumber = PathElement.SubobjectNumber;
    } else if (SubobjectType 
                 != Context.getCanonicalType(PathElement.Base->getType())) {
      // We found members of the given name in two subobjects of
      // different types. This lookup is ambiguous.
      BasePaths *PathsOnHeap = new BasePaths;
      PathsOnHeap->swap(Paths);
      return LookupResult::CreateLookupResult(Context, PathsOnHeap, true);
    } else if (SubobjectNumber != PathElement.SubobjectNumber) {
      // We have a different subobject of the same type.

      // C++ [class.member.lookup]p5:
      //   A static member, a nested type or an enumerator defined in
      //   a base class T can unambiguously be found even if an object
      //   has more than one base class subobject of type T. 
      Decl *FirstDecl = *Path->Decls.first;
      if (isa<VarDecl>(FirstDecl) ||
          isa<TypeDecl>(FirstDecl) ||
          isa<EnumConstantDecl>(FirstDecl))
        continue;

      if (isa<CXXMethodDecl>(FirstDecl)) {
        // Determine whether all of the methods are static.
        bool AllMethodsAreStatic = true;
        for (DeclContext::lookup_iterator Func = Path->Decls.first;
             Func != Path->Decls.second; ++Func) {
          if (!isa<CXXMethodDecl>(*Func)) {
            assert(isa<TagDecl>(*Func) && "Non-function must be a tag decl");
            break;
          }

          if (!cast<CXXMethodDecl>(*Func)->isStatic()) {
            AllMethodsAreStatic = false;
            break;
          }
        }

        if (AllMethodsAreStatic)
          continue;
      }

      // We have found a nonstatic member name in multiple, distinct
      // subobjects. Name lookup is ambiguous.
      BasePaths *PathsOnHeap = new BasePaths;
      PathsOnHeap->swap(Paths);
      return LookupResult::CreateLookupResult(Context, PathsOnHeap, false);
    }
  }

  // Lookup in a base class succeeded; return these results.

  // If we found a function declaration, return an overload set.
  if (isa<FunctionDecl>(*Paths.front().Decls.first))
    return LookupResult::CreateLookupResult(Context, 
                        Paths.front().Decls.first, Paths.front().Decls.second);

  // We found a non-function declaration; return a single declaration.
  return LookupResult::CreateLookupResult(Context, *Paths.front().Decls.first);
}

/// @brief Performs name lookup for a name that was parsed in the
/// source code, and may contain a C++ scope specifier.
///
/// This routine is a convenience routine meant to be called from
/// contexts that receive a name and an optional C++ scope specifier
/// (e.g., "N::M::x"). It will then perform either qualified or
/// unqualified name lookup (with LookupQualifiedName or LookupName,
/// respectively) on the given name and return those results.
///
/// @param S        The scope from which unqualified name lookup will
/// begin.
/// 
/// @param SS       An optional C++ scope-specified, e.g., "::N::M".
///
/// @param Name     The name of the entity that name lookup will
/// search for.
///
/// @returns The result of qualified or unqualified name lookup.
Sema::LookupResult
Sema::LookupParsedName(Scope *S, const CXXScopeSpec *SS, 
                       DeclarationName Name, LookupNameKind NameKind,
                       bool RedeclarationOnly) {
  if (SS) {
    if (SS->isInvalid())
      return LookupResult::CreateLookupResult(Context, 0);

    if (SS->isSet())
      return LookupQualifiedName(static_cast<DeclContext *>(SS->getScopeRep()),
                                 Name, NameKind, RedeclarationOnly);
  }

  return LookupName(S, Name, NameKind, RedeclarationOnly);
}

/// @brief Produce a diagnostic describing the ambiguity that resulted
/// from name lookup.
///
/// @param Result       The ambiguous name lookup result.
/// 
/// @param Name         The name of the entity that name lookup was
/// searching for.
///
/// @param NameLoc      The location of the name within the source code.
///
/// @param LookupRange  A source range that provides more
/// source-location information concerning the lookup itself. For
/// example, this range might highlight a nested-name-specifier that
/// precedes the name.
///
/// @returns true
bool Sema::DiagnoseAmbiguousLookup(LookupResult &Result, DeclarationName Name,
                                   SourceLocation NameLoc, 
                                   SourceRange LookupRange) {
  assert(Result.isAmbiguous() && "Lookup result must be ambiguous");

  BasePaths *Paths = Result.getBasePaths();
  if (Result.getKind() == LookupResult::AmbiguousBaseSubobjects) {
    QualType SubobjectType = Paths->front().back().Base->getType();
    Diag(NameLoc, diag::err_ambiguous_member_multiple_subobjects)
      << Name << SubobjectType << getAmbiguousPathsDisplayString(*Paths)
      << LookupRange;

    DeclContext::lookup_iterator Found = Paths->front().Decls.first;
    while (isa<CXXMethodDecl>(*Found) && cast<CXXMethodDecl>(*Found)->isStatic())
      ++Found;

    Diag((*Found)->getLocation(), diag::note_ambiguous_member_found);

    return true;
  } 

  assert(Result.getKind() == LookupResult::AmbiguousBaseSubobjectTypes &&
         "Unhandled form of name lookup ambiguity");

  Diag(NameLoc, diag::err_ambiguous_member_multiple_subobject_types)
    << Name << LookupRange;

  std::set<Decl *> DeclsPrinted;
  for (BasePaths::paths_iterator Path = Paths->begin(), PathEnd = Paths->end();
       Path != PathEnd; ++Path) {
    Decl *D = *Path->Decls.first;
    if (DeclsPrinted.insert(D).second)
      Diag(D->getLocation(), diag::note_ambiguous_member_found);
  }

  delete Paths;

  return true;
}
