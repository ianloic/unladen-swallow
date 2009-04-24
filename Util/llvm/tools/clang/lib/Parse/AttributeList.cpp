//===--- AttributeList.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the AttributeList class implementation
//
//===----------------------------------------------------------------------===//

#include "clang/Parse/AttributeList.h"
using namespace clang;

AttributeList::AttributeList(IdentifierInfo *aName, SourceLocation aLoc,
                             IdentifierInfo *pName, SourceLocation pLoc,
                             Action::ExprTy **elist, unsigned numargs, 
                             AttributeList *n)
  : AttrName(aName), AttrLoc(aLoc), ParmName(pName), ParmLoc(pLoc),
    NumArgs(numargs), Next(n) {
  Args = new Action::ExprTy*[numargs];
  for (unsigned i = 0; i != numargs; ++i)
    Args[i] = elist[i];
}

AttributeList::~AttributeList() {
  if (Args) {
    // FIXME: before we delete the vector, we need to make sure the Expr's 
    // have been deleted. Since Action::ExprTy is "void", we are dependent
    // on the actions module for actually freeing the memory. The specific
    // hooks are ActOnDeclarator, ActOnTypeName, ActOnParamDeclaratorType, 
    // ParseField, ParseTag. Once these routines have freed the expression, 
    // they should zero out the Args slot (to indicate the memory has been 
    // freed). If any element of the vector is non-null, we should assert.
    delete [] Args;
  }
  delete Next;
}

AttributeList::Kind AttributeList::getKind(const IdentifierInfo *Name) {
  const char *Str = Name->getName();
  unsigned Len = Name->getLength();

  // Normalize the attribute name, __foo__ becomes foo.
  if (Len > 4 && Str[0] == '_' && Str[1] == '_' &&
      Str[Len - 2] == '_' && Str[Len - 1] == '_') {
    Str += 2;
    Len -= 4;
  }
  
  // FIXME: Hand generating this is neither smart nor efficient.
  switch (Len) {
  case 4:
    if (!memcmp(Str, "weak", 4)) return AT_weak;
    if (!memcmp(Str, "pure", 4)) return AT_pure;
    if (!memcmp(Str, "mode", 4)) return AT_mode;
    break;
  case 5:
    if (!memcmp(Str, "alias", 5)) return AT_alias;
    break;
  case 6:
    if (!memcmp(Str, "packed", 6)) return AT_packed;
    if (!memcmp(Str, "malloc", 6)) return AT_malloc;
    if (!memcmp(Str, "format", 6)) return AT_format;
    if (!memcmp(Str, "unused", 6)) return AT_unused;
    if (!memcmp(Str, "blocks", 6)) return AT_blocks;
    break;
  case 7:
    if (!memcmp(Str, "aligned", 7)) return AT_aligned;
    if (!memcmp(Str, "nothrow", 7)) return AT_nothrow;
    if (!memcmp(Str, "nonnull", 7)) return AT_nonnull;
    if (!memcmp(Str, "objc_gc", 7)) return AT_objc_gc;
    if (!memcmp(Str, "stdcall", 7)) return AT_stdcall;
    if (!memcmp(Str, "cleanup", 7)) return AT_cleanup;
    break;
  case 8:
    if (!memcmp(Str, "annotate", 8)) return AT_annotate;
    if (!memcmp(Str, "noreturn", 8)) return AT_noreturn;
    if (!memcmp(Str, "noinline", 8)) return AT_noinline;
    if (!memcmp(Str, "fastcall", 8)) return AT_fastcall;
    if (!memcmp(Str, "iboutlet", 8)) return AT_IBOutlet;
    if (!memcmp(Str, "sentinel", 8)) return AT_sentinel;
    if (!memcmp(Str, "NSObject", 8)) return AT_nsobject;
    break;
  case 9:
    if (!memcmp(Str, "dllimport", 9)) return AT_dllimport;
    if (!memcmp(Str, "dllexport", 9)) return AT_dllexport;
    break;
  case 10:
    if (!memcmp(Str, "deprecated", 10)) return AT_deprecated;
    if (!memcmp(Str, "visibility", 10)) return AT_visibility;
    if (!memcmp(Str, "destructor", 10)) return AT_destructor;
    break;
  case 11:
    if (!memcmp(Str, "vector_size", 11)) return AT_vector_size;
    if (!memcmp(Str, "constructor", 11)) return AT_constructor;
    if (!memcmp(Str, "unavailable", 11)) return AT_unavailable;
    break;
  case 13:
    if (!memcmp(Str, "address_space", 13)) return AT_address_space;
    if (!memcmp(Str, "always_inline", 13)) return AT_always_inline;
    break;
  case 15:
    if (!memcmp(Str, "ext_vector_type", 15)) return AT_ext_vector_type;
    break;
  case 17:
    if (!memcmp(Str, "transparent_union", 17)) return AT_transparent_union;
    break;
  case 18:
    if (!memcmp(Str, "warn_unused_result", 18)) return AT_warn_unused_result;
    break;
  }
  return UnknownAttribute;
}
