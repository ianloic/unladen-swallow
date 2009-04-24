//===--- SemaType.cpp - Semantic Analysis for Types -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements type-related semantic analysis.
//
//===----------------------------------------------------------------------===//

#include "Sema.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/Parse/DeclSpec.h"
using namespace clang;

/// \brief Convert the specified declspec to the appropriate type
/// object.
/// \param DS  the declaration specifiers
/// \returns The type described by the declaration specifiers, or NULL
/// if there was an error.
QualType Sema::ConvertDeclSpecToType(const DeclSpec &DS) {
  // FIXME: Should move the logic from DeclSpec::Finish to here for validity
  // checking.
  QualType Result;
  
  switch (DS.getTypeSpecType()) {
  default: assert(0 && "Unknown TypeSpecType!");
  case DeclSpec::TST_void:
    Result = Context.VoidTy;
    break;
  case DeclSpec::TST_char:
    if (DS.getTypeSpecSign() == DeclSpec::TSS_unspecified)
      Result = Context.CharTy;
    else if (DS.getTypeSpecSign() == DeclSpec::TSS_signed)
      Result = Context.SignedCharTy;
    else {
      assert(DS.getTypeSpecSign() == DeclSpec::TSS_unsigned &&
             "Unknown TSS value");
      Result = Context.UnsignedCharTy;
    }
    break;
  case DeclSpec::TST_wchar:
    if (DS.getTypeSpecSign() == DeclSpec::TSS_unspecified)
      Result = Context.WCharTy;
    else if (DS.getTypeSpecSign() == DeclSpec::TSS_signed) {
      Diag(DS.getTypeSpecSignLoc(), diag::ext_invalid_sign_spec)
        << DS.getSpecifierName(DS.getTypeSpecType());
      Result = Context.getSignedWCharType();
    } else {
      assert(DS.getTypeSpecSign() == DeclSpec::TSS_unsigned &&
        "Unknown TSS value");
      Diag(DS.getTypeSpecSignLoc(), diag::ext_invalid_sign_spec)
        << DS.getSpecifierName(DS.getTypeSpecType());
      Result = Context.getUnsignedWCharType();
    }
    break;
  case DeclSpec::TST_unspecified:
    // "<proto1,proto2>" is an objc qualified ID with a missing id.
    if (DeclSpec::ProtocolQualifierListTy PQ = DS.getProtocolQualifiers()) {
      Result = Context.getObjCQualifiedIdType((ObjCProtocolDecl**)PQ,
                                              DS.getNumProtocolQualifiers());
      break;
    }
      
    // Unspecified typespec defaults to int in C90.  However, the C90 grammar
    // [C90 6.5] only allows a decl-spec if there was *some* type-specifier,
    // type-qualifier, or storage-class-specifier.  If not, emit an extwarn.
    // Note that the one exception to this is function definitions, which are
    // allowed to be completely missing a declspec.  This is handled in the
    // parser already though by it pretending to have seen an 'int' in this
    // case.
    if (getLangOptions().ImplicitInt) {
      if ((DS.getParsedSpecifiers() & (DeclSpec::PQ_StorageClassSpecifier |
                                       DeclSpec::PQ_TypeSpecifier |
                                       DeclSpec::PQ_TypeQualifier)) == 0)
        Diag(DS.getSourceRange().getBegin(), diag::ext_missing_declspec);
    } else {
      // C99 and C++ require a type specifier.  For example, C99 6.7.2p2 says:
      // "At least one type specifier shall be given in the declaration
      // specifiers in each declaration, and in the specifier-qualifier list in
      // each struct declaration and type name."
      // FIXME: this should be a hard error in C++
      if (!DS.hasTypeSpecifier())
        Diag(DS.getSourceRange().getBegin(), diag::ext_missing_type_specifier);
    }
      
    // FALL THROUGH.  
  case DeclSpec::TST_int: {
    if (DS.getTypeSpecSign() != DeclSpec::TSS_unsigned) {
      switch (DS.getTypeSpecWidth()) {
      case DeclSpec::TSW_unspecified: Result = Context.IntTy; break;
      case DeclSpec::TSW_short:       Result = Context.ShortTy; break;
      case DeclSpec::TSW_long:        Result = Context.LongTy; break;
      case DeclSpec::TSW_longlong:    Result = Context.LongLongTy; break;
      }
    } else {
      switch (DS.getTypeSpecWidth()) {
      case DeclSpec::TSW_unspecified: Result = Context.UnsignedIntTy; break;
      case DeclSpec::TSW_short:       Result = Context.UnsignedShortTy; break;
      case DeclSpec::TSW_long:        Result = Context.UnsignedLongTy; break;
      case DeclSpec::TSW_longlong:    Result =Context.UnsignedLongLongTy; break;
      }
    }
    break;
  }
  case DeclSpec::TST_float: Result = Context.FloatTy; break;
  case DeclSpec::TST_double:
    if (DS.getTypeSpecWidth() == DeclSpec::TSW_long)
      Result = Context.LongDoubleTy;
    else
      Result = Context.DoubleTy;
    break;
  case DeclSpec::TST_bool: Result = Context.BoolTy; break; // _Bool or bool
  case DeclSpec::TST_decimal32:    // _Decimal32
  case DeclSpec::TST_decimal64:    // _Decimal64
  case DeclSpec::TST_decimal128:   // _Decimal128
    assert(0 && "FIXME: GNU decimal extensions not supported yet!"); 
  case DeclSpec::TST_class:
  case DeclSpec::TST_enum:
  case DeclSpec::TST_union:
  case DeclSpec::TST_struct: {
    Decl *D = static_cast<Decl *>(DS.getTypeRep());
    assert(D && "Didn't get a decl for a class/enum/union/struct?");
    assert(DS.getTypeSpecWidth() == 0 && DS.getTypeSpecComplex() == 0 &&
           DS.getTypeSpecSign() == 0 &&
           "Can't handle qualifiers on typedef names yet!");
    // TypeQuals handled by caller.
    Result = Context.getTypeDeclType(cast<TypeDecl>(D));
    break;
  }    
  case DeclSpec::TST_typedef: {
    Decl *D = static_cast<Decl *>(DS.getTypeRep());
    assert(D && "Didn't get a decl for a typedef?");
    assert(DS.getTypeSpecWidth() == 0 && DS.getTypeSpecComplex() == 0 &&
           DS.getTypeSpecSign() == 0 &&
           "Can't handle qualifiers on typedef names yet!");
    DeclSpec::ProtocolQualifierListTy PQ = DS.getProtocolQualifiers();      

    // FIXME: Adding a TST_objcInterface clause doesn't seem ideal, so
    // we have this "hack" for now... 
    if (ObjCInterfaceDecl *ObjCIntDecl = dyn_cast<ObjCInterfaceDecl>(D)) {
      if (PQ == 0) {
        Result = Context.getObjCInterfaceType(ObjCIntDecl);
        break;
      }
      
      Result = Context.getObjCQualifiedInterfaceType(ObjCIntDecl,
                                                     (ObjCProtocolDecl**)PQ,
                                                 DS.getNumProtocolQualifiers());
      break;
    } else if (TypedefDecl *typeDecl = dyn_cast<TypedefDecl>(D)) {
      if (Context.getObjCIdType() == Context.getTypedefType(typeDecl) && PQ) {
        // id<protocol-list>
        Result = Context.getObjCQualifiedIdType((ObjCProtocolDecl**)PQ,
                                                DS.getNumProtocolQualifiers());
        break;
      }
    }
    // TypeQuals handled by caller.
    Result = Context.getTypeDeclType(dyn_cast<TypeDecl>(D));
    break;
  }
  case DeclSpec::TST_typeofType:
    Result = QualType::getFromOpaquePtr(DS.getTypeRep());
    assert(!Result.isNull() && "Didn't get a type for typeof?");
    // TypeQuals handled by caller.
    Result = Context.getTypeOfType(Result);
    break;
  case DeclSpec::TST_typeofExpr: {
    Expr *E = static_cast<Expr *>(DS.getTypeRep());
    assert(E && "Didn't get an expression for typeof?");
    // TypeQuals handled by caller.
    Result = Context.getTypeOfExpr(E);
    break;
  }
  }
  
  // Handle complex types.
  if (DS.getTypeSpecComplex() == DeclSpec::TSC_complex)
    Result = Context.getComplexType(Result);
  
  assert(DS.getTypeSpecComplex() != DeclSpec::TSC_imaginary &&
         "FIXME: imaginary types not supported yet!");
  
  // See if there are any attributes on the declspec that apply to the type (as
  // opposed to the decl).
  if (const AttributeList *AL = DS.getAttributes())
    ProcessTypeAttributeList(Result, AL);
    
  // Apply const/volatile/restrict qualifiers to T.
  if (unsigned TypeQuals = DS.getTypeQualifiers()) {

    // Enforce C99 6.7.3p2: "Types other than pointer types derived from object
    // or incomplete types shall not be restrict-qualified."  C++ also allows
    // restrict-qualified references.
    if (TypeQuals & QualType::Restrict) {
      if (const PointerLikeType *PT = Result->getAsPointerLikeType()) {
        QualType EltTy = PT->getPointeeType();
      
        // If we have a pointer or reference, the pointee must have an object or
        // incomplete type.
        if (!EltTy->isIncompleteOrObjectType()) {
          Diag(DS.getRestrictSpecLoc(),
               diag::err_typecheck_invalid_restrict_invalid_pointee)
            << EltTy << DS.getSourceRange();
          TypeQuals &= ~QualType::Restrict; // Remove the restrict qualifier.
        }
      } else {
        Diag(DS.getRestrictSpecLoc(),
             diag::err_typecheck_invalid_restrict_not_pointer)
          << Result << DS.getSourceRange();
        TypeQuals &= ~QualType::Restrict; // Remove the restrict qualifier.
      }
    }
    
    // Warn about CV qualifiers on functions: C99 6.7.3p8: "If the specification
    // of a function type includes any type qualifiers, the behavior is
    // undefined."
    if (Result->isFunctionType() && TypeQuals) {
      // Get some location to point at, either the C or V location.
      SourceLocation Loc;
      if (TypeQuals & QualType::Const)
        Loc = DS.getConstSpecLoc();
      else {
        assert((TypeQuals & QualType::Volatile) &&
               "Has CV quals but not C or V?");
        Loc = DS.getVolatileSpecLoc();
      }
      Diag(Loc, diag::warn_typecheck_function_qualifiers)
        << Result << DS.getSourceRange();
    }
    
    // C++ [dcl.ref]p1:
    //   Cv-qualified references are ill-formed except when the
    //   cv-qualifiers are introduced through the use of a typedef
    //   (7.1.3) or of a template type argument (14.3), in which
    //   case the cv-qualifiers are ignored.
    if (DS.getTypeSpecType() == DeclSpec::TST_typedef &&
        TypeQuals && Result->isReferenceType()) {
      TypeQuals &= ~QualType::Const;
      TypeQuals &= ~QualType::Volatile;
    }      
    
    Result = Result.getQualifiedType(TypeQuals);
  }
  return Result;
}

/// GetTypeForDeclarator - Convert the type for the specified declarator to Type
/// instances. Skip the outermost Skip type objects.
QualType Sema::GetTypeForDeclarator(Declarator &D, Scope *S, unsigned Skip) {
  // long long is a C99 feature.
  if (!getLangOptions().C99 && !getLangOptions().CPlusPlus0x &&
      D.getDeclSpec().getTypeSpecWidth() == DeclSpec::TSW_longlong)
    Diag(D.getDeclSpec().getTypeSpecWidthLoc(), diag::ext_longlong);

  // Determine the type of the declarator. Not all forms of declarator
  // have a type.
  QualType T;
  switch (D.getKind()) {
  case Declarator::DK_Abstract:
  case Declarator::DK_Normal:
  case Declarator::DK_Operator:
    T = ConvertDeclSpecToType(D.getDeclSpec());
    break;

  case Declarator::DK_Constructor:
  case Declarator::DK_Destructor:
  case Declarator::DK_Conversion:
    // Constructors and destructors don't have return types. Use
    // "void" instead. Conversion operators will check their return
    // types separately.
    T = Context.VoidTy;
    break;
  }

  // Walk the DeclTypeInfo, building the recursive type as we go.  DeclTypeInfos
  // are ordered from the identifier out, which is opposite of what we want :).
  for (unsigned i = Skip, e = D.getNumTypeObjects(); i != e; ++i) {
    DeclaratorChunk &DeclType = D.getTypeObject(e-i-1+Skip);
    switch (DeclType.Kind) {
    default: assert(0 && "Unknown decltype!");
    case DeclaratorChunk::BlockPointer:
      if (DeclType.Cls.TypeQuals)
        Diag(D.getIdentifierLoc(), diag::err_qualified_block_pointer_type);
      if (!T.getTypePtr()->isFunctionType())
        Diag(D.getIdentifierLoc(), diag::err_nonfunction_block_type);
      else
        T = Context.getBlockPointerType(T);
      break;
    case DeclaratorChunk::Pointer:
      if (T->isReferenceType()) {
        // C++ 8.3.2p4: There shall be no ... pointers to references ...
        Diag(DeclType.Loc, diag::err_illegal_decl_pointer_to_reference)
         << (D.getIdentifier() ? D.getIdentifier()->getName() : "type name");
        D.setInvalidType(true);
        T = Context.IntTy;
      }

      // Enforce C99 6.7.3p2: "Types other than pointer types derived from
      // object or incomplete types shall not be restrict-qualified."
      if ((DeclType.Ptr.TypeQuals & QualType::Restrict) &&
          !T->isIncompleteOrObjectType()) {
        Diag(DeclType.Loc, diag::err_typecheck_invalid_restrict_invalid_pointee)
          << T;
        DeclType.Ptr.TypeQuals &= ~QualType::Restrict;
      }

      // Apply the pointer typequals to the pointer object.
      T = Context.getPointerType(T).getQualifiedType(DeclType.Ptr.TypeQuals);
      break;
    case DeclaratorChunk::Reference: {
      // Whether we should suppress the creation of the reference.
      bool SuppressReference = false;
      if (T->isReferenceType()) {
        // C++ [dcl.ref]p4: There shall be no references to references.
        // 
        // According to C++ DR 106, references to references are only
        // diagnosed when they are written directly (e.g., "int & &"),
        // but not when they happen via a typedef:
        //
        //   typedef int& intref;
        //   typedef intref& intref2;
        //
        // Parser::ParserDeclaratorInternal diagnoses the case where
        // references are written directly; here, we handle the
        // collapsing of references-to-references as described in C++
        // DR 106 and amended by C++ DR 540.
        SuppressReference = true;
      }

      // C++ [dcl.ref]p1:
      //   A declarator that specifies the type “reference to cv void”
      //   is ill-formed.
      if (T->isVoidType()) {
        Diag(DeclType.Loc, diag::err_reference_to_void);
        D.setInvalidType(true);
        T = Context.IntTy;
      }

      // Enforce C99 6.7.3p2: "Types other than pointer types derived from
      // object or incomplete types shall not be restrict-qualified."
      if (DeclType.Ref.HasRestrict &&
          !T->isIncompleteOrObjectType()) {
        Diag(DeclType.Loc, diag::err_typecheck_invalid_restrict_invalid_pointee)
          << T;
        DeclType.Ref.HasRestrict = false;
      }        

      if (!SuppressReference)
        T = Context.getReferenceType(T);

      // Handle restrict on references.
      if (DeclType.Ref.HasRestrict)
        T.addRestrict();
      break;
    }
    case DeclaratorChunk::Array: {
      DeclaratorChunk::ArrayTypeInfo &ATI = DeclType.Arr;
      Expr *ArraySize = static_cast<Expr*>(ATI.NumElts);
      ArrayType::ArraySizeModifier ASM;
      if (ATI.isStar)
        ASM = ArrayType::Star;
      else if (ATI.hasStatic)
        ASM = ArrayType::Static;
      else
        ASM = ArrayType::Normal;

      // C99 6.7.5.2p1: If the element type is an incomplete or function type, 
      // reject it (e.g. void ary[7], struct foo ary[7], void ary[7]())
      if (DiagnoseIncompleteType(D.getIdentifierLoc(), T, 
                                 diag::err_illegal_decl_array_incomplete_type)) {
        T = Context.IntTy;
        D.setInvalidType(true);
      } else if (T->isFunctionType()) {
        Diag(D.getIdentifierLoc(), diag::err_illegal_decl_array_of_functions)
          << (D.getIdentifier() ? D.getIdentifier()->getName() : "type name");
        T = Context.getPointerType(T);
        D.setInvalidType(true);
      } else if (const ReferenceType *RT = T->getAsReferenceType()) {
        // C++ 8.3.2p4: There shall be no ... arrays of references ...
        Diag(D.getIdentifierLoc(), diag::err_illegal_decl_array_of_references)
          << (D.getIdentifier() ? D.getIdentifier()->getName() : "type name");
        T = RT->getPointeeType();
        D.setInvalidType(true);
      } else if (const RecordType *EltTy = T->getAsRecordType()) {
        // If the element type is a struct or union that contains a variadic
        // array, reject it: C99 6.7.2.1p2.
        if (EltTy->getDecl()->hasFlexibleArrayMember()) {
          Diag(DeclType.Loc, diag::err_flexible_array_in_array) << T;
          T = Context.IntTy;
          D.setInvalidType(true);
        }
      } else if (T->isObjCInterfaceType()) {
        Diag(DeclType.Loc, diag::warn_objc_array_of_interfaces) << T;
      }
      
      // C99 6.7.5.2p1: The size expression shall have integer type.
      if (ArraySize && !ArraySize->getType()->isIntegerType()) {
        Diag(ArraySize->getLocStart(), diag::err_array_size_non_int)
          << ArraySize->getType() << ArraySize->getSourceRange();
        D.setInvalidType(true);
        delete ArraySize;
        ATI.NumElts = ArraySize = 0;
      }
      llvm::APSInt ConstVal(32);
      if (!ArraySize) {
        T = Context.getIncompleteArrayType(T, ASM, ATI.TypeQuals);
      } else if (ArraySize->isValueDependent()) {
        T = Context.getDependentSizedArrayType(T, ArraySize, ASM, ATI.TypeQuals);
      } else if (!ArraySize->isIntegerConstantExpr(ConstVal, Context) ||
                 !T->isConstantSizeType()) {
        // Per C99, a variable array is an array with either a non-constant
        // size or an element type that has a non-constant-size
        T = Context.getVariableArrayType(T, ArraySize, ASM, ATI.TypeQuals);
      } else {
        // C99 6.7.5.2p1: If the expression is a constant expression, it shall
        // have a value greater than zero.
        if (ConstVal.isSigned()) {
          if (ConstVal.isNegative()) {
            Diag(ArraySize->getLocStart(),
                 diag::err_typecheck_negative_array_size)
              << ArraySize->getSourceRange();
            D.setInvalidType(true);
          } else if (ConstVal == 0) {
            // GCC accepts zero sized static arrays.
            Diag(ArraySize->getLocStart(), diag::ext_typecheck_zero_array_size)
              << ArraySize->getSourceRange();
          }
        } 
        T = Context.getConstantArrayType(T, ConstVal, ASM, ATI.TypeQuals);
      }
      // If this is not C99, extwarn about VLA's and C99 array size modifiers.
      if (!getLangOptions().C99) {
        if (ArraySize && !ArraySize->isValueDependent() && 
            !ArraySize->isIntegerConstantExpr(Context))
          Diag(D.getIdentifierLoc(), diag::ext_vla);
        else if (ASM != ArrayType::Normal || ATI.TypeQuals != 0)
          Diag(D.getIdentifierLoc(), diag::ext_c99_array_usage);
      }
      break;
    }
    case DeclaratorChunk::Function: {
      // If the function declarator has a prototype (i.e. it is not () and
      // does not have a K&R-style identifier list), then the arguments are part
      // of the type, otherwise the argument list is ().
      const DeclaratorChunk::FunctionTypeInfo &FTI = DeclType.Fun;
      
      // C99 6.7.5.3p1: The return type may not be a function or array type.
      if (T->isArrayType() || T->isFunctionType()) {
        Diag(DeclType.Loc, diag::err_func_returning_array_function) << T;
        T = Context.IntTy;
        D.setInvalidType(true);
      }
        
      if (FTI.NumArgs == 0) {
        if (getLangOptions().CPlusPlus) {
          // C++ 8.3.5p2: If the parameter-declaration-clause is empty, the
          // function takes no arguments.
          T = Context.getFunctionType(T, NULL, 0, FTI.isVariadic,FTI.TypeQuals);
        } else {
          // Simple void foo(), where the incoming T is the result type.
          T = Context.getFunctionTypeNoProto(T);
        }
      } else if (FTI.ArgInfo[0].Param == 0) {
        // C99 6.7.5.3p3: Reject int(x,y,z) when it's not a function definition.
        Diag(FTI.ArgInfo[0].IdentLoc, diag::err_ident_list_in_fn_declaration);        
      } else {
        // Otherwise, we have a function with an argument list that is
        // potentially variadic.
        llvm::SmallVector<QualType, 16> ArgTys;
        
        for (unsigned i = 0, e = FTI.NumArgs; i != e; ++i) {
          ParmVarDecl *Param = (ParmVarDecl *)FTI.ArgInfo[i].Param;
          QualType ArgTy = Param->getType();
          assert(!ArgTy.isNull() && "Couldn't parse type?");
          //
          // Perform the default function/array conversion (C99 6.7.5.3p[7,8]).
          // This matches the conversion that is done in 
          // Sema::ActOnParamDeclarator(). Without this conversion, the
          // argument type in the function prototype *will not* match the
          // type in ParmVarDecl (which makes the code generator unhappy).
          //
          // FIXME: We still apparently need the conversion in 
          // Sema::ActOnParamDeclarator(). This doesn't make any sense, since
          // it should be driving off the type being created here.
          // 
          // FIXME: If a source translation tool needs to see the original type,
          // then we need to consider storing both types somewhere...
          // 
          if (ArgTy->isArrayType()) {
            ArgTy = Context.getArrayDecayedType(ArgTy);
          } else if (ArgTy->isFunctionType())
            ArgTy = Context.getPointerType(ArgTy);
          
          // Look for 'void'.  void is allowed only as a single argument to a
          // function with no other parameters (C99 6.7.5.3p10).  We record
          // int(void) as a FunctionTypeProto with an empty argument list.
          else if (ArgTy->isVoidType()) {
            // If this is something like 'float(int, void)', reject it.  'void'
            // is an incomplete type (C99 6.2.5p19) and function decls cannot
            // have arguments of incomplete type.
            if (FTI.NumArgs != 1 || FTI.isVariadic) {
              Diag(DeclType.Loc, diag::err_void_only_param);
              ArgTy = Context.IntTy;
              Param->setType(ArgTy);
            } else if (FTI.ArgInfo[i].Ident) {
              // Reject, but continue to parse 'int(void abc)'.
              Diag(FTI.ArgInfo[i].IdentLoc,
                   diag::err_param_with_void_type);
              ArgTy = Context.IntTy;
              Param->setType(ArgTy);
            } else {
              // Reject, but continue to parse 'float(const void)'.
              if (ArgTy.getCVRQualifiers())
                Diag(DeclType.Loc, diag::err_void_param_qualified);
              
              // Do not add 'void' to the ArgTys list.
              break;
            }
          } else if (!FTI.hasPrototype) {
            if (ArgTy->isPromotableIntegerType()) {
              ArgTy = Context.IntTy;
            } else if (const BuiltinType* BTy = ArgTy->getAsBuiltinType()) {
              if (BTy->getKind() == BuiltinType::Float)
                ArgTy = Context.DoubleTy;
            }
          }
          
          ArgTys.push_back(ArgTy);
        }
        T = Context.getFunctionType(T, &ArgTys[0], ArgTys.size(),
                                    FTI.isVariadic, FTI.TypeQuals);
      }
      break;
    }
    case DeclaratorChunk::MemberPointer:
      // The scope spec must refer to a class, or be dependent.
      DeclContext *DC = static_cast<DeclContext*>(
        DeclType.Mem.Scope().getScopeRep());
      QualType ClsType;
      // FIXME: Extend for dependent types when it's actually supported.
      // See ActOnCXXNestedNameSpecifier.
      if (CXXRecordDecl *RD = dyn_cast_or_null<CXXRecordDecl>(DC)) {
        ClsType = Context.getTagDeclType(RD);
      } else {
        if (DC) {
          Diag(DeclType.Mem.Scope().getBeginLoc(),
               diag::err_illegal_decl_mempointer_in_nonclass)
            << (D.getIdentifier() ? D.getIdentifier()->getName() : "type name")
            << DeclType.Mem.Scope().getRange();
        }
        D.setInvalidType(true);
        ClsType = Context.IntTy;
      }

      // C++ 8.3.3p3: A pointer to member shall not pointer to ... a member
      //   with reference type, or "cv void."
      if (T->isReferenceType()) {
        Diag(DeclType.Loc, diag::err_illegal_decl_pointer_to_reference)
          << (D.getIdentifier() ? D.getIdentifier()->getName() : "type name");
        D.setInvalidType(true);
        T = Context.IntTy;
      }
      if (T->isVoidType()) {
        Diag(DeclType.Loc, diag::err_illegal_decl_mempointer_to_void)
          << (D.getIdentifier() ? D.getIdentifier()->getName() : "type name");
        T = Context.IntTy;
      }

      // Enforce C99 6.7.3p2: "Types other than pointer types derived from
      // object or incomplete types shall not be restrict-qualified."
      if ((DeclType.Mem.TypeQuals & QualType::Restrict) &&
          !T->isIncompleteOrObjectType()) {
        Diag(DeclType.Loc, diag::err_typecheck_invalid_restrict_invalid_pointee)
          << T;
        DeclType.Mem.TypeQuals &= ~QualType::Restrict;
      }

      T = Context.getMemberPointerType(T, ClsType.getTypePtr()).
                    getQualifiedType(DeclType.Mem.TypeQuals);

      break;
    }

    // See if there are any attributes on this declarator chunk.
    if (const AttributeList *AL = DeclType.getAttrs())
      ProcessTypeAttributeList(T, AL);
  }

  if (getLangOptions().CPlusPlus && T->isFunctionType()) {
    const FunctionTypeProto *FnTy = T->getAsFunctionTypeProto();
    assert(FnTy && "Why oh why is there not a FunctionTypeProto here ?");

    // C++ 8.3.5p4: A cv-qualifier-seq shall only be part of the function type
    // for a nonstatic member function, the function type to which a pointer
    // to member refers, or the top-level function type of a function typedef
    // declaration.
    if (FnTy->getTypeQuals() != 0 &&
        D.getDeclSpec().getStorageClassSpec() != DeclSpec::SCS_typedef &&
        ((D.getContext() != Declarator::MemberContext &&
          (!D.getCXXScopeSpec().isSet() ||
           !static_cast<DeclContext*>(D.getCXXScopeSpec().getScopeRep())
              ->isRecord())) ||
         D.getDeclSpec().getStorageClassSpec() == DeclSpec::SCS_static)) {
      if (D.isFunctionDeclarator())
        Diag(D.getIdentifierLoc(), diag::err_invalid_qualified_function_type);
      else
        Diag(D.getIdentifierLoc(),
             diag::err_invalid_qualified_typedef_function_type_use);

      // Strip the cv-quals from the type.
      T = Context.getFunctionType(FnTy->getResultType(), FnTy->arg_type_begin(),
                                  FnTy->getNumArgs(), FnTy->isVariadic(), 0);
    }
  }
  
  // If there were any type attributes applied to the decl itself (not the
  // type, apply the type attribute to the type!)
  if (const AttributeList *Attrs = D.getAttributes())
    ProcessTypeAttributeList(T, Attrs);
  
  return T;
}

/// ObjCGetTypeForMethodDefinition - Builds the type for a method definition
/// declarator
QualType Sema::ObjCGetTypeForMethodDefinition(DeclTy *D) {
  ObjCMethodDecl *MDecl = dyn_cast<ObjCMethodDecl>(static_cast<Decl *>(D));
  QualType T = MDecl->getResultType();
  llvm::SmallVector<QualType, 16> ArgTys;
  
  // Add the first two invisible argument types for self and _cmd.
  if (MDecl->isInstanceMethod()) {
    QualType selfTy = Context.getObjCInterfaceType(MDecl->getClassInterface());
    selfTy = Context.getPointerType(selfTy);
    ArgTys.push_back(selfTy);
  }
  else
    ArgTys.push_back(Context.getObjCIdType());
  ArgTys.push_back(Context.getObjCSelType());
      
  for (int i = 0, e = MDecl->getNumParams(); i != e; ++i) {
    ParmVarDecl *PDecl = MDecl->getParamDecl(i);
    QualType ArgTy = PDecl->getType();
    assert(!ArgTy.isNull() && "Couldn't parse type?");
    // Perform the default function/array conversion (C99 6.7.5.3p[7,8]).
    // This matches the conversion that is done in 
    // Sema::ActOnParamDeclarator(). 
    if (ArgTy->isArrayType())
      ArgTy = Context.getArrayDecayedType(ArgTy);
    else if (ArgTy->isFunctionType())
      ArgTy = Context.getPointerType(ArgTy);
    ArgTys.push_back(ArgTy);
  }
  T = Context.getFunctionType(T, &ArgTys[0], ArgTys.size(),
                              MDecl->isVariadic(), 0);
  return T;
}

/// UnwrapSimilarPointerTypes - If T1 and T2 are pointer types  that
/// may be similar (C++ 4.4), replaces T1 and T2 with the type that
/// they point to and return true. If T1 and T2 aren't pointer types
/// or pointer-to-member types, or if they are not similar at this
/// level, returns false and leaves T1 and T2 unchanged. Top-level
/// qualifiers on T1 and T2 are ignored. This function will typically
/// be called in a loop that successively "unwraps" pointer and
/// pointer-to-member types to compare them at each level.
bool Sema::UnwrapSimilarPointerTypes(QualType& T1, QualType& T2)
{
  const PointerType *T1PtrType = T1->getAsPointerType(),
                    *T2PtrType = T2->getAsPointerType();
  if (T1PtrType && T2PtrType) {
    T1 = T1PtrType->getPointeeType();
    T2 = T2PtrType->getPointeeType();
    return true;
  }

  const MemberPointerType *T1MPType = T1->getAsMemberPointerType(),
                          *T2MPType = T2->getAsMemberPointerType();
  if (T1MPType && T2MPType &&
      Context.getCanonicalType(T1MPType->getClass()) ==
      Context.getCanonicalType(T2MPType->getClass())) {
    T1 = T1MPType->getPointeeType();
    T2 = T2MPType->getPointeeType();
    return true;
  }
  return false;
}

Sema::TypeResult Sema::ActOnTypeName(Scope *S, Declarator &D) {
  // C99 6.7.6: Type names have no identifier.  This is already validated by
  // the parser.
  assert(D.getIdentifier() == 0 && "Type name should have no identifier!");
  
  QualType T = GetTypeForDeclarator(D, S);

  assert(!T.isNull() && "GetTypeForDeclarator() returned null type");
  
  // Check that there are no default arguments (C++ only).
  if (getLangOptions().CPlusPlus)
    CheckExtraCXXDefaultArguments(D);

  // In this context, we *do not* check D.getInvalidType(). If the declarator
  // type was invalid, GetTypeForDeclarator() still returns a "valid" type,
  // though it will not reflect the user specified type.
  return T.getAsOpaquePtr();
}



//===----------------------------------------------------------------------===//
// Type Attribute Processing
//===----------------------------------------------------------------------===//

/// HandleAddressSpaceTypeAttribute - Process an address_space attribute on the
/// specified type.  The attribute contains 1 argument, the id of the address
/// space for the type.
static void HandleAddressSpaceTypeAttribute(QualType &Type, 
                                            const AttributeList &Attr, Sema &S){
  // If this type is already address space qualified, reject it.
  // Clause 6.7.3 - Type qualifiers: "No type shall be qualified by qualifiers
  // for two or more different address spaces."
  if (Type.getAddressSpace()) {
    S.Diag(Attr.getLoc(), diag::err_attribute_address_multiple_qualifiers);
    return;
  }
  
  // Check the attribute arguments.
  if (Attr.getNumArgs() != 1) {
    S.Diag(Attr.getLoc(), diag::err_attribute_wrong_number_arguments) << 1;
    return;
  }
  Expr *ASArgExpr = static_cast<Expr *>(Attr.getArg(0));
  llvm::APSInt addrSpace(32);
  if (!ASArgExpr->isIntegerConstantExpr(addrSpace, S.Context)) {
    S.Diag(Attr.getLoc(), diag::err_attribute_address_space_not_int)
      << ASArgExpr->getSourceRange();
    return;
  }

  unsigned ASIdx = static_cast<unsigned>(addrSpace.getZExtValue()); 
  Type = S.Context.getASQualType(Type, ASIdx);
}

void Sema::ProcessTypeAttributeList(QualType &Result, const AttributeList *AL) {
  // Scan through and apply attributes to this type where it makes sense.  Some
  // attributes (such as __address_space__, __vector_size__, etc) apply to the
  // type, but others can be present in the type specifiers even though they
  // apply to the decl.  Here we apply type attributes and ignore the rest.
  for (; AL; AL = AL->getNext()) {
    // If this is an attribute we can handle, do so now, otherwise, add it to
    // the LeftOverAttrs list for rechaining.
    switch (AL->getKind()) {
    default: break;
    case AttributeList::AT_address_space:
      HandleAddressSpaceTypeAttribute(Result, *AL, *this);
      break;
    }
  }
}

/// @brief If the type T is incomplete and cannot be completed,
/// produce a suitable diagnostic.
///
/// This routine checks whether the type @p T is complete in any
/// context where a complete type is required. If @p T is a complete
/// type, returns false. If @p T is incomplete, issues the diagnostic
/// @p diag (giving it the type @p T) and returns true.
///
/// @param Loc  The location in the source that the incomplete type
/// diagnostic should refer to.
///
/// @param T  The type that this routine is examining for completeness.
///
/// @param diag The diagnostic value (e.g., 
/// @c diag::err_typecheck_decl_incomplete_type) that will be used
/// for the error message if @p T is incomplete.
///
/// @param Range1  An optional range in the source code that will be a
/// part of the "incomplete type" error message.
///
/// @param Range2  An optional range in the source code that will be a
/// part of the "incomplete type" error message.
///
/// @param PrintType If non-NULL, the type that should be printed
/// instead of @p T. This parameter should be used when the type that
/// we're checking for incompleteness isn't the type that should be
/// displayed to the user, e.g., when T is a type and PrintType is a
/// pointer to T.
///
/// @returns @c true if @p T is incomplete and a diagnostic was emitted,
/// @c false otherwise.
///
/// @todo When Clang gets proper support for C++ templates, this
/// routine will also be able perform template instantiation when @p T
/// is a class template specialization.
bool Sema::DiagnoseIncompleteType(SourceLocation Loc, QualType T, unsigned diag,
                                  SourceRange Range1, SourceRange Range2,
                                  QualType PrintType) {
  // If we have a complete type, we're done.
  if (!T->isIncompleteType())
    return false;

  if (PrintType.isNull())
    PrintType = T;

  // We have an incomplete type. Produce a diagnostic.
  Diag(Loc, diag) << PrintType << Range1 << Range2;

  // If the type was a forward declaration of a class/struct/union
  // type, produce 
  const TagType *Tag = 0;
  if (const RecordType *Record = T->getAsRecordType())
    Tag = Record;
  else if (const EnumType *Enum = T->getAsEnumType())
    Tag = Enum;

  if (Tag && !Tag->getDecl()->isInvalidDecl())
    Diag(Tag->getDecl()->getLocation(), 
         Tag->isBeingDefined() ? diag::note_type_being_defined
                               : diag::note_forward_declaration)
        << QualType(Tag, 0);

  return true;
}
