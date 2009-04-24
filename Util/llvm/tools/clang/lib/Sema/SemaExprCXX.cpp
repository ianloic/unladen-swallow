//===--- SemaExprCXX.cpp - Semantic Analysis for Expressions --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for C++ expressions.
//
//===----------------------------------------------------------------------===//

#include "Sema.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ASTContext.h"
#include "clang/Parse/DeclSpec.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/STLExtras.h"
using namespace clang;

/// ActOnCXXConversionFunctionExpr - Parse a C++ conversion function
/// name (e.g., operator void const *) as an expression. This is
/// very similar to ActOnIdentifierExpr, except that instead of
/// providing an identifier the parser provides the type of the
/// conversion function.
Sema::OwningExprResult
Sema::ActOnCXXConversionFunctionExpr(Scope *S, SourceLocation OperatorLoc,
                                     TypeTy *Ty, bool HasTrailingLParen,
                                     const CXXScopeSpec &SS) {
  QualType ConvType = QualType::getFromOpaquePtr(Ty);
  QualType ConvTypeCanon = Context.getCanonicalType(ConvType);
  DeclarationName ConvName 
    = Context.DeclarationNames.getCXXConversionFunctionName(ConvTypeCanon);
  return ActOnDeclarationNameExpr(S, OperatorLoc, ConvName, HasTrailingLParen,
                                  &SS);
}

/// ActOnCXXOperatorFunctionIdExpr - Parse a C++ overloaded operator
/// name (e.g., @c operator+ ) as an expression. This is very
/// similar to ActOnIdentifierExpr, except that instead of providing
/// an identifier the parser provides the kind of overloaded
/// operator that was parsed.
Sema::OwningExprResult
Sema::ActOnCXXOperatorFunctionIdExpr(Scope *S, SourceLocation OperatorLoc,
                                     OverloadedOperatorKind Op,
                                     bool HasTrailingLParen,
                                     const CXXScopeSpec &SS) {
  DeclarationName Name = Context.DeclarationNames.getCXXOperatorName(Op);
  return ActOnDeclarationNameExpr(S, OperatorLoc, Name, HasTrailingLParen, &SS);
}

/// ActOnCXXTypeidOfType - Parse typeid( type-id ).
Action::ExprResult
Sema::ActOnCXXTypeid(SourceLocation OpLoc, SourceLocation LParenLoc,
                     bool isType, void *TyOrExpr, SourceLocation RParenLoc) {
  NamespaceDecl *StdNs = GetStdNamespace();
  if (!StdNs)
    return Diag(OpLoc, diag::err_need_header_before_typeid);
  
  IdentifierInfo *TypeInfoII = &PP.getIdentifierTable().get("type_info");
  Decl *TypeInfoDecl = LookupQualifiedName(StdNs, TypeInfoII, LookupTagName);
  RecordDecl *TypeInfoRecordDecl = dyn_cast_or_null<RecordDecl>(TypeInfoDecl);
  if (!TypeInfoRecordDecl)
    return Diag(OpLoc, diag::err_need_header_before_typeid);

  QualType TypeInfoType = Context.getTypeDeclType(TypeInfoRecordDecl);

  return new CXXTypeidExpr(isType, TyOrExpr, TypeInfoType.withConst(),
                           SourceRange(OpLoc, RParenLoc));
}

/// ActOnCXXBoolLiteral - Parse {true,false} literals.
Action::ExprResult
Sema::ActOnCXXBoolLiteral(SourceLocation OpLoc, tok::TokenKind Kind) {
  assert((Kind == tok::kw_true || Kind == tok::kw_false) &&
         "Unknown C++ Boolean value!");
  return new CXXBoolLiteralExpr(Kind == tok::kw_true, Context.BoolTy, OpLoc);
}

/// ActOnCXXThrow - Parse throw expressions.
Action::ExprResult
Sema::ActOnCXXThrow(SourceLocation OpLoc, ExprTy *E) {
  return new CXXThrowExpr((Expr*)E, Context.VoidTy, OpLoc);
}

Action::ExprResult Sema::ActOnCXXThis(SourceLocation ThisLoc) {
  /// C++ 9.3.2: In the body of a non-static member function, the keyword this
  /// is a non-lvalue expression whose value is the address of the object for
  /// which the function is called.

  if (!isa<FunctionDecl>(CurContext)) {
    Diag(ThisLoc, diag::err_invalid_this_use);
    return ExprResult(true);
  }

  if (CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(CurContext))
    if (MD->isInstance())
      return new CXXThisExpr(ThisLoc, MD->getThisType(Context));

  return Diag(ThisLoc, diag::err_invalid_this_use);
}

/// ActOnCXXTypeConstructExpr - Parse construction of a specified type.
/// Can be interpreted either as function-style casting ("int(x)")
/// or class type construction ("ClassType(x,y,z)")
/// or creation of a value-initialized type ("int()").
Action::ExprResult
Sema::ActOnCXXTypeConstructExpr(SourceRange TypeRange, TypeTy *TypeRep,
                                SourceLocation LParenLoc,
                                ExprTy **ExprTys, unsigned NumExprs,
                                SourceLocation *CommaLocs,
                                SourceLocation RParenLoc) {
  assert(TypeRep && "Missing type!");
  QualType Ty = QualType::getFromOpaquePtr(TypeRep);
  Expr **Exprs = (Expr**)ExprTys;
  SourceLocation TyBeginLoc = TypeRange.getBegin();
  SourceRange FullRange = SourceRange(TyBeginLoc, RParenLoc);

  // C++ [expr.type.conv]p1:
  // If the expression list is a single expression, the type conversion
  // expression is equivalent (in definedness, and if defined in meaning) to the
  // corresponding cast expression.
  //
  if (NumExprs == 1) {
    if (CheckCastTypes(TypeRange, Ty, Exprs[0]))
      return true;
    return new CXXFunctionalCastExpr(Ty.getNonReferenceType(), Ty, TyBeginLoc, 
                                     Exprs[0], RParenLoc);
  }

  if (const RecordType *RT = Ty->getAsRecordType()) {
    CXXRecordDecl *Record = cast<CXXRecordDecl>(RT->getDecl());
    
    if (NumExprs > 1 || Record->hasUserDeclaredConstructor()) {
      CXXConstructorDecl *Constructor
        = PerformInitializationByConstructor(Ty, Exprs, NumExprs,
                                             TypeRange.getBegin(),
                                             SourceRange(TypeRange.getBegin(),
                                                         RParenLoc),
                                             DeclarationName(),
                                             IK_Direct);
      
      if (!Constructor)
        return true;

      return new CXXTemporaryObjectExpr(Constructor, Ty, TyBeginLoc, 
                                        Exprs, NumExprs, RParenLoc);
    }

    // Fall through to value-initialize an object of class type that
    // doesn't have a user-declared default constructor.
  }

  // C++ [expr.type.conv]p1:
  // If the expression list specifies more than a single value, the type shall
  // be a class with a suitably declared constructor.
  //
  if (NumExprs > 1)
    return Diag(CommaLocs[0], diag::err_builtin_func_cast_more_than_one_arg)
      << FullRange;

  assert(NumExprs == 0 && "Expected 0 expressions");

  // C++ [expr.type.conv]p2:
  // The expression T(), where T is a simple-type-specifier for a non-array
  // complete object type or the (possibly cv-qualified) void type, creates an
  // rvalue of the specified type, which is value-initialized.
  //
  if (Ty->isArrayType())
    return Diag(TyBeginLoc, diag::err_value_init_for_array_type) << FullRange;
  if (!Ty->isDependentType() && !Ty->isVoidType() &&
      DiagnoseIncompleteType(TyBeginLoc, Ty, 
                             diag::err_invalid_incomplete_type_use, FullRange))
    return true;

  return new CXXZeroInitValueExpr(Ty, TyBeginLoc, RParenLoc);
}


/// ActOnCXXNew - Parsed a C++ 'new' expression (C++ 5.3.4), as in e.g.:
/// @code new (memory) int[size][4] @endcode
/// or
/// @code ::new Foo(23, "hello") @endcode
/// For the interpretation of this heap of arguments, consult the base version.
Action::ExprResult
Sema::ActOnCXXNew(SourceLocation StartLoc, bool UseGlobal,
                  SourceLocation PlacementLParen,
                  ExprTy **PlacementArgs, unsigned NumPlaceArgs,
                  SourceLocation PlacementRParen, bool ParenTypeId,
                  Declarator &D, SourceLocation ConstructorLParen,
                  ExprTy **ConstructorArgs, unsigned NumConsArgs,
                  SourceLocation ConstructorRParen)
{
  // FIXME: Throughout this function, we have rather bad location information.
  // Implementing Declarator::getSourceRange() would go a long way toward
  // fixing that.

  Expr *ArraySize = 0;
  unsigned Skip = 0;
  // If the specified type is an array, unwrap it and save the expression.
  if (D.getNumTypeObjects() > 0 &&
      D.getTypeObject(0).Kind == DeclaratorChunk::Array) {
    DeclaratorChunk &Chunk = D.getTypeObject(0);
    if (Chunk.Arr.hasStatic)
      return Diag(Chunk.Loc, diag::err_static_illegal_in_new);
    if (!Chunk.Arr.NumElts)
      return Diag(Chunk.Loc, diag::err_array_new_needs_size);
    ArraySize = static_cast<Expr*>(Chunk.Arr.NumElts);
    Skip = 1;
  }

  QualType AllocType = GetTypeForDeclarator(D, /*Scope=*/0, Skip);
  if (D.getInvalidType())
    return true;

  if (CheckAllocatedType(AllocType, D))
    return true;

  QualType ResultType = Context.getPointerType(AllocType);

  // That every array dimension except the first is constant was already
  // checked by the type check above.

  // C++ 5.3.4p6: "The expression in a direct-new-declarator shall have integral
  //   or enumeration type with a non-negative value."
  if (ArraySize) {
    QualType SizeType = ArraySize->getType();
    if (!SizeType->isIntegralType() && !SizeType->isEnumeralType())
      return Diag(ArraySize->getSourceRange().getBegin(),
                  diag::err_array_size_not_integral)
        << SizeType << ArraySize->getSourceRange();
    // Let's see if this is a constant < 0. If so, we reject it out of hand.
    // We don't care about special rules, so we tell the machinery it's not
    // evaluated - it gives us a result in more cases.
    llvm::APSInt Value;
    if (ArraySize->isIntegerConstantExpr(Value, Context, 0, false)) {
      if (Value < llvm::APSInt(
                      llvm::APInt::getNullValue(Value.getBitWidth()), false))
        return Diag(ArraySize->getSourceRange().getBegin(),
                    diag::err_typecheck_negative_array_size)
          << ArraySize->getSourceRange();
    }
  }

  FunctionDecl *OperatorNew = 0;
  FunctionDecl *OperatorDelete = 0;
  Expr **PlaceArgs = (Expr**)PlacementArgs;
  if (FindAllocationFunctions(StartLoc, UseGlobal, AllocType, ArraySize,
                              PlaceArgs, NumPlaceArgs, OperatorNew,
                              OperatorDelete))
    return true;

  bool Init = ConstructorLParen.isValid();
  // --- Choosing a constructor ---
  // C++ 5.3.4p15
  // 1) If T is a POD and there's no initializer (ConstructorLParen is invalid)
  //   the object is not initialized. If the object, or any part of it, is
  //   const-qualified, it's an error.
  // 2) If T is a POD and there's an empty initializer, the object is value-
  //   initialized.
  // 3) If T is a POD and there's one initializer argument, the object is copy-
  //   constructed.
  // 4) If T is a POD and there's more initializer arguments, it's an error.
  // 5) If T is not a POD, the initializer arguments are used as constructor
  //   arguments.
  //
  // Or by the C++0x formulation:
  // 1) If there's no initializer, the object is default-initialized according
  //    to C++0x rules.
  // 2) Otherwise, the object is direct-initialized.
  CXXConstructorDecl *Constructor = 0;
  Expr **ConsArgs = (Expr**)ConstructorArgs;
  if (const RecordType *RT = AllocType->getAsRecordType()) {
    // FIXME: This is incorrect for when there is an empty initializer and
    // no user-defined constructor. Must zero-initialize, not default-construct.
    Constructor = PerformInitializationByConstructor(
                      AllocType, ConsArgs, NumConsArgs,
                      D.getDeclSpec().getSourceRange().getBegin(),
                      SourceRange(D.getDeclSpec().getSourceRange().getBegin(),
                                  ConstructorRParen),
                      RT->getDecl()->getDeclName(),
                      NumConsArgs != 0 ? IK_Direct : IK_Default);
    if (!Constructor)
      return true;
  } else {
    if (!Init) {
      // FIXME: Check that no subpart is const.
      if (AllocType.isConstQualified()) {
        Diag(StartLoc, diag::err_new_uninitialized_const)
          << D.getSourceRange();
        return true;
      }
    } else if (NumConsArgs == 0) {
      // Object is value-initialized. Do nothing.
    } else if (NumConsArgs == 1) {
      // Object is direct-initialized.
      // FIXME: WHAT DeclarationName do we pass in here?
      if (CheckInitializerTypes(ConsArgs[0], AllocType, StartLoc,
                                DeclarationName() /*AllocType.getAsString()*/,
                                /*DirectInit=*/true))
        return true;
    } else {
      Diag(StartLoc, diag::err_builtin_direct_init_more_than_one_arg)
        << SourceRange(ConstructorLParen, ConstructorRParen);
    }
  }

  // FIXME: Also check that the destructor is accessible. (C++ 5.3.4p16)

  return new CXXNewExpr(UseGlobal, OperatorNew, PlaceArgs, NumPlaceArgs,
                        ParenTypeId, ArraySize, Constructor, Init,
                        ConsArgs, NumConsArgs, OperatorDelete, ResultType,
                        StartLoc, Init ? ConstructorRParen : SourceLocation());
}

/// CheckAllocatedType - Checks that a type is suitable as the allocated type
/// in a new-expression.
/// dimension off and stores the size expression in ArraySize.
bool Sema::CheckAllocatedType(QualType AllocType, const Declarator &D)
{
  // C++ 5.3.4p1: "[The] type shall be a complete object type, but not an
  //   abstract class type or array thereof.
  // FIXME: We don't have abstract types yet.
  // FIXME: Under C++ semantics, an incomplete object type is still an object
  // type. This code assumes the C semantics, where it's not.
  if (!AllocType->isObjectType()) {
    unsigned type; // For the select in the message.
    if (AllocType->isFunctionType()) {
      type = 0;
    } else if(AllocType->isIncompleteType()) {
      type = 1;
    } else {
      assert(AllocType->isReferenceType() && "What else could it be?");
      type = 2;
    }
    SourceRange TyR = D.getDeclSpec().getSourceRange();
    // FIXME: This is very much a guess and won't work for, e.g., pointers.
    if (D.getNumTypeObjects() > 0)
      TyR.setEnd(D.getTypeObject(0).Loc);
    Diag(TyR.getBegin(), diag::err_bad_new_type)
      << AllocType.getAsString() << type << TyR;
    return true;
  }

  // Every dimension shall be of constant size.
  unsigned i = 1;
  while (const ArrayType *Array = Context.getAsArrayType(AllocType)) {
    if (!Array->isConstantArrayType()) {
      Diag(D.getTypeObject(i).Loc, diag::err_new_array_nonconst)
        << static_cast<Expr*>(D.getTypeObject(i).Arr.NumElts)->getSourceRange();
      return true;
    }
    AllocType = Array->getElementType();
    ++i;
  }

  return false;
}

/// FindAllocationFunctions - Finds the overloads of operator new and delete
/// that are appropriate for the allocation.
bool Sema::FindAllocationFunctions(SourceLocation StartLoc, bool UseGlobal,
                                   QualType AllocType, bool IsArray,
                                   Expr **PlaceArgs, unsigned NumPlaceArgs,
                                   FunctionDecl *&OperatorNew,
                                   FunctionDecl *&OperatorDelete)
{
  // --- Choosing an allocation function ---
  // C++ 5.3.4p8 - 14 & 18
  // 1) If UseGlobal is true, only look in the global scope. Else, also look
  //   in the scope of the allocated class.
  // 2) If an array size is given, look for operator new[], else look for
  //   operator new.
  // 3) The first argument is always size_t. Append the arguments from the
  //   placement form.
  // FIXME: Also find the appropriate delete operator.

  llvm::SmallVector<Expr*, 8> AllocArgs(1 + NumPlaceArgs);
  // We don't care about the actual value of this argument.
  // FIXME: Should the Sema create the expression and embed it in the syntax
  // tree? Or should the consumer just recalculate the value?
  AllocArgs[0] = new IntegerLiteral(llvm::APInt::getNullValue(
                                        Context.Target.getPointerWidth(0)),
                                    Context.getSizeType(),
                                    SourceLocation());
  std::copy(PlaceArgs, PlaceArgs + NumPlaceArgs, AllocArgs.begin() + 1);

  DeclarationName NewName = Context.DeclarationNames.getCXXOperatorName(
                                        IsArray ? OO_Array_New : OO_New);
  if (AllocType->isRecordType() && !UseGlobal) {
    CXXRecordDecl *Record = cast<CXXRecordType>(AllocType->getAsRecordType())
                                ->getDecl();
    // FIXME: We fail to find inherited overloads.
    if (FindAllocationOverload(StartLoc, NewName, &AllocArgs[0],
                          AllocArgs.size(), Record, /*AllowMissing=*/true,
                          OperatorNew))
      return true;
  }
  if (!OperatorNew) {
    // Didn't find a member overload. Look for a global one.
    DeclareGlobalNewDelete();
    DeclContext *TUDecl = Context.getTranslationUnitDecl();
    if (FindAllocationOverload(StartLoc, NewName, &AllocArgs[0],
                          AllocArgs.size(), TUDecl, /*AllowMissing=*/false,
                          OperatorNew))
      return true;
  }

  // FIXME: This is leaked on error. But so much is currently in Sema that it's
  // easier to clean it in one go.
  AllocArgs[0]->Destroy(Context);
  return false;
}

/// FindAllocationOverload - Find an fitting overload for the allocation
/// function in the specified scope.
bool Sema::FindAllocationOverload(SourceLocation StartLoc, DeclarationName Name,
                                  Expr** Args, unsigned NumArgs,
                                  DeclContext *Ctx, bool AllowMissing,
                                  FunctionDecl *&Operator)
{
  DeclContext::lookup_iterator Alloc, AllocEnd;
  llvm::tie(Alloc, AllocEnd) = Ctx->lookup(Name);
  if (Alloc == AllocEnd) {
    if (AllowMissing)
      return false;
    // FIXME: Bad location information.
    return Diag(StartLoc, diag::err_ovl_no_viable_function_in_call)
      << Name << 0;
  }

  OverloadCandidateSet Candidates;
  for (; Alloc != AllocEnd; ++Alloc) {
    // Even member operator new/delete are implicitly treated as
    // static, so don't use AddMemberCandidate.
    if (FunctionDecl *Fn = dyn_cast<FunctionDecl>(*Alloc))
      AddOverloadCandidate(Fn, Args, NumArgs, Candidates,
                           /*SuppressUserConversions=*/false);
  }

  // Do the resolution.
  OverloadCandidateSet::iterator Best;
  switch(BestViableFunction(Candidates, Best)) {
  case OR_Success: {
    // Got one!
    FunctionDecl *FnDecl = Best->Function;
    // The first argument is size_t, and the first parameter must be size_t,
    // too. This is checked on declaration and can be assumed. (It can't be
    // asserted on, though, since invalid decls are left in there.)
    for (unsigned i = 1; i < NumArgs; ++i) {
      // FIXME: Passing word to diagnostic.
      if (PerformCopyInitialization(Args[i-1],
                                    FnDecl->getParamDecl(i)->getType(),
                                    "passing"))
        return true;
    }
    Operator = FnDecl;
    return false;
  }

  case OR_No_Viable_Function:
    if (AllowMissing)
      return false;
    // FIXME: Bad location information.
    Diag(StartLoc, diag::err_ovl_no_viable_function_in_call)
      << Name << (unsigned)Candidates.size();
    PrintOverloadCandidates(Candidates, /*OnlyViable=*/false);
    return true;

  case OR_Ambiguous:
    // FIXME: Bad location information.
    Diag(StartLoc, diag::err_ovl_ambiguous_call)
      << Name;
    PrintOverloadCandidates(Candidates, /*OnlyViable=*/true);
    return true;
  }
  assert(false && "Unreachable, bad result from BestViableFunction");
  return true;
}


/// DeclareGlobalNewDelete - Declare the global forms of operator new and
/// delete. These are:
/// @code
///   void* operator new(std::size_t) throw(std::bad_alloc);
///   void* operator new[](std::size_t) throw(std::bad_alloc);
///   void operator delete(void *) throw();
///   void operator delete[](void *) throw();
/// @endcode
/// Note that the placement and nothrow forms of new are *not* implicitly
/// declared. Their use requires including \<new\>.
void Sema::DeclareGlobalNewDelete()
{
  if (GlobalNewDeleteDeclared)
    return;
  GlobalNewDeleteDeclared = true;

  QualType VoidPtr = Context.getPointerType(Context.VoidTy);
  QualType SizeT = Context.getSizeType();

  // FIXME: Exception specifications are not added.
  DeclareGlobalAllocationFunction(
      Context.DeclarationNames.getCXXOperatorName(OO_New),
      VoidPtr, SizeT);
  DeclareGlobalAllocationFunction(
      Context.DeclarationNames.getCXXOperatorName(OO_Array_New),
      VoidPtr, SizeT);
  DeclareGlobalAllocationFunction(
      Context.DeclarationNames.getCXXOperatorName(OO_Delete),
      Context.VoidTy, VoidPtr);
  DeclareGlobalAllocationFunction(
      Context.DeclarationNames.getCXXOperatorName(OO_Array_Delete),
      Context.VoidTy, VoidPtr);
}

/// DeclareGlobalAllocationFunction - Declares a single implicit global
/// allocation function if it doesn't already exist.
void Sema::DeclareGlobalAllocationFunction(DeclarationName Name,
                                           QualType Return, QualType Argument)
{
  DeclContext *GlobalCtx = Context.getTranslationUnitDecl();

  // Check if this function is already declared.
  {
    DeclContext::lookup_iterator Alloc, AllocEnd;
    for (llvm::tie(Alloc, AllocEnd) = GlobalCtx->lookup(Name);
         Alloc != AllocEnd; ++Alloc) {
      // FIXME: Do we need to check for default arguments here?
      FunctionDecl *Func = cast<FunctionDecl>(*Alloc);
      if (Func->getNumParams() == 1 &&
          Context.getCanonicalType(Func->getParamDecl(0)->getType()) == Argument)
        return;
    }
  }

  QualType FnType = Context.getFunctionType(Return, &Argument, 1, false, 0);
  FunctionDecl *Alloc =
    FunctionDecl::Create(Context, GlobalCtx, SourceLocation(), Name,
                         FnType, FunctionDecl::None, false,
                         SourceLocation());
  Alloc->setImplicit();
  ParmVarDecl *Param = ParmVarDecl::Create(Context, Alloc, SourceLocation(),
                                           0, Argument, VarDecl::None, 0);
  Alloc->setParams(Context, &Param, 1);

  // FIXME: Also add this declaration to the IdentifierResolver, but
  // make sure it is at the end of the chain to coincide with the
  // global scope.
  ((DeclContext *)TUScope->getEntity())->addDecl(Alloc);
}

/// ActOnCXXDelete - Parsed a C++ 'delete' expression (C++ 5.3.5), as in:
/// @code ::delete ptr; @endcode
/// or
/// @code delete [] ptr; @endcode
Action::ExprResult
Sema::ActOnCXXDelete(SourceLocation StartLoc, bool UseGlobal,
                     bool ArrayForm, ExprTy *Operand)
{
  // C++ 5.3.5p1: "The operand shall have a pointer type, or a class type
  //   having a single conversion function to a pointer type. The result has
  //   type void."
  // DR599 amends "pointer type" to "pointer to object type" in both cases.

  Expr *Ex = (Expr *)Operand;
  QualType Type = Ex->getType();

  if (Type->isRecordType()) {
    // FIXME: Find that one conversion function and amend the type.
  }

  if (!Type->isPointerType()) {
    Diag(StartLoc, diag::err_delete_operand) << Type << Ex->getSourceRange();
    return true;
  }

  QualType Pointee = Type->getAsPointerType()->getPointeeType();
  if (!Pointee->isVoidType() && 
      DiagnoseIncompleteType(StartLoc, Pointee, diag::warn_delete_incomplete,
                             Ex->getSourceRange()))
    return true;
  else if (!Pointee->isObjectType()) {
    Diag(StartLoc, diag::err_delete_operand)
      << Type << Ex->getSourceRange();
    return true;
  }

  // FIXME: Look up the correct operator delete overload and pass a pointer
  // along.
  // FIXME: Check access and ambiguity of operator delete and destructor.

  return new CXXDeleteExpr(Context.VoidTy, UseGlobal, ArrayForm, 0, Ex,
                           StartLoc);
}


/// ActOnCXXConditionDeclarationExpr - Parsed a condition declaration of a
/// C++ if/switch/while/for statement.
/// e.g: "if (int x = f()) {...}"
Action::ExprResult
Sema::ActOnCXXConditionDeclarationExpr(Scope *S, SourceLocation StartLoc,
                                       Declarator &D,
                                       SourceLocation EqualLoc,
                                       ExprTy *AssignExprVal) {
  assert(AssignExprVal && "Null assignment expression");

  // C++ 6.4p2:
  // The declarator shall not specify a function or an array.
  // The type-specifier-seq shall not contain typedef and shall not declare a
  // new class or enumeration.

  assert(D.getDeclSpec().getStorageClassSpec() != DeclSpec::SCS_typedef &&
         "Parser allowed 'typedef' as storage class of condition decl.");

  QualType Ty = GetTypeForDeclarator(D, S);
  
  if (Ty->isFunctionType()) { // The declarator shall not specify a function...
    // We exit without creating a CXXConditionDeclExpr because a FunctionDecl
    // would be created and CXXConditionDeclExpr wants a VarDecl.
    return Diag(StartLoc, diag::err_invalid_use_of_function_type)
      << SourceRange(StartLoc, EqualLoc);
  } else if (Ty->isArrayType()) { // ...or an array.
    Diag(StartLoc, diag::err_invalid_use_of_array_type)
      << SourceRange(StartLoc, EqualLoc);
  } else if (const RecordType *RT = Ty->getAsRecordType()) {
    RecordDecl *RD = RT->getDecl();
    // The type-specifier-seq shall not declare a new class...
    if (RD->isDefinition() && (RD->getIdentifier() == 0 || S->isDeclScope(RD)))
      Diag(RD->getLocation(), diag::err_type_defined_in_condition);
  } else if (const EnumType *ET = Ty->getAsEnumType()) {
    EnumDecl *ED = ET->getDecl();
    // ...or enumeration.
    if (ED->isDefinition() && (ED->getIdentifier() == 0 || S->isDeclScope(ED)))
      Diag(ED->getLocation(), diag::err_type_defined_in_condition);
  }

  DeclTy *Dcl = ActOnDeclarator(S, D, 0);
  if (!Dcl)
    return true;
  AddInitializerToDecl(Dcl, ExprArg(*this, AssignExprVal));

  // Mark this variable as one that is declared within a conditional.
  if (VarDecl *VD = dyn_cast<VarDecl>((Decl *)Dcl))
    VD->setDeclaredInCondition(true);

  return new CXXConditionDeclExpr(StartLoc, EqualLoc,
                                       cast<VarDecl>(static_cast<Decl *>(Dcl)));
}

/// CheckCXXBooleanCondition - Returns true if a conversion to bool is invalid.
bool Sema::CheckCXXBooleanCondition(Expr *&CondExpr) {
  // C++ 6.4p4:
  // The value of a condition that is an initialized declaration in a statement
  // other than a switch statement is the value of the declared variable
  // implicitly converted to type bool. If that conversion is ill-formed, the
  // program is ill-formed.
  // The value of a condition that is an expression is the value of the
  // expression, implicitly converted to bool.
  //
  return PerformContextuallyConvertToBool(CondExpr);
}

/// Helper function to determine whether this is the (deprecated) C++
/// conversion from a string literal to a pointer to non-const char or
/// non-const wchar_t (for narrow and wide string literals,
/// respectively).
bool 
Sema::IsStringLiteralToNonConstPointerConversion(Expr *From, QualType ToType) {
  // Look inside the implicit cast, if it exists.
  if (ImplicitCastExpr *Cast = dyn_cast<ImplicitCastExpr>(From))
    From = Cast->getSubExpr();

  // A string literal (2.13.4) that is not a wide string literal can
  // be converted to an rvalue of type "pointer to char"; a wide
  // string literal can be converted to an rvalue of type "pointer
  // to wchar_t" (C++ 4.2p2).
  if (StringLiteral *StrLit = dyn_cast<StringLiteral>(From))
    if (const PointerType *ToPtrType = ToType->getAsPointerType())
      if (const BuiltinType *ToPointeeType 
          = ToPtrType->getPointeeType()->getAsBuiltinType()) {
        // This conversion is considered only when there is an
        // explicit appropriate pointer target type (C++ 4.2p2).
        if (ToPtrType->getPointeeType().getCVRQualifiers() == 0 &&
            ((StrLit->isWide() && ToPointeeType->isWideCharType()) ||
             (!StrLit->isWide() &&
              (ToPointeeType->getKind() == BuiltinType::Char_U ||
               ToPointeeType->getKind() == BuiltinType::Char_S))))
          return true;
      }

  return false;
}

/// PerformImplicitConversion - Perform an implicit conversion of the
/// expression From to the type ToType. Returns true if there was an
/// error, false otherwise. The expression From is replaced with the
/// converted expression. Flavor is the kind of conversion we're
/// performing, used in the error message. If @p AllowExplicit,
/// explicit user-defined conversions are permitted.
bool 
Sema::PerformImplicitConversion(Expr *&From, QualType ToType,
                                const char *Flavor, bool AllowExplicit)
{
  ImplicitConversionSequence ICS = TryImplicitConversion(From, ToType, false,
                                                         AllowExplicit);
  return PerformImplicitConversion(From, ToType, ICS, Flavor);
}

/// PerformImplicitConversion - Perform an implicit conversion of the
/// expression From to the type ToType using the pre-computed implicit
/// conversion sequence ICS. Returns true if there was an error, false
/// otherwise. The expression From is replaced with the converted
/// expression. Flavor is the kind of conversion we're performing,
/// used in the error message.
bool
Sema::PerformImplicitConversion(Expr *&From, QualType ToType,
                                const ImplicitConversionSequence &ICS,
                                const char* Flavor) {
  switch (ICS.ConversionKind) {
  case ImplicitConversionSequence::StandardConversion:
    if (PerformImplicitConversion(From, ToType, ICS.Standard, Flavor))
      return true;
    break;

  case ImplicitConversionSequence::UserDefinedConversion:
    // FIXME: This is, of course, wrong. We'll need to actually call
    // the constructor or conversion operator, and then cope with the
    // standard conversions.
    ImpCastExprToType(From, ToType.getNonReferenceType(), 
                      ToType->isReferenceType());
    return false;

  case ImplicitConversionSequence::EllipsisConversion:
    assert(false && "Cannot perform an ellipsis conversion");
    return false;

  case ImplicitConversionSequence::BadConversion:
    return true;
  }

  // Everything went well.
  return false;
}

/// PerformImplicitConversion - Perform an implicit conversion of the
/// expression From to the type ToType by following the standard
/// conversion sequence SCS. Returns true if there was an error, false
/// otherwise. The expression From is replaced with the converted
/// expression. Flavor is the context in which we're performing this
/// conversion, for use in error messages.
bool 
Sema::PerformImplicitConversion(Expr *&From, QualType ToType,
                                const StandardConversionSequence& SCS,
                                const char *Flavor) {
  // Overall FIXME: we are recomputing too many types here and doing
  // far too much extra work. What this means is that we need to keep
  // track of more information that is computed when we try the
  // implicit conversion initially, so that we don't need to recompute
  // anything here.
  QualType FromType = From->getType();

  if (SCS.CopyConstructor) {
    // FIXME: Create a temporary object by calling the copy
    // constructor.
    ImpCastExprToType(From, ToType.getNonReferenceType(), 
                      ToType->isReferenceType());
    return false;
  }

  // Perform the first implicit conversion.
  switch (SCS.First) {
  case ICK_Identity:
  case ICK_Lvalue_To_Rvalue:
    // Nothing to do.
    break;

  case ICK_Array_To_Pointer:
    if (FromType->isOverloadType()) {
      FunctionDecl *Fn = ResolveAddressOfOverloadedFunction(From, ToType, true);
      if (!Fn)
        return true;

      FixOverloadedFunctionReference(From, Fn);
      FromType = From->getType();
    } else {
      FromType = Context.getArrayDecayedType(FromType);
    }
    ImpCastExprToType(From, FromType);
    break;

  case ICK_Function_To_Pointer:
    FromType = Context.getPointerType(FromType);
    ImpCastExprToType(From, FromType);
    break;

  default:
    assert(false && "Improper first standard conversion");
    break;
  }

  // Perform the second implicit conversion
  switch (SCS.Second) {
  case ICK_Identity:
    // Nothing to do.
    break;

  case ICK_Integral_Promotion:
  case ICK_Floating_Promotion:
  case ICK_Integral_Conversion:
  case ICK_Floating_Conversion:
  case ICK_Floating_Integral:
    FromType = ToType.getUnqualifiedType();
    ImpCastExprToType(From, FromType);
    break;

  case ICK_Pointer_Conversion:
    if (SCS.IncompatibleObjC) {
      // Diagnose incompatible Objective-C conversions
      Diag(From->getSourceRange().getBegin(), 
           diag::ext_typecheck_convert_incompatible_pointer)
        << From->getType() << ToType << Flavor
        << From->getSourceRange();
    }

    if (CheckPointerConversion(From, ToType))
      return true;
    ImpCastExprToType(From, ToType);
    break;

  case ICK_Pointer_Member:
    if (CheckMemberPointerConversion(From, ToType))
      return true;
    ImpCastExprToType(From, ToType);
    break;

  case ICK_Boolean_Conversion:
    FromType = Context.BoolTy;
    ImpCastExprToType(From, FromType);
    break;

  default:
    assert(false && "Improper second standard conversion");
    break;
  }

  switch (SCS.Third) {
  case ICK_Identity:
    // Nothing to do.
    break;

  case ICK_Qualification:
    ImpCastExprToType(From, ToType.getNonReferenceType(), 
                      ToType->isReferenceType());
    break;

  default:
    assert(false && "Improper second standard conversion");
    break;
  }

  return false;
}

Sema::OwningExprResult Sema::ActOnUnaryTypeTrait(UnaryTypeTrait OTT,
                                                 SourceLocation KWLoc,
                                                 SourceLocation LParen,
                                                 TypeTy *Ty,
                                                 SourceLocation RParen) {
  // FIXME: Some of the type traits have requirements. Interestingly, only the
  // __is_base_of requirement is explicitly stated to be diagnosed. Indeed,
  // G++ accepts __is_pod(Incomplete) without complaints, and claims that the
  // type is indeed a POD.

  // There is no point in eagerly computing the value. The traits are designed
  // to be used from type trait templates, so Ty will be a template parameter
  // 99% of the time.
  return Owned(new UnaryTypeTraitExpr(KWLoc, OTT,
                                      QualType::getFromOpaquePtr(Ty),
                                      RParen, Context.BoolTy));
}
