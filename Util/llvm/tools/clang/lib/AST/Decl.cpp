//===--- Decl.cpp - Declaration AST Node Implementation -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Decl subclasses.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/IdentifierTable.h"
#include <vector>

using namespace clang;

void Attr::Destroy(ASTContext &C) {
  if (Next) {
    Next->Destroy(C);
    Next = 0;
  }
  this->~Attr();
  C.Deallocate((void*)this);
}


//===----------------------------------------------------------------------===//
// Decl Allocation/Deallocation Method Implementations
//===----------------------------------------------------------------------===//
 

TranslationUnitDecl *TranslationUnitDecl::Create(ASTContext &C) {
  return new (C) TranslationUnitDecl();
}

NamespaceDecl *NamespaceDecl::Create(ASTContext &C, DeclContext *DC,
                                     SourceLocation L, IdentifierInfo *Id) {
  return new (C) NamespaceDecl(DC, L, Id);
}

void NamespaceDecl::Destroy(ASTContext& C) {
  // NamespaceDecl uses "NextDeclarator" to chain namespace declarations
  // together. They are all top-level Decls.
  
  this->~NamespaceDecl();
  C.Deallocate((void *)this);
}


ImplicitParamDecl *ImplicitParamDecl::Create(ASTContext &C, DeclContext *DC,
    SourceLocation L, IdentifierInfo *Id, QualType T) {
  return new (C) ImplicitParamDecl(ImplicitParam, DC, L, Id, T);
}

const char *VarDecl::getStorageClassSpecifierString(StorageClass SC) {
  switch (SC) {
  case VarDecl::None:          break;
  case VarDecl::Auto:          return "auto"; break;
  case VarDecl::Extern:        return "extern"; break;
  case VarDecl::PrivateExtern: return "__private_extern__"; break; 
  case VarDecl::Register:      return "register"; break;
  case VarDecl::Static:        return "static"; break; 
  }

  assert(0 && "Invalid storage class");
  return 0;
}

ParmVarDecl *ParmVarDecl::Create(ASTContext &C, DeclContext *DC,
                                 SourceLocation L, IdentifierInfo *Id,
                                 QualType T, StorageClass S,
                                 Expr *DefArg) {
  return new (C) ParmVarDecl(ParmVar, DC, L, Id, T, S, DefArg);
}

QualType ParmVarDecl::getOriginalType() const {
  if (const OriginalParmVarDecl *PVD = 
      dyn_cast<OriginalParmVarDecl>(this))
    return PVD->OriginalType;
  return getType();
}

bool VarDecl::isExternC(ASTContext &Context) const {
  if (!Context.getLangOptions().CPlusPlus)
    return (getDeclContext()->isTranslationUnit() && 
            getStorageClass() != Static) ||
      (getDeclContext()->isFunctionOrMethod() && hasExternalStorage());

  for (const DeclContext *DC = getDeclContext(); !DC->isTranslationUnit(); 
       DC = DC->getParent()) {
    if (const LinkageSpecDecl *Linkage = dyn_cast<LinkageSpecDecl>(DC))  {
      if (Linkage->getLanguage() == LinkageSpecDecl::lang_c)
        return getStorageClass() != Static;

      break;
    }

    if (DC->isFunctionOrMethod())
      return false;
  }

  return false;
}

OriginalParmVarDecl *OriginalParmVarDecl::Create(
                                 ASTContext &C, DeclContext *DC,
                                 SourceLocation L, IdentifierInfo *Id,
                                 QualType T, QualType OT, StorageClass S,
                                 Expr *DefArg) {
  return new (C) OriginalParmVarDecl(DC, L, Id, T, OT, S, DefArg);
}

FunctionDecl *FunctionDecl::Create(ASTContext &C, DeclContext *DC,
                                   SourceLocation L, 
                                   DeclarationName N, QualType T, 
                                   StorageClass S, bool isInline, 
                                   bool hasPrototype,
                                   SourceLocation TypeSpecStartLoc) {
  FunctionDecl *New 
    = new (C) FunctionDecl(Function, DC, L, N, T, S, isInline, 
                           TypeSpecStartLoc);
  New->HasPrototype = hasPrototype;
  return New;
}

BlockDecl *BlockDecl::Create(ASTContext &C, DeclContext *DC, SourceLocation L) {
  return new (C) BlockDecl(DC, L);
}

FieldDecl *FieldDecl::Create(ASTContext &C, DeclContext *DC, SourceLocation L,
                             IdentifierInfo *Id, QualType T, Expr *BW,
                             bool Mutable) {
  return new (C) FieldDecl(Decl::Field, DC, L, Id, T, BW, Mutable);
}

bool FieldDecl::isAnonymousStructOrUnion() const {
  if (!isImplicit() || getDeclName())
    return false;
  
  if (const RecordType *Record = getType()->getAsRecordType())
    return Record->getDecl()->isAnonymousStructOrUnion();

  return false;
}

EnumConstantDecl *EnumConstantDecl::Create(ASTContext &C, EnumDecl *CD,
                                           SourceLocation L,
                                           IdentifierInfo *Id, QualType T,
                                           Expr *E, const llvm::APSInt &V) {
  return new (C) EnumConstantDecl(CD, L, Id, T, E, V);
}

void EnumConstantDecl::Destroy(ASTContext& C) {
  if (Init) Init->Destroy(C);
  Decl::Destroy(C);
}

TypedefDecl *TypedefDecl::Create(ASTContext &C, DeclContext *DC,
                                 SourceLocation L,
                                 IdentifierInfo *Id, QualType T) {
  return new (C) TypedefDecl(DC, L, Id, T);
}

EnumDecl *EnumDecl::Create(ASTContext &C, DeclContext *DC, SourceLocation L,
                           IdentifierInfo *Id,
                           EnumDecl *PrevDecl) {
  EnumDecl *Enum = new (C) EnumDecl(DC, L, Id);
  C.getTypeDeclType(Enum, PrevDecl);
  return Enum;
}

void EnumDecl::Destroy(ASTContext& C) {
  Decl::Destroy(C);
}

void EnumDecl::completeDefinition(ASTContext &C, QualType NewType) {
  assert(!isDefinition() && "Cannot redefine enums!");
  IntegerType = NewType;
  TagDecl::completeDefinition();
}

FileScopeAsmDecl *FileScopeAsmDecl::Create(ASTContext &C, DeclContext *DC,
                                           SourceLocation L,
                                           StringLiteral *Str) {
  return new (C) FileScopeAsmDecl(DC, L, Str);
}

//===----------------------------------------------------------------------===//
// NamedDecl Implementation
//===----------------------------------------------------------------------===//

std::string NamedDecl::getQualifiedNameAsString() const {
  std::vector<std::string> Names;
  std::string QualName;
  const DeclContext *Ctx = getDeclContext();

  if (Ctx->isFunctionOrMethod())
    return getNameAsString();

  while (Ctx) {
    if (Ctx->isFunctionOrMethod())
      // FIXME: That probably will happen, when D was member of local
      // scope class/struct/union. How do we handle this case?
      break;

    if (const NamedDecl *ND = dyn_cast<NamedDecl>(Ctx))
      Names.push_back(ND->getNameAsString());
    else
      break;

    Ctx = Ctx->getParent();
  }

  std::vector<std::string>::reverse_iterator
    I = Names.rbegin(),
    End = Names.rend();

  for (; I!=End; ++I)
    QualName += *I + "::";

  QualName += getNameAsString();

  return QualName;
}


bool NamedDecl::declarationReplaces(NamedDecl *OldD) const {
  assert(getDeclName() == OldD->getDeclName() && "Declaration name mismatch");

  // UsingDirectiveDecl's are not really NamedDecl's, and all have same name.
  // We want to keep it, unless it nominates same namespace.
  if (getKind() == Decl::UsingDirective) {
    return cast<UsingDirectiveDecl>(this)->getNominatedNamespace() ==
           cast<UsingDirectiveDecl>(OldD)->getNominatedNamespace();
  }
           
  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(this))
    // For function declarations, we keep track of redeclarations.
    return FD->getPreviousDeclaration() == OldD;

  // For method declarations, we keep track of redeclarations.
  if (isa<ObjCMethodDecl>(this))
    return false;
    
  // For non-function declarations, if the declarations are of the
  // same kind then this must be a redeclaration, or semantic analysis
  // would not have given us the new declaration.
  return this->getKind() == OldD->getKind();
}

bool NamedDecl::hasLinkage() const {
  if (const VarDecl *VD = dyn_cast<VarDecl>(this))
    return VD->hasExternalStorage() || VD->isFileVarDecl();

  if (isa<FunctionDecl>(this) && !isa<CXXMethodDecl>(this))
    return true;

  return false;
}

//===----------------------------------------------------------------------===//
// VarDecl Implementation
//===----------------------------------------------------------------------===//

VarDecl *VarDecl::Create(ASTContext &C, DeclContext *DC, SourceLocation L,
                         IdentifierInfo *Id, QualType T, StorageClass S, 
                         SourceLocation TypeSpecStartLoc) {
  return new (C) VarDecl(Var, DC, L, Id, T, S, TypeSpecStartLoc);
}

void VarDecl::Destroy(ASTContext& C) {
  Expr *Init = getInit();
  if (Init)
    Init->Destroy(C);
  this->~VarDecl();
  C.Deallocate((void *)this);
}

VarDecl::~VarDecl() {
}

bool VarDecl::isTentativeDefinition(ASTContext &Context) const {
  if (!isFileVarDecl() || Context.getLangOptions().CPlusPlus)
    return false;

  const VarDecl *Def = 0;
  return (!getDefinition(Def) &&
          (getStorageClass() == None || getStorageClass() == Static));
}

const Expr *VarDecl::getDefinition(const VarDecl *&Def) const {
  Def = this;
  while (Def && !Def->getInit())
    Def = Def->getPreviousDeclaration();

  return Def? Def->getInit() : 0;
}

//===----------------------------------------------------------------------===//
// FunctionDecl Implementation
//===----------------------------------------------------------------------===//

void FunctionDecl::Destroy(ASTContext& C) {
  if (Body && Body.isOffset())
    Body.get(C.getExternalSource())->Destroy(C);

  for (param_iterator I=param_begin(), E=param_end(); I!=E; ++I)
    (*I)->Destroy(C);

  C.Deallocate(ParamInfo);

  Decl::Destroy(C);
}


Stmt *FunctionDecl::getBody(ASTContext &Context,
                            const FunctionDecl *&Definition) const {
  for (const FunctionDecl *FD = this; FD != 0; FD = FD->PreviousDeclaration) {
    if (FD->Body) {
      Definition = FD;
      return FD->Body.get(Context.getExternalSource());
    }
  }

  return 0;
}

Stmt *FunctionDecl::getBodyIfAvailable() const {
  for (const FunctionDecl *FD = this; FD != 0; FD = FD->PreviousDeclaration) {
    if (FD->Body && !FD->Body.isOffset()) {
      return FD->Body.get(0);
    }
  }

  return 0;
}

bool FunctionDecl::isMain() const {
  return getDeclContext()->getLookupContext()->isTranslationUnit() &&
    getIdentifier() && getIdentifier()->isStr("main");
}

bool FunctionDecl::isExternC(ASTContext &Context) const {
  // In C, any non-static, non-overloadable function has external
  // linkage.
  if (!Context.getLangOptions().CPlusPlus)
    return getStorageClass() != Static && !getAttr<OverloadableAttr>();

  for (const DeclContext *DC = getDeclContext(); !DC->isTranslationUnit(); 
       DC = DC->getParent()) {
    if (const LinkageSpecDecl *Linkage = dyn_cast<LinkageSpecDecl>(DC))  {
      if (Linkage->getLanguage() == LinkageSpecDecl::lang_c)
        return getStorageClass() != Static && !getAttr<OverloadableAttr>();

      break;
    }
  }

  return false;
}

bool FunctionDecl::isGlobal() const {
  if (const CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(this))
    return Method->isStatic();

  if (getStorageClass() == Static)
    return false;

  for (const DeclContext *DC = getDeclContext(); 
       DC->isNamespace();
       DC = DC->getParent()) {
    if (const NamespaceDecl *Namespace = cast<NamespaceDecl>(DC)) {
      if (!Namespace->getDeclName())
        return false;
      break;
    }
  }

  return true;
}

/// \brief Returns a value indicating whether this function
/// corresponds to a builtin function.
///
/// The function corresponds to a built-in function if it is
/// declared at translation scope or within an extern "C" block and
/// its name matches with the name of a builtin. The returned value
/// will be 0 for functions that do not correspond to a builtin, a
/// value of type \c Builtin::ID if in the target-independent range 
/// \c [1,Builtin::First), or a target-specific builtin value.
unsigned FunctionDecl::getBuiltinID(ASTContext &Context) const {
  if (!getIdentifier() || !getIdentifier()->getBuiltinID())
    return 0;

  unsigned BuiltinID = getIdentifier()->getBuiltinID();
  if (!Context.BuiltinInfo.isPredefinedLibFunction(BuiltinID))
    return BuiltinID;

  // This function has the name of a known C library
  // function. Determine whether it actually refers to the C library
  // function or whether it just has the same name.

  // If this is a static function, it's not a builtin.
  if (getStorageClass() == Static)
    return 0;

  // If this function is at translation-unit scope and we're not in
  // C++, it refers to the C library function.
  if (!Context.getLangOptions().CPlusPlus &&
      getDeclContext()->isTranslationUnit())
    return BuiltinID;

  // If the function is in an extern "C" linkage specification and is
  // not marked "overloadable", it's the real function.
  if (isa<LinkageSpecDecl>(getDeclContext()) &&
      cast<LinkageSpecDecl>(getDeclContext())->getLanguage() 
        == LinkageSpecDecl::lang_c &&
      !getAttr<OverloadableAttr>())
    return BuiltinID;

  // Not a builtin
  return 0;
}


/// getNumParams - Return the number of parameters this function must have
/// based on its FunctionType.  This is the length of the PararmInfo array
/// after it has been created.
unsigned FunctionDecl::getNumParams() const {
  const FunctionType *FT = getType()->getAsFunctionType();
  if (isa<FunctionNoProtoType>(FT))
    return 0;
  return cast<FunctionProtoType>(FT)->getNumArgs();
  
}

void FunctionDecl::setParams(ASTContext& C, ParmVarDecl **NewParamInfo,
                             unsigned NumParams) {
  assert(ParamInfo == 0 && "Already has param info!");
  assert(NumParams == getNumParams() && "Parameter count mismatch!");
  
  // Zero params -> null pointer.
  if (NumParams) {
    void *Mem = C.Allocate(sizeof(ParmVarDecl*)*NumParams);
    ParamInfo = new (Mem) ParmVarDecl*[NumParams];
    memcpy(ParamInfo, NewParamInfo, sizeof(ParmVarDecl*)*NumParams);
  }
}

/// getMinRequiredArguments - Returns the minimum number of arguments
/// needed to call this function. This may be fewer than the number of
/// function parameters, if some of the parameters have default
/// arguments (in C++).
unsigned FunctionDecl::getMinRequiredArguments() const {
  unsigned NumRequiredArgs = getNumParams();
  while (NumRequiredArgs > 0
         && getParamDecl(NumRequiredArgs-1)->getDefaultArg())
    --NumRequiredArgs;

  return NumRequiredArgs;
}

bool FunctionDecl::hasActiveGNUInlineAttribute() const {
  if (!isInline() || !hasAttr<GNUInlineAttr>())
    return false;

  for (const FunctionDecl *FD = getPreviousDeclaration(); FD; 
       FD = FD->getPreviousDeclaration()) {
    if (FD->isInline() && !FD->hasAttr<GNUInlineAttr>())
      return false;
  }

  return true;
}

bool FunctionDecl::isExternGNUInline() const {
  if (!hasActiveGNUInlineAttribute())
    return false;

  for (const FunctionDecl *FD = this; FD; FD = FD->getPreviousDeclaration())
    if (FD->getStorageClass() == Extern && FD->hasAttr<GNUInlineAttr>())
      return true;

  return false;
}

/// getOverloadedOperator - Which C++ overloaded operator this
/// function represents, if any.
OverloadedOperatorKind FunctionDecl::getOverloadedOperator() const {
  if (getDeclName().getNameKind() == DeclarationName::CXXOperatorName)
    return getDeclName().getCXXOverloadedOperator();
  else
    return OO_None;
}

//===----------------------------------------------------------------------===//
// TagDecl Implementation
//===----------------------------------------------------------------------===//

void TagDecl::startDefinition() {
  TagType *TagT = const_cast<TagType *>(TypeForDecl->getAsTagType());
  TagT->decl.setPointer(this);
  TagT->getAsTagType()->decl.setInt(1);
}

void TagDecl::completeDefinition() {
  assert((!TypeForDecl || 
          TypeForDecl->getAsTagType()->decl.getPointer() == this) &&
         "Attempt to redefine a tag definition?");
  IsDefinition = true;
  TagType *TagT = const_cast<TagType *>(TypeForDecl->getAsTagType());
  TagT->decl.setPointer(this);
  TagT->decl.setInt(0);
}

TagDecl* TagDecl::getDefinition(ASTContext& C) const {
  QualType T = C.getTypeDeclType(const_cast<TagDecl*>(this));
  TagDecl* D = cast<TagDecl>(T->getAsTagType()->getDecl());
  return D->isDefinition() ? D : 0;
}

//===----------------------------------------------------------------------===//
// RecordDecl Implementation
//===----------------------------------------------------------------------===//

RecordDecl::RecordDecl(Kind DK, TagKind TK, DeclContext *DC, SourceLocation L,
                       IdentifierInfo *Id)
  : TagDecl(DK, TK, DC, L, Id) {
  HasFlexibleArrayMember = false;
  AnonymousStructOrUnion = false;
  assert(classof(static_cast<Decl*>(this)) && "Invalid Kind!");
}

RecordDecl *RecordDecl::Create(ASTContext &C, TagKind TK, DeclContext *DC,
                               SourceLocation L, IdentifierInfo *Id,
                               RecordDecl* PrevDecl) {
  
  RecordDecl* R = new (C) RecordDecl(Record, TK, DC, L, Id);
  C.getTypeDeclType(R, PrevDecl);
  return R;
}

RecordDecl::~RecordDecl() {
}

void RecordDecl::Destroy(ASTContext& C) {
  TagDecl::Destroy(C);
}

bool RecordDecl::isInjectedClassName() const {
  return isImplicit() && getDeclName() && getDeclContext()->isRecord() && 
    cast<RecordDecl>(getDeclContext())->getDeclName() == getDeclName();
}

/// completeDefinition - Notes that the definition of this type is now
/// complete.
void RecordDecl::completeDefinition(ASTContext& C) {
  assert(!isDefinition() && "Cannot redefine record!");
  TagDecl::completeDefinition();
}

//===----------------------------------------------------------------------===//
// BlockDecl Implementation
//===----------------------------------------------------------------------===//

BlockDecl::~BlockDecl() {
}

void BlockDecl::Destroy(ASTContext& C) {
  if (Body)
    Body->Destroy(C);

  for (param_iterator I=param_begin(), E=param_end(); I!=E; ++I)
    (*I)->Destroy(C);
  
  C.Deallocate(ParamInfo);    
  Decl::Destroy(C);
}

void BlockDecl::setParams(ASTContext& C, ParmVarDecl **NewParamInfo,
                          unsigned NParms) {
  assert(ParamInfo == 0 && "Already has param info!");
  
  // Zero params -> null pointer.
  if (NParms) {
    NumParams = NParms;
    void *Mem = C.Allocate(sizeof(ParmVarDecl*)*NumParams);
    ParamInfo = new (Mem) ParmVarDecl*[NumParams];
    memcpy(ParamInfo, NewParamInfo, sizeof(ParmVarDecl*)*NumParams);
  }
}

unsigned BlockDecl::getNumParams() const {
  return NumParams;
}
