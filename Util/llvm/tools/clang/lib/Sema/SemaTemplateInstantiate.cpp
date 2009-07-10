//===------- SemaTemplateInstantiate.cpp - C++ Template Instantiation ------===/
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//===----------------------------------------------------------------------===/
//
//  This file implements C++ template instantiation.
//
//===----------------------------------------------------------------------===/

#include "Sema.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/Parse/DeclSpec.h"
#include "clang/Basic/LangOptions.h"
#include "llvm/Support/Compiler.h"

using namespace clang;

//===----------------------------------------------------------------------===/
// Template Instantiation Support
//===----------------------------------------------------------------------===/

/// \brief Retrieve the template argument list that should be used to
/// instantiate the given declaration.
const TemplateArgumentList &
Sema::getTemplateInstantiationArgs(NamedDecl *D) {
  // Template arguments for a class template specialization.
  if (ClassTemplateSpecializationDecl *Spec 
        = dyn_cast<ClassTemplateSpecializationDecl>(D))
    return Spec->getTemplateArgs();

  // Template arguments for a function template specialization.
  if (FunctionDecl *Function = dyn_cast<FunctionDecl>(D))
    if (const TemplateArgumentList *TemplateArgs
          = Function->getTemplateSpecializationArgs())
      return *TemplateArgs;
      
  // Template arguments for a member of a class template specialization.
  DeclContext *EnclosingTemplateCtx = D->getDeclContext();
  while (!isa<ClassTemplateSpecializationDecl>(EnclosingTemplateCtx)) {
    assert(!EnclosingTemplateCtx->isFileContext() &&
           "Tried to get the instantiation arguments of a non-template");
    EnclosingTemplateCtx = EnclosingTemplateCtx->getParent();
  }

  ClassTemplateSpecializationDecl *EnclosingTemplate 
    = cast<ClassTemplateSpecializationDecl>(EnclosingTemplateCtx);
  return EnclosingTemplate->getTemplateArgs();
}

Sema::InstantiatingTemplate::
InstantiatingTemplate(Sema &SemaRef, SourceLocation PointOfInstantiation,
                      Decl *Entity,
                      SourceRange InstantiationRange)
  :  SemaRef(SemaRef) {

  Invalid = CheckInstantiationDepth(PointOfInstantiation,
                                    InstantiationRange);
  if (!Invalid) {
    ActiveTemplateInstantiation Inst;
    Inst.Kind = ActiveTemplateInstantiation::TemplateInstantiation;
    Inst.PointOfInstantiation = PointOfInstantiation;
    Inst.Entity = reinterpret_cast<uintptr_t>(Entity);
    Inst.TemplateArgs = 0;
    Inst.NumTemplateArgs = 0;
    Inst.InstantiationRange = InstantiationRange;
    SemaRef.ActiveTemplateInstantiations.push_back(Inst);
    Invalid = false;
  }
}

Sema::InstantiatingTemplate::InstantiatingTemplate(Sema &SemaRef, 
                                         SourceLocation PointOfInstantiation,
                                         TemplateDecl *Template,
                                         const TemplateArgument *TemplateArgs,
                                         unsigned NumTemplateArgs,
                                         SourceRange InstantiationRange)
  : SemaRef(SemaRef) {

  Invalid = CheckInstantiationDepth(PointOfInstantiation,
                                    InstantiationRange);
  if (!Invalid) {
    ActiveTemplateInstantiation Inst;
    Inst.Kind 
      = ActiveTemplateInstantiation::DefaultTemplateArgumentInstantiation;
    Inst.PointOfInstantiation = PointOfInstantiation;
    Inst.Entity = reinterpret_cast<uintptr_t>(Template);
    Inst.TemplateArgs = TemplateArgs;
    Inst.NumTemplateArgs = NumTemplateArgs;
    Inst.InstantiationRange = InstantiationRange;
    SemaRef.ActiveTemplateInstantiations.push_back(Inst);
    Invalid = false;
  }
}

Sema::InstantiatingTemplate::InstantiatingTemplate(Sema &SemaRef, 
                                         SourceLocation PointOfInstantiation,
                                      FunctionTemplateDecl *FunctionTemplate,
                                        const TemplateArgument *TemplateArgs,
                                                   unsigned NumTemplateArgs,
                         ActiveTemplateInstantiation::InstantiationKind Kind,
                                              SourceRange InstantiationRange)
: SemaRef(SemaRef) {
  
  Invalid = CheckInstantiationDepth(PointOfInstantiation,
                                    InstantiationRange);
  if (!Invalid) {
    ActiveTemplateInstantiation Inst;
    Inst.Kind = Kind;
    Inst.PointOfInstantiation = PointOfInstantiation;
    Inst.Entity = reinterpret_cast<uintptr_t>(FunctionTemplate);
    Inst.TemplateArgs = TemplateArgs;
    Inst.NumTemplateArgs = NumTemplateArgs;
    Inst.InstantiationRange = InstantiationRange;
    SemaRef.ActiveTemplateInstantiations.push_back(Inst);
    Invalid = false;
  }
}

Sema::InstantiatingTemplate::InstantiatingTemplate(Sema &SemaRef, 
                                         SourceLocation PointOfInstantiation,
                          ClassTemplatePartialSpecializationDecl *PartialSpec,
                                         const TemplateArgument *TemplateArgs,
                                         unsigned NumTemplateArgs,
                                         SourceRange InstantiationRange)
  : SemaRef(SemaRef) {

  Invalid = CheckInstantiationDepth(PointOfInstantiation,
                                    InstantiationRange);
  if (!Invalid) {
    ActiveTemplateInstantiation Inst;
    Inst.Kind 
      = ActiveTemplateInstantiation::DeducedTemplateArgumentSubstitution;
    Inst.PointOfInstantiation = PointOfInstantiation;
    Inst.Entity = reinterpret_cast<uintptr_t>(PartialSpec);
    Inst.TemplateArgs = TemplateArgs;
    Inst.NumTemplateArgs = NumTemplateArgs;
    Inst.InstantiationRange = InstantiationRange;
    SemaRef.ActiveTemplateInstantiations.push_back(Inst);
    Invalid = false;
  }
}

void Sema::InstantiatingTemplate::Clear() {
  if (!Invalid) {
    SemaRef.ActiveTemplateInstantiations.pop_back();
    Invalid = true;
  }
}

bool Sema::InstantiatingTemplate::CheckInstantiationDepth(
                                        SourceLocation PointOfInstantiation,
                                           SourceRange InstantiationRange) {
  if (SemaRef.ActiveTemplateInstantiations.size() 
       <= SemaRef.getLangOptions().InstantiationDepth)
    return false;

  SemaRef.Diag(PointOfInstantiation, 
               diag::err_template_recursion_depth_exceeded)
    << SemaRef.getLangOptions().InstantiationDepth
    << InstantiationRange;
  SemaRef.Diag(PointOfInstantiation, diag::note_template_recursion_depth)
    << SemaRef.getLangOptions().InstantiationDepth;
  return true;
}

/// \brief Prints the current instantiation stack through a series of
/// notes.
void Sema::PrintInstantiationStack() {
  // FIXME: In all of these cases, we need to show the template arguments
  for (llvm::SmallVector<ActiveTemplateInstantiation, 16>::reverse_iterator
         Active = ActiveTemplateInstantiations.rbegin(),
         ActiveEnd = ActiveTemplateInstantiations.rend();
       Active != ActiveEnd;
       ++Active) {
    switch (Active->Kind) {
    case ActiveTemplateInstantiation::TemplateInstantiation: {
      Decl *D = reinterpret_cast<Decl *>(Active->Entity);
      if (CXXRecordDecl *Record = dyn_cast<CXXRecordDecl>(D)) {
        unsigned DiagID = diag::note_template_member_class_here;
        if (isa<ClassTemplateSpecializationDecl>(Record))
          DiagID = diag::note_template_class_instantiation_here;
        Diags.Report(FullSourceLoc(Active->PointOfInstantiation, SourceMgr), 
                     DiagID)
          << Context.getTypeDeclType(Record)
          << Active->InstantiationRange;
      } else {
        FunctionDecl *Function = cast<FunctionDecl>(D);
        unsigned DiagID;
        if (Function->getPrimaryTemplate())
          DiagID = diag::note_function_template_spec_here;
        else
          DiagID = diag::note_template_member_function_here;
        Diags.Report(FullSourceLoc(Active->PointOfInstantiation, SourceMgr), 
                     DiagID)
          << Function
          << Active->InstantiationRange;
      }
      break;
    }

    case ActiveTemplateInstantiation::DefaultTemplateArgumentInstantiation: {
      TemplateDecl *Template = cast<TemplateDecl>((Decl *)Active->Entity);
      std::string TemplateArgsStr
        = TemplateSpecializationType::PrintTemplateArgumentList(
                                                         Active->TemplateArgs, 
                                                      Active->NumTemplateArgs,
                                                      Context.PrintingPolicy);
      Diags.Report(FullSourceLoc(Active->PointOfInstantiation, SourceMgr),
                   diag::note_default_arg_instantiation_here)
        << (Template->getNameAsString() + TemplateArgsStr)
        << Active->InstantiationRange;
      break;
    }

    case ActiveTemplateInstantiation::ExplicitTemplateArgumentSubstitution: {
      FunctionTemplateDecl *FnTmpl 
        = cast<FunctionTemplateDecl>((Decl *)Active->Entity);
      Diags.Report(FullSourceLoc(Active->PointOfInstantiation, SourceMgr),
                   diag::note_explicit_template_arg_substitution_here)
        << FnTmpl << Active->InstantiationRange;
      break;
    }
        
    case ActiveTemplateInstantiation::DeducedTemplateArgumentSubstitution:
      if (ClassTemplatePartialSpecializationDecl *PartialSpec
            = dyn_cast<ClassTemplatePartialSpecializationDecl>(
                                                    (Decl *)Active->Entity)) {
        Diags.Report(FullSourceLoc(Active->PointOfInstantiation, SourceMgr),
                     diag::note_partial_spec_deduct_instantiation_here)
          << Context.getTypeDeclType(PartialSpec)
          << Active->InstantiationRange;
      } else {
        FunctionTemplateDecl *FnTmpl
          = cast<FunctionTemplateDecl>((Decl *)Active->Entity);
        Diags.Report(FullSourceLoc(Active->PointOfInstantiation, SourceMgr),
                     diag::note_function_template_deduction_instantiation_here)
          << FnTmpl << Active->InstantiationRange;
      }
      break;

    }
  }
}

bool Sema::isSFINAEContext() const {
  using llvm::SmallVector;
  for (SmallVector<ActiveTemplateInstantiation, 16>::const_reverse_iterator
         Active = ActiveTemplateInstantiations.rbegin(),
         ActiveEnd = ActiveTemplateInstantiations.rend();
       Active != ActiveEnd;
       ++Active) {

    switch(Active->Kind) {
    case ActiveTemplateInstantiation::TemplateInstantiation:
      // This is a template instantiation, so there is no SFINAE.
      return false;
        
    case ActiveTemplateInstantiation::DefaultTemplateArgumentInstantiation:
      // A default template argument instantiation may or may not be a
      // SFINAE context; look further up the stack.
      break;
        
    case ActiveTemplateInstantiation::ExplicitTemplateArgumentSubstitution:
    case ActiveTemplateInstantiation::DeducedTemplateArgumentSubstitution:
      // We're either substitution explicitly-specified template arguments
      // or deduced template arguments, so SFINAE applies.
      return true;
    }
  }

  return false;
}

//===----------------------------------------------------------------------===/
// Template Instantiation for Types
//===----------------------------------------------------------------------===/
namespace {
  class VISIBILITY_HIDDEN TemplateTypeInstantiator {
    Sema &SemaRef;
    const TemplateArgumentList &TemplateArgs;
    SourceLocation Loc;
    DeclarationName Entity;

  public:
    TemplateTypeInstantiator(Sema &SemaRef, 
                             const TemplateArgumentList &TemplateArgs,
                             SourceLocation Loc,
                             DeclarationName Entity) 
      : SemaRef(SemaRef), TemplateArgs(TemplateArgs), 
        Loc(Loc), Entity(Entity) { }

    QualType operator()(QualType T) const { return Instantiate(T); }
    
    QualType Instantiate(QualType T) const;

    // Declare instantiate functions for each type.
#define TYPE(Class, Base)                                       \
    QualType Instantiate##Class##Type(const Class##Type *T) const;
#define ABSTRACT_TYPE(Class, Base)
#include "clang/AST/TypeNodes.def"
  };
}

QualType 
TemplateTypeInstantiator::InstantiateExtQualType(const ExtQualType *T) const {
  // FIXME: Implement this
  assert(false && "Cannot instantiate ExtQualType yet");
  return QualType();
}

QualType 
TemplateTypeInstantiator::InstantiateBuiltinType(const BuiltinType *T) const {
  assert(false && "Builtin types are not dependent and cannot be instantiated");
  return QualType(T, 0);
}

QualType 
TemplateTypeInstantiator::
InstantiateFixedWidthIntType(const FixedWidthIntType *T) const {
  // FIXME: Implement this
  assert(false && "Cannot instantiate FixedWidthIntType yet");
  return QualType();
}

QualType 
TemplateTypeInstantiator::InstantiateComplexType(const ComplexType *T) const {
  // FIXME: Implement this
  assert(false && "Cannot instantiate ComplexType yet");
  return QualType();
}

QualType 
TemplateTypeInstantiator::InstantiatePointerType(const PointerType *T) const {
  QualType PointeeType = Instantiate(T->getPointeeType());
  if (PointeeType.isNull())
    return QualType();

  return SemaRef.BuildPointerType(PointeeType, 0, Loc, Entity);
}

QualType 
TemplateTypeInstantiator::InstantiateBlockPointerType(
                                            const BlockPointerType *T) const {
  QualType PointeeType = Instantiate(T->getPointeeType());
  if (PointeeType.isNull())
    return QualType();
  
  return SemaRef.BuildBlockPointerType(PointeeType, 0, Loc, Entity);
}

QualType
TemplateTypeInstantiator::InstantiateLValueReferenceType(
                                        const LValueReferenceType *T) const {
  QualType ReferentType = Instantiate(T->getPointeeType());
  if (ReferentType.isNull())
    return QualType();

  return SemaRef.BuildReferenceType(ReferentType, true, 0, Loc, Entity);
}

QualType
TemplateTypeInstantiator::InstantiateRValueReferenceType(
                                        const RValueReferenceType *T) const {
  QualType ReferentType = Instantiate(T->getPointeeType());
  if (ReferentType.isNull())
    return QualType();

  return SemaRef.BuildReferenceType(ReferentType, false, 0, Loc, Entity);
}

QualType 
TemplateTypeInstantiator::
InstantiateMemberPointerType(const MemberPointerType *T) const {
  QualType PointeeType = Instantiate(T->getPointeeType());
  if (PointeeType.isNull())
    return QualType();

  QualType ClassType = Instantiate(QualType(T->getClass(), 0));
  if (ClassType.isNull())
    return QualType();

  return SemaRef.BuildMemberPointerType(PointeeType, ClassType, 0, Loc, 
                                        Entity);
}

QualType 
TemplateTypeInstantiator::
InstantiateConstantArrayType(const ConstantArrayType *T) const {
  QualType ElementType = Instantiate(T->getElementType());
  if (ElementType.isNull())
    return ElementType;
  
  // Build a temporary integer literal to specify the size for
  // BuildArrayType. Since we have already checked the size as part of
  // creating the dependent array type in the first place, we know
  // there aren't any errors. However, we do need to determine what
  // C++ type to give the size expression.
  llvm::APInt Size = T->getSize();
  QualType Types[] = { 
    SemaRef.Context.UnsignedCharTy, SemaRef.Context.UnsignedShortTy, 
    SemaRef.Context.UnsignedIntTy, SemaRef.Context.UnsignedLongTy, 
    SemaRef.Context.UnsignedLongLongTy, SemaRef.Context.UnsignedInt128Ty 
  };
  const unsigned NumTypes = sizeof(Types) / sizeof(QualType);
  QualType SizeType;
  for (unsigned I = 0; I != NumTypes; ++I)
    if (Size.getBitWidth() == SemaRef.Context.getIntWidth(Types[I])) {
      SizeType = Types[I];
      break;
    }

  if (SizeType.isNull())
    SizeType = SemaRef.Context.getFixedWidthIntType(Size.getBitWidth(), false);

  IntegerLiteral ArraySize(Size, SizeType, Loc);
  return SemaRef.BuildArrayType(ElementType, T->getSizeModifier(), 
                                &ArraySize, T->getIndexTypeQualifier(), 
                                SourceRange(), // FIXME: provide proper range?
                                Entity);
}

QualType
TemplateTypeInstantiator::InstantiateConstantArrayWithExprType
(const ConstantArrayWithExprType *T) const {
  return InstantiateConstantArrayType(T);
}

QualType
TemplateTypeInstantiator::InstantiateConstantArrayWithoutExprType
(const ConstantArrayWithoutExprType *T) const {
  return InstantiateConstantArrayType(T);
}

QualType 
TemplateTypeInstantiator::
InstantiateIncompleteArrayType(const IncompleteArrayType *T) const {
  QualType ElementType = Instantiate(T->getElementType());
  if (ElementType.isNull())
    return ElementType;
  
  return SemaRef.BuildArrayType(ElementType, T->getSizeModifier(), 
                                0, T->getIndexTypeQualifier(),
                                SourceRange(), // FIXME: provide proper range?
                                Entity);
}

QualType
TemplateTypeInstantiator::
InstantiateVariableArrayType(const VariableArrayType *T) const {
  // FIXME: Implement this
  assert(false && "Cannot instantiate VariableArrayType yet");
  return QualType();
}

QualType 
TemplateTypeInstantiator::
InstantiateDependentSizedArrayType(const DependentSizedArrayType *T) const {
  Expr *ArraySize = T->getSizeExpr();
  assert(ArraySize->isValueDependent() && 
         "dependent sized array types must have value dependent size expr");
  
  // Instantiate the element type if needed
  QualType ElementType = T->getElementType();
  if (ElementType->isDependentType()) {
    ElementType = Instantiate(ElementType);
    if (ElementType.isNull())
      return QualType();
  }
  
  // Instantiate the size expression
  EnterExpressionEvaluationContext Unevaluated(SemaRef, Action::Unevaluated);
  Sema::OwningExprResult InstantiatedArraySize = 
    SemaRef.InstantiateExpr(ArraySize, TemplateArgs);
  if (InstantiatedArraySize.isInvalid())
    return QualType();
  
  return SemaRef.BuildArrayType(ElementType, T->getSizeModifier(),
                                InstantiatedArraySize.takeAs<Expr>(),
                                T->getIndexTypeQualifier(),
                                SourceRange(), // FIXME: provide proper range?
                                Entity);
}

QualType 
TemplateTypeInstantiator::
InstantiateDependentSizedExtVectorType(
                                const DependentSizedExtVectorType *T) const {

  // Instantiate the element type if needed.
  QualType ElementType = T->getElementType();
  if (ElementType->isDependentType()) {
    ElementType = Instantiate(ElementType);
    if (ElementType.isNull())
      return QualType();
  }

  // The expression in a dependent-sized extended vector type is not
  // potentially evaluated.
  EnterExpressionEvaluationContext Unevaluated(SemaRef, Action::Unevaluated);

  // Instantiate the size expression.
  const Expr *SizeExpr = T->getSizeExpr();
  Sema::OwningExprResult InstantiatedArraySize = 
    SemaRef.InstantiateExpr(const_cast<Expr *>(SizeExpr), TemplateArgs);
  if (InstantiatedArraySize.isInvalid())
    return QualType();
  
  return SemaRef.BuildExtVectorType(ElementType,
                                    SemaRef.Owned(
                                      InstantiatedArraySize.takeAs<Expr>()),
                                    T->getAttributeLoc());
}

QualType 
TemplateTypeInstantiator::InstantiateVectorType(const VectorType *T) const {
  // FIXME: Implement this
  assert(false && "Cannot instantiate VectorType yet");
  return QualType();
}

QualType 
TemplateTypeInstantiator::InstantiateExtVectorType(
                                              const ExtVectorType *T) const {
  // FIXME: Implement this
  assert(false && "Cannot instantiate ExtVectorType yet");
  return QualType();
}

QualType 
TemplateTypeInstantiator::
InstantiateFunctionProtoType(const FunctionProtoType *T) const {
  QualType ResultType = Instantiate(T->getResultType());
  if (ResultType.isNull())
    return ResultType;

  llvm::SmallVector<QualType, 4> ParamTypes;
  for (FunctionProtoType::arg_type_iterator Param = T->arg_type_begin(),
                                         ParamEnd = T->arg_type_end(); 
       Param != ParamEnd; ++Param) {
    QualType P = Instantiate(*Param);
    if (P.isNull())
      return P;

    ParamTypes.push_back(P);
  }

  return SemaRef.BuildFunctionType(ResultType, ParamTypes.data(), 
                                   ParamTypes.size(),
                                   T->isVariadic(), T->getTypeQuals(),
                                   Loc, Entity);
}

QualType 
TemplateTypeInstantiator::
InstantiateFunctionNoProtoType(const FunctionNoProtoType *T) const {
  assert(false && "Functions without prototypes cannot be dependent.");
  return QualType();
}

QualType 
TemplateTypeInstantiator::InstantiateTypedefType(const TypedefType *T) const {
  TypedefDecl *Typedef 
    = cast_or_null<TypedefDecl>(
                           SemaRef.InstantiateCurrentDeclRef(T->getDecl()));
  if (!Typedef)
    return QualType();
  
  return SemaRef.Context.getTypeDeclType(Typedef);
}

QualType 
TemplateTypeInstantiator::InstantiateTypeOfExprType(
                                              const TypeOfExprType *T) const {
  // The expression in a typeof is not potentially evaluated.
  EnterExpressionEvaluationContext Unevaluated(SemaRef, Action::Unevaluated);
  
  Sema::OwningExprResult E 
    = SemaRef.InstantiateExpr(T->getUnderlyingExpr(), TemplateArgs);
  if (E.isInvalid())
    return QualType();

  return SemaRef.BuildTypeofExprType(E.takeAs<Expr>());
}

QualType 
TemplateTypeInstantiator::InstantiateTypeOfType(const TypeOfType *T) const {
  QualType Underlying = Instantiate(T->getUnderlyingType());
  if (Underlying.isNull())
    return QualType();

  return SemaRef.Context.getTypeOfType(Underlying);
}

QualType
TemplateTypeInstantiator::InstantiateDecltypeType(const DecltypeType *T) const {
  // C++0x [dcl.type.simple]p4:
  //   The operand of the decltype specifier is an unevaluated operand.
  EnterExpressionEvaluationContext Unevaluated(SemaRef,
                                               Action::Unevaluated);
  
  Sema::OwningExprResult E 
    = SemaRef.InstantiateExpr(T->getUnderlyingExpr(), TemplateArgs);

  if (E.isInvalid())
    return QualType();
  
  return SemaRef.BuildDecltypeType(E.takeAs<Expr>());
}

QualType 
TemplateTypeInstantiator::InstantiateRecordType(const RecordType *T) const {
  RecordDecl *Record 
    = cast_or_null<RecordDecl>(SemaRef.InstantiateCurrentDeclRef(T->getDecl()));
  if (!Record)
    return QualType();
  
  return SemaRef.Context.getTypeDeclType(Record);
}

QualType 
TemplateTypeInstantiator::InstantiateEnumType(const EnumType *T) const {
  EnumDecl *Enum 
    = cast_or_null<EnumDecl>(SemaRef.InstantiateCurrentDeclRef(T->getDecl()));
  if (!Enum)
    return QualType();
  
  return SemaRef.Context.getTypeDeclType(Enum);
}

QualType 
TemplateTypeInstantiator::
InstantiateTemplateTypeParmType(const TemplateTypeParmType *T) const {
  if (T->getDepth() == 0) {
    // Replace the template type parameter with its corresponding
    // template argument.
    
    // If the corresponding template argument is NULL or doesn't exist, it's 
    // because we are performing instantiation from explicitly-specified 
    // template arguments in a function template class, but there were some 
    // arguments left unspecified.
    if (T->getIndex() >= TemplateArgs.size() ||
        TemplateArgs[T->getIndex()].isNull())
      return QualType(T, 0); // Would be nice to keep the original type here
        
    assert(TemplateArgs[T->getIndex()].getKind() == TemplateArgument::Type &&
           "Template argument kind mismatch");
    return TemplateArgs[T->getIndex()].getAsType();
  } 

  // The template type parameter comes from an inner template (e.g.,
  // the template parameter list of a member template inside the
  // template we are instantiating). Create a new template type
  // parameter with the template "level" reduced by one.
  return SemaRef.Context.getTemplateTypeParmType(T->getDepth() - 1,
                                                 T->getIndex(),
                                                 T->isParameterPack(),
                                                 T->getName());
}

QualType 
TemplateTypeInstantiator::
InstantiateTemplateSpecializationType(
                                  const TemplateSpecializationType *T) const {
  llvm::SmallVector<TemplateArgument, 4> InstantiatedTemplateArgs;
  InstantiatedTemplateArgs.reserve(T->getNumArgs());
  for (TemplateSpecializationType::iterator Arg = T->begin(), ArgEnd = T->end();
       Arg != ArgEnd; ++Arg) {
    TemplateArgument InstArg = SemaRef.Instantiate(*Arg, TemplateArgs);
    if (InstArg.isNull())
      return QualType();

    InstantiatedTemplateArgs.push_back(InstArg);
  }

  // FIXME: We're missing the locations of the template name, '<', and '>'.

  TemplateName Name = SemaRef.InstantiateTemplateName(T->getTemplateName(),
                                                      Loc, 
                                                      TemplateArgs);

  return SemaRef.CheckTemplateIdType(Name, Loc, SourceLocation(),
                                     InstantiatedTemplateArgs.data(),
                                     InstantiatedTemplateArgs.size(),
                                     SourceLocation());
}

QualType 
TemplateTypeInstantiator::
InstantiateQualifiedNameType(const QualifiedNameType *T) const {
  // When we instantiated a qualified name type, there's no point in
  // keeping the qualification around in the instantiated result. So,
  // just instantiate the named type.
  return (*this)(T->getNamedType());
}

QualType 
TemplateTypeInstantiator::
InstantiateTypenameType(const TypenameType *T) const {
  if (const TemplateSpecializationType *TemplateId = T->getTemplateId()) {
    // When the typename type refers to a template-id, the template-id
    // is dependent and has enough information to instantiate the
    // result of the typename type. Since we don't care about keeping
    // the spelling of the typename type in template instantiations,
    // we just instantiate the template-id.
    return InstantiateTemplateSpecializationType(TemplateId);
  }

  NestedNameSpecifier *NNS 
    = SemaRef.InstantiateNestedNameSpecifier(T->getQualifier(), 
                                             SourceRange(Loc),
                                             TemplateArgs);
  if (!NNS)
    return QualType();

  return SemaRef.CheckTypenameType(NNS, *T->getIdentifier(), SourceRange(Loc));
}

QualType 
TemplateTypeInstantiator::
InstantiateObjCObjectPointerType(const ObjCObjectPointerType *T) const {
  assert(false && "Objective-C types cannot be dependent");
  return QualType();
}

QualType
TemplateTypeInstantiator::
InstantiateObjCInterfaceType(const ObjCInterfaceType *T) const {
  assert(false && "Objective-C types cannot be dependent");
  return QualType();
}

QualType 
TemplateTypeInstantiator::
InstantiateObjCQualifiedInterfaceType(
                                 const ObjCQualifiedInterfaceType *T) const {
  assert(false && "Objective-C types cannot be dependent");
  return QualType();
}

/// \brief The actual implementation of Sema::InstantiateType().
QualType TemplateTypeInstantiator::Instantiate(QualType T) const {
  // If T is not a dependent type, there is nothing to do.
  if (!T->isDependentType())
    return T;

  QualType Result;
  switch (T->getTypeClass()) {
#define TYPE(Class, Base)                                               \
  case Type::Class:                                                     \
    Result = Instantiate##Class##Type(cast<Class##Type>(T.getTypePtr()));  \
    break;
#define ABSTRACT_TYPE(Class, Base)
#include "clang/AST/TypeNodes.def"
  }

  // C++ [dcl.ref]p1:
  //   [...] Cv-qualified references are ill-formed except when
  //   the cv-qualifiers are introduced through the use of a
  //   typedef (7.1.3) or of a template type argument (14.3), in
  //   which case the cv-qualifiers are ignored.
  // 
  // The same rule applies to function types.
  if (!Result.isNull() && T.getCVRQualifiers() &&
      !Result->isFunctionType() && !Result->isReferenceType())
    Result = Result.getWithAdditionalQualifiers(T.getCVRQualifiers());
  return Result;
}

/// \brief Instantiate the type T with a given set of template arguments.
///
/// This routine substitutes the given template arguments into the
/// type T and produces the instantiated type.
///
/// \param T the type into which the template arguments will be
/// substituted. If this type is not dependent, it will be returned
/// immediately.
///
/// \param TemplateArgs the template arguments that will be
/// substituted for the top-level template parameters within T.
///
/// \param Loc the location in the source code where this substitution
/// is being performed. It will typically be the location of the
/// declarator (if we're instantiating the type of some declaration)
/// or the location of the type in the source code (if, e.g., we're
/// instantiating the type of a cast expression).
///
/// \param Entity the name of the entity associated with a declaration
/// being instantiated (if any). May be empty to indicate that there
/// is no such entity (if, e.g., this is a type that occurs as part of
/// a cast expression) or that the entity has no name (e.g., an
/// unnamed function parameter).
///
/// \returns If the instantiation succeeds, the instantiated
/// type. Otherwise, produces diagnostics and returns a NULL type.
QualType Sema::InstantiateType(QualType T, 
                               const TemplateArgumentList &TemplateArgs,
                               SourceLocation Loc, DeclarationName Entity) {
  assert(!ActiveTemplateInstantiations.empty() &&
         "Cannot perform an instantiation without some context on the "
         "instantiation stack");

  // If T is not a dependent type, there is nothing to do.
  if (!T->isDependentType())
    return T;

  TemplateTypeInstantiator Instantiator(*this, TemplateArgs, Loc, Entity);
  return Instantiator(T);
}

/// \brief Instantiate the base class specifiers of the given class
/// template specialization.
///
/// Produces a diagnostic and returns true on error, returns false and
/// attaches the instantiated base classes to the class template
/// specialization if successful.
bool 
Sema::InstantiateBaseSpecifiers(CXXRecordDecl *Instantiation,
                                CXXRecordDecl *Pattern,
                                const TemplateArgumentList &TemplateArgs) {
  bool Invalid = false;
  llvm::SmallVector<CXXBaseSpecifier*, 4> InstantiatedBases;
  for (ClassTemplateSpecializationDecl::base_class_iterator 
         Base = Pattern->bases_begin(), BaseEnd = Pattern->bases_end();
       Base != BaseEnd; ++Base) {
    if (!Base->getType()->isDependentType()) {
      // FIXME: Allocate via ASTContext
      InstantiatedBases.push_back(new CXXBaseSpecifier(*Base));
      continue;
    }

    QualType BaseType = InstantiateType(Base->getType(), 
                                        TemplateArgs, 
                                        Base->getSourceRange().getBegin(),
                                        DeclarationName());
    if (BaseType.isNull()) {
      Invalid = true;
      continue;
    }

    if (CXXBaseSpecifier *InstantiatedBase
          = CheckBaseSpecifier(Instantiation,
                               Base->getSourceRange(),
                               Base->isVirtual(),
                               Base->getAccessSpecifierAsWritten(),
                               BaseType,
                               /*FIXME: Not totally accurate */
                               Base->getSourceRange().getBegin()))
      InstantiatedBases.push_back(InstantiatedBase);
    else
      Invalid = true;
  }

  if (!Invalid &&
      AttachBaseSpecifiers(Instantiation, InstantiatedBases.data(),
                           InstantiatedBases.size()))
    Invalid = true;

  return Invalid;
}

/// \brief Instantiate the definition of a class from a given pattern.
///
/// \param PointOfInstantiation The point of instantiation within the
/// source code.
///
/// \param Instantiation is the declaration whose definition is being
/// instantiated. This will be either a class template specialization
/// or a member class of a class template specialization.
///
/// \param Pattern is the pattern from which the instantiation
/// occurs. This will be either the declaration of a class template or
/// the declaration of a member class of a class template.
///
/// \param TemplateArgs The template arguments to be substituted into
/// the pattern.
///
/// \returns true if an error occurred, false otherwise.
bool
Sema::InstantiateClass(SourceLocation PointOfInstantiation,
                       CXXRecordDecl *Instantiation, CXXRecordDecl *Pattern,
                       const TemplateArgumentList &TemplateArgs,
                       bool ExplicitInstantiation) {
  bool Invalid = false;
  
  CXXRecordDecl *PatternDef 
    = cast_or_null<CXXRecordDecl>(Pattern->getDefinition(Context));
  if (!PatternDef) {
    if (Pattern == Instantiation->getInstantiatedFromMemberClass()) {
      Diag(PointOfInstantiation,
           diag::err_implicit_instantiate_member_undefined)
        << Context.getTypeDeclType(Instantiation);
      Diag(Pattern->getLocation(), diag::note_member_of_template_here);
    } else {
      Diag(PointOfInstantiation, diag::err_template_instantiate_undefined)
        << ExplicitInstantiation
        << Context.getTypeDeclType(Instantiation);
      Diag(Pattern->getLocation(), diag::note_template_decl_here);
    }
    return true;
  }
  Pattern = PatternDef;

  InstantiatingTemplate Inst(*this, PointOfInstantiation, Instantiation);
  if (Inst)
    return true;

  // Enter the scope of this instantiation. We don't use
  // PushDeclContext because we don't have a scope.
  DeclContext *PreviousContext = CurContext;
  CurContext = Instantiation;

  // Start the definition of this instantiation.
  Instantiation->startDefinition();

  // Instantiate the base class specifiers.
  if (InstantiateBaseSpecifiers(Instantiation, Pattern, TemplateArgs))
    Invalid = true;

  llvm::SmallVector<DeclPtrTy, 4> Fields;
  for (RecordDecl::decl_iterator Member = Pattern->decls_begin(),
         MemberEnd = Pattern->decls_end(); 
       Member != MemberEnd; ++Member) {
    Decl *NewMember = InstantiateDecl(*Member, Instantiation, TemplateArgs);
    if (NewMember) {
      if (NewMember->isInvalidDecl())
        Invalid = true;
      else if (FieldDecl *Field = dyn_cast<FieldDecl>(NewMember))
        Fields.push_back(DeclPtrTy::make(Field));
    } else {
      // FIXME: Eventually, a NULL return will mean that one of the
      // instantiations was a semantic disaster, and we'll want to set Invalid =
      // true. For now, we expect to skip some members that we can't yet handle.
    }
  }

  // Finish checking fields.
  ActOnFields(0, Instantiation->getLocation(), DeclPtrTy::make(Instantiation),
              Fields.data(), Fields.size(), SourceLocation(), SourceLocation(),
              0);

  // Add any implicitly-declared members that we might need.
  AddImplicitlyDeclaredMembersToClass(Instantiation);

  // Exit the scope of this instantiation.
  CurContext = PreviousContext;

  if (!Invalid)
    Consumer.HandleTagDeclDefinition(Instantiation);

  // If this is an explicit instantiation, instantiate our members, too.
  if (!Invalid && ExplicitInstantiation) {
    Inst.Clear();
    InstantiateClassMembers(PointOfInstantiation, Instantiation, TemplateArgs);
  }

  return Invalid;
}

bool 
Sema::InstantiateClassTemplateSpecialization(
                           ClassTemplateSpecializationDecl *ClassTemplateSpec,
                           bool ExplicitInstantiation) {
  // Perform the actual instantiation on the canonical declaration.
  ClassTemplateSpec = cast<ClassTemplateSpecializationDecl>(
                               Context.getCanonicalDecl(ClassTemplateSpec));

  // We can only instantiate something that hasn't already been
  // instantiated or specialized. Fail without any diagnostics: our
  // caller will provide an error message.
  if (ClassTemplateSpec->getSpecializationKind() != TSK_Undeclared)
    return true;

  ClassTemplateDecl *Template = ClassTemplateSpec->getSpecializedTemplate();
  CXXRecordDecl *Pattern = Template->getTemplatedDecl();
  const TemplateArgumentList *TemplateArgs 
    = &ClassTemplateSpec->getTemplateArgs();

  // C++ [temp.class.spec.match]p1:
  //   When a class template is used in a context that requires an
  //   instantiation of the class, it is necessary to determine
  //   whether the instantiation is to be generated using the primary
  //   template or one of the partial specializations. This is done by
  //   matching the template arguments of the class template
  //   specialization with the template argument lists of the partial
  //   specializations.
  typedef std::pair<ClassTemplatePartialSpecializationDecl *,
                    TemplateArgumentList *> MatchResult;
  llvm::SmallVector<MatchResult, 4> Matched;
  for (llvm::FoldingSet<ClassTemplatePartialSpecializationDecl>::iterator 
         Partial = Template->getPartialSpecializations().begin(),
         PartialEnd = Template->getPartialSpecializations().end();
       Partial != PartialEnd;
       ++Partial) {
    TemplateDeductionInfo Info(Context);
    if (TemplateDeductionResult Result
          = DeduceTemplateArguments(&*Partial, 
                                    ClassTemplateSpec->getTemplateArgs(),
                                    Info)) {
      // FIXME: Store the failed-deduction information for use in
      // diagnostics, later.
      (void)Result;
    } else {
      Matched.push_back(std::make_pair(&*Partial, Info.take()));
    }
  }

  if (Matched.size() == 1) {
    //   -- If exactly one matching specialization is found, the
    //      instantiation is generated from that specialization.
    Pattern = Matched[0].first;
    TemplateArgs = Matched[0].second;
  } else if (Matched.size() > 1) {
    //   -- If more than one matching specialization is found, the
    //      partial order rules (14.5.4.2) are used to determine
    //      whether one of the specializations is more specialized
    //      than the others. If none of the specializations is more
    //      specialized than all of the other matching
    //      specializations, then the use of the class template is
    //      ambiguous and the program is ill-formed.
    // FIXME: Implement partial ordering of class template partial
    // specializations.
    Diag(ClassTemplateSpec->getLocation(), 
         diag::unsup_template_partial_spec_ordering);
  } else {
    //   -- If no matches are found, the instantiation is generated
    //      from the primary template.

    // Since we initialized the pattern and template arguments from
    // the primary template, there is nothing more we need to do here.
  }

  // Note that this is an instantiation.  
  ClassTemplateSpec->setSpecializationKind(
                        ExplicitInstantiation? TSK_ExplicitInstantiation 
                                             : TSK_ImplicitInstantiation);

  bool Result = InstantiateClass(ClassTemplateSpec->getLocation(),
                                 ClassTemplateSpec, Pattern, *TemplateArgs,
                                 ExplicitInstantiation);
  
  for (unsigned I = 0, N = Matched.size(); I != N; ++I) {
    // FIXME: Implement TemplateArgumentList::Destroy!
    //    if (Matched[I].first != Pattern)
    //      Matched[I].second->Destroy(Context);
  }
  
  return Result;
}

/// \brief Instantiate the definitions of all of the member of the
/// given class, which is an instantiation of a class template or a
/// member class of a template.
void
Sema::InstantiateClassMembers(SourceLocation PointOfInstantiation,
                              CXXRecordDecl *Instantiation,
                              const TemplateArgumentList &TemplateArgs) {
  for (DeclContext::decl_iterator D = Instantiation->decls_begin(),
                               DEnd = Instantiation->decls_end();
       D != DEnd; ++D) {
    if (FunctionDecl *Function = dyn_cast<FunctionDecl>(*D)) {
      if (!Function->getBody())
        InstantiateFunctionDefinition(PointOfInstantiation, Function);
    } else if (VarDecl *Var = dyn_cast<VarDecl>(*D)) {
      const VarDecl *Def = 0;
      if (!Var->getDefinition(Def))
        InstantiateVariableDefinition(Var);
    } else if (CXXRecordDecl *Record = dyn_cast<CXXRecordDecl>(*D)) {
      if (!Record->isInjectedClassName() && !Record->getDefinition(Context)) {
        assert(Record->getInstantiatedFromMemberClass() && 
               "Missing instantiated-from-template information");
        InstantiateClass(PointOfInstantiation, Record,
                         Record->getInstantiatedFromMemberClass(),
                         TemplateArgs, true);
      }
    }
  }
}

/// \brief Instantiate the definitions of all of the members of the
/// given class template specialization, which was named as part of an
/// explicit instantiation.
void Sema::InstantiateClassTemplateSpecializationMembers(
                                           SourceLocation PointOfInstantiation,
                          ClassTemplateSpecializationDecl *ClassTemplateSpec) {
  // C++0x [temp.explicit]p7:
  //   An explicit instantiation that names a class template
  //   specialization is an explicit instantion of the same kind
  //   (declaration or definition) of each of its members (not
  //   including members inherited from base classes) that has not
  //   been previously explicitly specialized in the translation unit
  //   containing the explicit instantiation, except as described
  //   below.
  InstantiateClassMembers(PointOfInstantiation, ClassTemplateSpec,
                          ClassTemplateSpec->getTemplateArgs());
}

/// \brief Instantiate a nested-name-specifier.
NestedNameSpecifier *
Sema::InstantiateNestedNameSpecifier(NestedNameSpecifier *NNS,
                                     SourceRange Range,
                                     const TemplateArgumentList &TemplateArgs) {
  // Instantiate the prefix of this nested name specifier.
  NestedNameSpecifier *Prefix = NNS->getPrefix();
  if (Prefix) {
    Prefix = InstantiateNestedNameSpecifier(Prefix, Range, TemplateArgs);
    if (!Prefix)
      return 0;
  }

  switch (NNS->getKind()) {
  case NestedNameSpecifier::Identifier: {
    assert(Prefix && 
           "Can't have an identifier nested-name-specifier with no prefix");
    CXXScopeSpec SS;
    // FIXME: The source location information is all wrong.
    SS.setRange(Range);
    SS.setScopeRep(Prefix);
    return static_cast<NestedNameSpecifier *>(
                                 ActOnCXXNestedNameSpecifier(0, SS,
                                                             Range.getEnd(),
                                                             Range.getEnd(),
                                                    *NNS->getAsIdentifier()));
    break;
  }

  case NestedNameSpecifier::Namespace:
  case NestedNameSpecifier::Global:
    return NNS;
    
  case NestedNameSpecifier::TypeSpecWithTemplate:
  case NestedNameSpecifier::TypeSpec: {
    QualType T = QualType(NNS->getAsType(), 0);
    if (!T->isDependentType())
      return NNS;

    T = InstantiateType(T, TemplateArgs, Range.getBegin(), DeclarationName());
    if (T.isNull())
      return 0;

    if (T->isDependentType() || T->isRecordType() ||
        (getLangOptions().CPlusPlus0x && T->isEnumeralType())) {
      assert(T.getCVRQualifiers() == 0 && "Can't get cv-qualifiers here");
      return NestedNameSpecifier::Create(Context, Prefix, 
                 NNS->getKind() == NestedNameSpecifier::TypeSpecWithTemplate,
                                         T.getTypePtr());
    }

    Diag(Range.getBegin(), diag::err_nested_name_spec_non_tag) << T;
    return 0;
  }
  }

  // Required to silence a GCC warning
  return 0;
}

TemplateName
Sema::InstantiateTemplateName(TemplateName Name, SourceLocation Loc,
                              const TemplateArgumentList &TemplateArgs) {
  if (TemplateTemplateParmDecl *TTP 
        = dyn_cast_or_null<TemplateTemplateParmDecl>(
                                                 Name.getAsTemplateDecl())) {
    assert(TTP->getDepth() == 0 && 
           "Cannot reduce depth of a template template parameter");
    assert(TemplateArgs[TTP->getPosition()].getAsDecl() &&
           "Wrong kind of template template argument");
    ClassTemplateDecl *ClassTemplate 
      = dyn_cast<ClassTemplateDecl>(
                               TemplateArgs[TTP->getPosition()].getAsDecl());
    assert(ClassTemplate && "Expected a class template");
    if (QualifiedTemplateName *QTN = Name.getAsQualifiedTemplateName()) {
      NestedNameSpecifier *NNS 
        = InstantiateNestedNameSpecifier(QTN->getQualifier(),
                                         /*FIXME=*/SourceRange(Loc),
                                         TemplateArgs);
      if (NNS)
        return Context.getQualifiedTemplateName(NNS, 
                                                QTN->hasTemplateKeyword(),
                                                ClassTemplate);
    }

    return TemplateName(ClassTemplate);
  } else if (DependentTemplateName *DTN = Name.getAsDependentTemplateName()) {
    NestedNameSpecifier *NNS 
      = InstantiateNestedNameSpecifier(DTN->getQualifier(),
                                       /*FIXME=*/SourceRange(Loc),
                                       TemplateArgs);
    
    if (!NNS) // FIXME: Not the best recovery strategy.
      return Name;
    
    if (NNS->isDependent())
      return Context.getDependentTemplateName(NNS, DTN->getName());

    // Somewhat redundant with ActOnDependentTemplateName.
    CXXScopeSpec SS;
    SS.setRange(SourceRange(Loc));
    SS.setScopeRep(NNS);
    TemplateTy Template;
    TemplateNameKind TNK = isTemplateName(*DTN->getName(), 0, Template, &SS);
    if (TNK == TNK_Non_template) {
      Diag(Loc, diag::err_template_kw_refers_to_non_template)
        << DTN->getName();
      return Name;
    } else if (TNK == TNK_Function_template) {
      Diag(Loc, diag::err_template_kw_refers_to_non_template)
        << DTN->getName();
      return Name;
    }
    
    return Template.getAsVal<TemplateName>();
  }

  

  // FIXME: Even if we're referring to a Decl that isn't a template template
  // parameter, we may need to instantiate the outer contexts of that
  // Decl. However, this won't be needed until we implement member templates.
  return Name;
}

TemplateArgument Sema::Instantiate(TemplateArgument Arg, 
                                   const TemplateArgumentList &TemplateArgs) {
  switch (Arg.getKind()) {
  case TemplateArgument::Null:
    assert(false && "Should never have a NULL template argument");
    break;
    
  case TemplateArgument::Type: {
    QualType T = InstantiateType(Arg.getAsType(), TemplateArgs, 
                                 Arg.getLocation(), DeclarationName());
    if (T.isNull())
      return TemplateArgument();
    
    return TemplateArgument(Arg.getLocation(), T);
  }

  case TemplateArgument::Declaration:
    // FIXME: Template instantiation for template template parameters.
    return Arg;

  case TemplateArgument::Integral:
    return Arg;

  case TemplateArgument::Expression: {
    // Template argument expressions are not potentially evaluated.
    EnterExpressionEvaluationContext Unevaluated(*this, Action::Unevaluated);

    Sema::OwningExprResult E = InstantiateExpr(Arg.getAsExpr(), TemplateArgs);
    if (E.isInvalid())
      return TemplateArgument();
    return TemplateArgument(E.takeAs<Expr>());
  }
  
  case TemplateArgument::Pack:
    assert(0 && "FIXME: Implement!");
    break;
  }

  assert(false && "Unhandled template argument kind");
  return TemplateArgument();
}
