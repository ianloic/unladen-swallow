//===--- SemaChecking.cpp - Extra Semantic Checking -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements extra semantic analysis beyond what is enforced 
//  by the C type system.
//
//===----------------------------------------------------------------------===//

#include "Sema.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/Lex/LiteralSupport.h"
#include "clang/Lex/Preprocessor.h"
using namespace clang;

/// getLocationOfStringLiteralByte - Return a source location that points to the
/// specified byte of the specified string literal.
///
/// Strings are amazingly complex.  They can be formed from multiple tokens and
/// can have escape sequences in them in addition to the usual trigraph and
/// escaped newline business.  This routine handles this complexity.
///
SourceLocation Sema::getLocationOfStringLiteralByte(const StringLiteral *SL,
                                                    unsigned ByteNo) const {
  assert(!SL->isWide() && "This doesn't work for wide strings yet");
  
  // Loop over all of the tokens in this string until we find the one that
  // contains the byte we're looking for.
  unsigned TokNo = 0;
  while (1) {
    assert(TokNo < SL->getNumConcatenated() && "Invalid byte number!");
    SourceLocation StrTokLoc = SL->getStrTokenLoc(TokNo);
   
    // Get the spelling of the string so that we can get the data that makes up
    // the string literal, not the identifier for the macro it is potentially
    // expanded through.
    SourceLocation StrTokSpellingLoc = SourceMgr.getSpellingLoc(StrTokLoc);

    // Re-lex the token to get its length and original spelling.
    std::pair<FileID, unsigned> LocInfo =
      SourceMgr.getDecomposedLoc(StrTokSpellingLoc);
    std::pair<const char *,const char *> Buffer =
      SourceMgr.getBufferData(LocInfo.first);
    const char *StrData = Buffer.first+LocInfo.second;
    
    // Create a langops struct and enable trigraphs.  This is sufficient for
    // relexing tokens.
    LangOptions LangOpts;
    LangOpts.Trigraphs = true;
    
    // Create a lexer starting at the beginning of this token.
    Lexer TheLexer(StrTokSpellingLoc, LangOpts, Buffer.first, StrData,
                   Buffer.second);
    Token TheTok;
    TheLexer.LexFromRawLexer(TheTok);
    
    // Use the StringLiteralParser to compute the length of the string in bytes.
    StringLiteralParser SLP(&TheTok, 1, PP);
    unsigned TokNumBytes = SLP.GetStringLength();
    
    // If the byte is in this token, return the location of the byte.
    if (ByteNo < TokNumBytes ||
        (ByteNo == TokNumBytes && TokNo == SL->getNumConcatenated())) {
      unsigned Offset = 
        StringLiteralParser::getOffsetOfStringByte(TheTok, ByteNo, PP);
     
      // Now that we know the offset of the token in the spelling, use the
      // preprocessor to get the offset in the original source.
      return PP.AdvanceToTokenCharacter(StrTokLoc, Offset);
    }
    
    // Move to the next string token.
    ++TokNo;
    ByteNo -= TokNumBytes;
  }
}


/// CheckFunctionCall - Check a direct function call for various correctness
/// and safety properties not strictly enforced by the C type system.
Action::OwningExprResult
Sema::CheckFunctionCall(FunctionDecl *FDecl, CallExpr *TheCall) {
  OwningExprResult TheCallResult(Owned(TheCall));
  // Get the IdentifierInfo* for the called function.
  IdentifierInfo *FnInfo = FDecl->getIdentifier();

  // None of the checks below are needed for functions that don't have
  // simple names (e.g., C++ conversion functions).
  if (!FnInfo)
    return move(TheCallResult);

  switch (FDecl->getBuiltinID(Context)) {
  case Builtin::BI__builtin___CFStringMakeConstantString:
    assert(TheCall->getNumArgs() == 1 &&
           "Wrong # arguments to builtin CFStringMakeConstantString");
    if (CheckObjCString(TheCall->getArg(0)))
      return ExprError();
    return move(TheCallResult);
  case Builtin::BI__builtin_stdarg_start:
  case Builtin::BI__builtin_va_start:
    if (SemaBuiltinVAStart(TheCall))
      return ExprError();
    return move(TheCallResult);
  case Builtin::BI__builtin_isgreater:
  case Builtin::BI__builtin_isgreaterequal:
  case Builtin::BI__builtin_isless:
  case Builtin::BI__builtin_islessequal:
  case Builtin::BI__builtin_islessgreater:
  case Builtin::BI__builtin_isunordered:
    if (SemaBuiltinUnorderedCompare(TheCall))
      return ExprError();
    return move(TheCallResult);
  case Builtin::BI__builtin_return_address:
  case Builtin::BI__builtin_frame_address:
    if (SemaBuiltinStackAddress(TheCall))
      return ExprError();
    return move(TheCallResult);
  case Builtin::BI__builtin_shufflevector:
    return SemaBuiltinShuffleVector(TheCall);
    // TheCall will be freed by the smart pointer here, but that's fine, since
    // SemaBuiltinShuffleVector guts it, but then doesn't release it.
  case Builtin::BI__builtin_prefetch:
    if (SemaBuiltinPrefetch(TheCall))
      return ExprError();
    return move(TheCallResult);
  case Builtin::BI__builtin_object_size:
    if (SemaBuiltinObjectSize(TheCall))
      return ExprError();
    return move(TheCallResult);
  case Builtin::BI__builtin_longjmp:
    if (SemaBuiltinLongjmp(TheCall))
      return ExprError();
    return move(TheCallResult);
  }

  // FIXME: This mechanism should be abstracted to be less fragile and
  // more efficient. For example, just map function ids to custom
  // handlers.

  // Printf checking.
  if (const FormatAttr *Format = FDecl->getAttr<FormatAttr>()) {
    if (Format->getType() == "printf") {
      bool HasVAListArg = Format->getFirstArg() == 0;
      if (!HasVAListArg) {
        if (const FunctionProtoType *Proto 
            = FDecl->getType()->getAsFunctionProtoType())
        HasVAListArg = !Proto->isVariadic();
      }
      CheckPrintfArguments(TheCall, HasVAListArg, Format->getFormatIdx() - 1,
                           HasVAListArg ? 0 : Format->getFirstArg() - 1);
    }
  }

  return move(TheCallResult);
}

/// CheckObjCString - Checks that the argument to the builtin
/// CFString constructor is correct
/// FIXME: GCC currently emits the following warning:
/// "warning: input conversion stopped due to an input byte that does not 
///           belong to the input codeset UTF-8"
/// Note: It might also make sense to do the UTF-16 conversion here (would
/// simplify the backend).
bool Sema::CheckObjCString(Expr *Arg) {
  Arg = Arg->IgnoreParenCasts();
  StringLiteral *Literal = dyn_cast<StringLiteral>(Arg);

  if (!Literal || Literal->isWide()) {
    Diag(Arg->getLocStart(), diag::err_cfstring_literal_not_string_constant)
      << Arg->getSourceRange();
    return true;
  }
  
  const char *Data = Literal->getStrData();
  unsigned Length = Literal->getByteLength();
  
  for (unsigned i = 0; i < Length; ++i) {
    if (!Data[i]) {
      Diag(getLocationOfStringLiteralByte(Literal, i),
           diag::warn_cfstring_literal_contains_nul_character)
        << Arg->getSourceRange();
      break;
    }
  }
  
  return false;
}

/// SemaBuiltinVAStart - Check the arguments to __builtin_va_start for validity.
/// Emit an error and return true on failure, return false on success.
bool Sema::SemaBuiltinVAStart(CallExpr *TheCall) {
  Expr *Fn = TheCall->getCallee();
  if (TheCall->getNumArgs() > 2) {
    Diag(TheCall->getArg(2)->getLocStart(),
         diag::err_typecheck_call_too_many_args)
      << 0 /*function call*/ << Fn->getSourceRange()
      << SourceRange(TheCall->getArg(2)->getLocStart(), 
                     (*(TheCall->arg_end()-1))->getLocEnd());
    return true;
  }

  if (TheCall->getNumArgs() < 2) {
    return Diag(TheCall->getLocEnd(), diag::err_typecheck_call_too_few_args)
      << 0 /*function call*/;
  }

  // Determine whether the current function is variadic or not.
  bool isVariadic;
  if (CurBlock)
    isVariadic = CurBlock->isVariadic;
  else if (getCurFunctionDecl()) {
    if (FunctionProtoType* FTP =
            dyn_cast<FunctionProtoType>(getCurFunctionDecl()->getType()))
      isVariadic = FTP->isVariadic();
    else
      isVariadic = false;
  } else {
    isVariadic = getCurMethodDecl()->isVariadic();
  }
  
  if (!isVariadic) {
    Diag(Fn->getLocStart(), diag::err_va_start_used_in_non_variadic_function);
    return true;
  }
  
  // Verify that the second argument to the builtin is the last argument of the
  // current function or method.
  bool SecondArgIsLastNamedArgument = false;
  const Expr *Arg = TheCall->getArg(1)->IgnoreParenCasts();
  
  if (const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(Arg)) {
    if (const ParmVarDecl *PV = dyn_cast<ParmVarDecl>(DR->getDecl())) {
      // FIXME: This isn't correct for methods (results in bogus warning).
      // Get the last formal in the current function.
      const ParmVarDecl *LastArg;
      if (CurBlock)
        LastArg = *(CurBlock->TheDecl->param_end()-1);
      else if (FunctionDecl *FD = getCurFunctionDecl())
        LastArg = *(FD->param_end()-1);
      else
        LastArg = *(getCurMethodDecl()->param_end()-1);
      SecondArgIsLastNamedArgument = PV == LastArg;
    }
  }
  
  if (!SecondArgIsLastNamedArgument)
    Diag(TheCall->getArg(1)->getLocStart(), 
         diag::warn_second_parameter_of_va_start_not_last_named_argument);
  return false;
}

/// SemaBuiltinUnorderedCompare - Handle functions like __builtin_isgreater and
/// friends.  This is declared to take (...), so we have to check everything.
bool Sema::SemaBuiltinUnorderedCompare(CallExpr *TheCall) {
  if (TheCall->getNumArgs() < 2)
    return Diag(TheCall->getLocEnd(), diag::err_typecheck_call_too_few_args)
      << 0 /*function call*/;
  if (TheCall->getNumArgs() > 2)
    return Diag(TheCall->getArg(2)->getLocStart(), 
                diag::err_typecheck_call_too_many_args)
      << 0 /*function call*/
      << SourceRange(TheCall->getArg(2)->getLocStart(),
                     (*(TheCall->arg_end()-1))->getLocEnd());
  
  Expr *OrigArg0 = TheCall->getArg(0);
  Expr *OrigArg1 = TheCall->getArg(1);
  
  // Do standard promotions between the two arguments, returning their common
  // type.
  QualType Res = UsualArithmeticConversions(OrigArg0, OrigArg1, false);

  // Make sure any conversions are pushed back into the call; this is
  // type safe since unordered compare builtins are declared as "_Bool
  // foo(...)".
  TheCall->setArg(0, OrigArg0);
  TheCall->setArg(1, OrigArg1);
  
  // If the common type isn't a real floating type, then the arguments were
  // invalid for this operation.
  if (!Res->isRealFloatingType())
    return Diag(OrigArg0->getLocStart(), 
                diag::err_typecheck_call_invalid_ordered_compare)
      << OrigArg0->getType() << OrigArg1->getType()
      << SourceRange(OrigArg0->getLocStart(), OrigArg1->getLocEnd());
  
  return false;
}

bool Sema::SemaBuiltinStackAddress(CallExpr *TheCall) {
  // The signature for these builtins is exact; the only thing we need
  // to check is that the argument is a constant.
  SourceLocation Loc;
  if (!TheCall->getArg(0)->isIntegerConstantExpr(Context, &Loc))
    return Diag(Loc, diag::err_stack_const_level) << TheCall->getSourceRange();
  
  return false;
}

/// SemaBuiltinShuffleVector - Handle __builtin_shufflevector.
// This is declared to take (...), so we have to check everything.
Action::OwningExprResult Sema::SemaBuiltinShuffleVector(CallExpr *TheCall) {
  if (TheCall->getNumArgs() < 3)
    return ExprError(Diag(TheCall->getLocEnd(),
                          diag::err_typecheck_call_too_few_args)
      << 0 /*function call*/ << TheCall->getSourceRange());

  QualType FAType = TheCall->getArg(0)->getType();
  QualType SAType = TheCall->getArg(1)->getType();

  if (!FAType->isVectorType() || !SAType->isVectorType()) {
    Diag(TheCall->getLocStart(), diag::err_shufflevector_non_vector)
      << SourceRange(TheCall->getArg(0)->getLocStart(), 
                     TheCall->getArg(1)->getLocEnd());
    return ExprError();
  }

  if (Context.getCanonicalType(FAType).getUnqualifiedType() !=
      Context.getCanonicalType(SAType).getUnqualifiedType()) {
    Diag(TheCall->getLocStart(), diag::err_shufflevector_incompatible_vector)
      << SourceRange(TheCall->getArg(0)->getLocStart(), 
                     TheCall->getArg(1)->getLocEnd());
    return ExprError();
  }

  unsigned numElements = FAType->getAsVectorType()->getNumElements();
  if (TheCall->getNumArgs() != numElements+2) {
    if (TheCall->getNumArgs() < numElements+2)
      return ExprError(Diag(TheCall->getLocEnd(),
                            diag::err_typecheck_call_too_few_args)
               << 0 /*function call*/ << TheCall->getSourceRange());
    return ExprError(Diag(TheCall->getLocEnd(),
                          diag::err_typecheck_call_too_many_args)
             << 0 /*function call*/ << TheCall->getSourceRange());
  }

  for (unsigned i = 2; i < TheCall->getNumArgs(); i++) {
    llvm::APSInt Result(32);
    if (!TheCall->getArg(i)->isIntegerConstantExpr(Result, Context))
      return ExprError(Diag(TheCall->getLocStart(),
                  diag::err_shufflevector_nonconstant_argument)
                << TheCall->getArg(i)->getSourceRange());

    if (Result.getActiveBits() > 64 || Result.getZExtValue() >= numElements*2)
      return ExprError(Diag(TheCall->getLocStart(),
                  diag::err_shufflevector_argument_too_large)
               << TheCall->getArg(i)->getSourceRange());
  }

  llvm::SmallVector<Expr*, 32> exprs;

  for (unsigned i = 0, e = TheCall->getNumArgs(); i != e; i++) {
    exprs.push_back(TheCall->getArg(i));
    TheCall->setArg(i, 0);
  }

  return Owned(new (Context) ShuffleVectorExpr(exprs.begin(), numElements+2,
                                            FAType,
                                            TheCall->getCallee()->getLocStart(),
                                            TheCall->getRParenLoc()));
}

/// SemaBuiltinPrefetch - Handle __builtin_prefetch.
// This is declared to take (const void*, ...) and can take two
// optional constant int args.
bool Sema::SemaBuiltinPrefetch(CallExpr *TheCall) {
  unsigned NumArgs = TheCall->getNumArgs();

  if (NumArgs > 3)
    return Diag(TheCall->getLocEnd(), diag::err_typecheck_call_too_many_args)
             << 0 /*function call*/ << TheCall->getSourceRange();

  // Argument 0 is checked for us and the remaining arguments must be
  // constant integers.
  for (unsigned i = 1; i != NumArgs; ++i) {
    Expr *Arg = TheCall->getArg(i);
    QualType RWType = Arg->getType();

    const BuiltinType *BT = RWType->getAsBuiltinType();
    llvm::APSInt Result;
    if (!BT || BT->getKind() != BuiltinType::Int ||
        !Arg->isIntegerConstantExpr(Result, Context))
      return Diag(TheCall->getLocStart(), diag::err_prefetch_invalid_argument)
              << SourceRange(Arg->getLocStart(), Arg->getLocEnd());
    
    // FIXME: gcc issues a warning and rewrites these to 0. These
    // seems especially odd for the third argument since the default
    // is 3.
    if (i == 1) {
      if (Result.getSExtValue() < 0 || Result.getSExtValue() > 1)
        return Diag(TheCall->getLocStart(), diag::err_argument_invalid_range)
             << "0" << "1" << SourceRange(Arg->getLocStart(), Arg->getLocEnd());
    } else {
      if (Result.getSExtValue() < 0 || Result.getSExtValue() > 3)
        return Diag(TheCall->getLocStart(), diag::err_argument_invalid_range)
            << "0" << "3" << SourceRange(Arg->getLocStart(), Arg->getLocEnd());
    }
  }

  return false;
}

/// SemaBuiltinObjectSize - Handle __builtin_object_size(void *ptr,
/// int type). This simply type checks that type is one of the defined
/// constants (0-3).
bool Sema::SemaBuiltinObjectSize(CallExpr *TheCall) {
  Expr *Arg = TheCall->getArg(1);
  QualType ArgType = Arg->getType();  
  const BuiltinType *BT = ArgType->getAsBuiltinType();  
  llvm::APSInt Result(32);
  if (!BT || BT->getKind() != BuiltinType::Int ||
      !Arg->isIntegerConstantExpr(Result, Context)) {
    return Diag(TheCall->getLocStart(), diag::err_object_size_invalid_argument)
             << SourceRange(Arg->getLocStart(), Arg->getLocEnd());
  }

  if (Result.getSExtValue() < 0 || Result.getSExtValue() > 3) {
    return Diag(TheCall->getLocStart(), diag::err_argument_invalid_range)
             << "0" << "3" << SourceRange(Arg->getLocStart(), Arg->getLocEnd());
  }

  return false;
}

/// SemaBuiltinLongjmp - Handle __builtin_longjmp(void *env[5], int val).
/// This checks that val is a constant 1.
bool Sema::SemaBuiltinLongjmp(CallExpr *TheCall) {
  Expr *Arg = TheCall->getArg(1);
  llvm::APSInt Result(32);
  if (!Arg->isIntegerConstantExpr(Result, Context) || Result != 1)
    return Diag(TheCall->getLocStart(), diag::err_builtin_longjmp_invalid_val)
             << SourceRange(Arg->getLocStart(), Arg->getLocEnd());

  return false;
}

// Handle i > 1 ? "x" : "y", recursivelly
bool Sema::SemaCheckStringLiteral(const Expr *E, const CallExpr *TheCall,
                                  bool HasVAListArg,
                                  unsigned format_idx, unsigned firstDataArg) {

  switch (E->getStmtClass()) {
  case Stmt::ConditionalOperatorClass: {
    const ConditionalOperator *C = cast<ConditionalOperator>(E);
    return SemaCheckStringLiteral(C->getLHS(), TheCall,
                                  HasVAListArg, format_idx, firstDataArg)
        && SemaCheckStringLiteral(C->getRHS(), TheCall,
                                  HasVAListArg, format_idx, firstDataArg);
  }

  case Stmt::ImplicitCastExprClass: {
    const ImplicitCastExpr *Expr = cast<ImplicitCastExpr>(E);
    return SemaCheckStringLiteral(Expr->getSubExpr(), TheCall, HasVAListArg,
                                  format_idx, firstDataArg);
  }

  case Stmt::ParenExprClass: {
    const ParenExpr *Expr = cast<ParenExpr>(E);
    return SemaCheckStringLiteral(Expr->getSubExpr(), TheCall, HasVAListArg,
                                  format_idx, firstDataArg);
  }
      
  case Stmt::DeclRefExprClass: {
    const DeclRefExpr *DR = cast<DeclRefExpr>(E);
    
    // As an exception, do not flag errors for variables binding to
    // const string literals.
    if (const VarDecl *VD = dyn_cast<VarDecl>(DR->getDecl())) {
      bool isConstant = false;
      QualType T = DR->getType();

      if (const ArrayType *AT = Context.getAsArrayType(T)) {
        isConstant = AT->getElementType().isConstant(Context);
      }
      else if (const PointerType *PT = T->getAsPointerType()) {
        isConstant = T.isConstant(Context) && 
                     PT->getPointeeType().isConstant(Context);
      }
        
      if (isConstant) {
        const VarDecl *Def = 0;
        if (const Expr *Init = VD->getDefinition(Def))
          return SemaCheckStringLiteral(Init, TheCall,
                                        HasVAListArg, format_idx, firstDataArg);
      }
    }
        
    return false;
  }

  case Stmt::ObjCStringLiteralClass:
  case Stmt::StringLiteralClass: {
    const StringLiteral *StrE = NULL;
    
    if (const ObjCStringLiteral *ObjCFExpr = dyn_cast<ObjCStringLiteral>(E))
      StrE = ObjCFExpr->getString();
    else
      StrE = cast<StringLiteral>(E);
    
    if (StrE) {
      CheckPrintfString(StrE, E, TheCall, HasVAListArg, format_idx, 
                        firstDataArg);
      return true;
    }
    
    return false;
  }
      
  default:
    return false;
  }
}


/// CheckPrintfArguments - Check calls to printf (and similar functions) for
/// correct use of format strings.  
///
///  HasVAListArg - A predicate indicating whether the printf-like
///    function is passed an explicit va_arg argument (e.g., vprintf)
///
///  format_idx - The index into Args for the format string.
///
/// Improper format strings to functions in the printf family can be
/// the source of bizarre bugs and very serious security holes.  A
/// good source of information is available in the following paper
/// (which includes additional references):
///
///  FormatGuard: Automatic Protection From printf Format String
///  Vulnerabilities, Proceedings of the 10th USENIX Security Symposium, 2001.
///
/// Functionality implemented:
///
///  We can statically check the following properties for string
///  literal format strings for non v.*printf functions (where the
///  arguments are passed directly):
//
///  (1) Are the number of format conversions equal to the number of
///      data arguments?
///
///  (2) Does each format conversion correctly match the type of the
///      corresponding data argument?  (TODO)
///
/// Moreover, for all printf functions we can:
///
///  (3) Check for a missing format string (when not caught by type checking).
///
///  (4) Check for no-operation flags; e.g. using "#" with format
///      conversion 'c'  (TODO)
///
///  (5) Check the use of '%n', a major source of security holes.
///
///  (6) Check for malformed format conversions that don't specify anything.
///
///  (7) Check for empty format strings.  e.g: printf("");
///
///  (8) Check that the format string is a wide literal.
///
///  (9) Also check the arguments of functions with the __format__ attribute.
///      (TODO).
///
/// All of these checks can be done by parsing the format string.
///
/// For now, we ONLY do (1), (3), (5), (6), (7), and (8).
void
Sema::CheckPrintfArguments(const CallExpr *TheCall, bool HasVAListArg, 
                           unsigned format_idx, unsigned firstDataArg) {
  const Expr *Fn = TheCall->getCallee();

  // CHECK: printf-like function is called with no format string.  
  if (format_idx >= TheCall->getNumArgs()) {
    Diag(TheCall->getRParenLoc(), diag::warn_printf_missing_format_string)
      << Fn->getSourceRange();
    return;
  }
  
  const Expr *OrigFormatExpr = TheCall->getArg(format_idx)->IgnoreParenCasts();
  
  // CHECK: format string is not a string literal.
  // 
  // Dynamically generated format strings are difficult to
  // automatically vet at compile time.  Requiring that format strings
  // are string literals: (1) permits the checking of format strings by
  // the compiler and thereby (2) can practically remove the source of
  // many format string exploits.

  // Format string can be either ObjC string (e.g. @"%d") or 
  // C string (e.g. "%d")
  // ObjC string uses the same format specifiers as C string, so we can use 
  // the same format string checking logic for both ObjC and C strings.
  if (SemaCheckStringLiteral(OrigFormatExpr, TheCall, HasVAListArg, format_idx,
                             firstDataArg))
    return;  // Literal format string found, check done!

  // For vprintf* functions (i.e., HasVAListArg==true), we add a
  // special check to see if the format string is a function parameter
  // of the function calling the printf function.  If the function
  // has an attribute indicating it is a printf-like function, then we
  // should suppress warnings concerning non-literals being used in a call
  // to a vprintf function.  For example:
  //
  // void
  // logmessage(char const *fmt __attribute__ (format (printf, 1, 2)), ...) {
  //      va_list ap;
  //      va_start(ap, fmt);
  //      vprintf(fmt, ap);  // Do NOT emit a warning about "fmt".
  //      ...
  //
  //
  //  FIXME: We don't have full attribute support yet, so just check to see
  //    if the argument is a DeclRefExpr that references a parameter.  We'll
  //    add proper support for checking the attribute later.
  if (HasVAListArg)
    if (const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(OrigFormatExpr))
      if (isa<ParmVarDecl>(DR->getDecl()))
        return;

  // If there are no arguments specified, warn with -Wformat-security, otherwise
  // warn only with -Wformat-nonliteral.
  if (TheCall->getNumArgs() == format_idx+1)
    Diag(TheCall->getArg(format_idx)->getLocStart(), 
         diag::warn_printf_nonliteral_noargs)
      << OrigFormatExpr->getSourceRange();
  else
    Diag(TheCall->getArg(format_idx)->getLocStart(), 
         diag::warn_printf_nonliteral)
           << OrigFormatExpr->getSourceRange();
}

void Sema::CheckPrintfString(const StringLiteral *FExpr,
                             const Expr *OrigFormatExpr,
                             const CallExpr *TheCall, bool HasVAListArg,
                             unsigned format_idx, unsigned firstDataArg) {

  const ObjCStringLiteral *ObjCFExpr =
    dyn_cast<ObjCStringLiteral>(OrigFormatExpr);

  // CHECK: is the format string a wide literal?
  if (FExpr->isWide()) {
    Diag(FExpr->getLocStart(),
         diag::warn_printf_format_string_is_wide_literal)
      << OrigFormatExpr->getSourceRange();
    return;
  }

  // Str - The format string.  NOTE: this is NOT null-terminated!
  const char *Str = FExpr->getStrData();

  // CHECK: empty format string?
  unsigned StrLen = FExpr->getByteLength();
  
  if (StrLen == 0) {
    Diag(FExpr->getLocStart(), diag::warn_printf_empty_format_string)
      << OrigFormatExpr->getSourceRange();
    return;
  }

  // We process the format string using a binary state machine.  The
  // current state is stored in CurrentState.
  enum {
    state_OrdChr,
    state_Conversion
  } CurrentState = state_OrdChr;
  
  // numConversions - The number of conversions seen so far.  This is
  //  incremented as we traverse the format string.
  unsigned numConversions = 0;

  // numDataArgs - The number of data arguments after the format
  //  string.  This can only be determined for non vprintf-like
  //  functions.  For those functions, this value is 1 (the sole
  //  va_arg argument).
  unsigned numDataArgs = TheCall->getNumArgs()-firstDataArg;

  // Inspect the format string.
  unsigned StrIdx = 0;
  
  // LastConversionIdx - Index within the format string where we last saw
  //  a '%' character that starts a new format conversion.
  unsigned LastConversionIdx = 0;
  
  for (; StrIdx < StrLen; ++StrIdx) {
    
    // Is the number of detected conversion conversions greater than
    // the number of matching data arguments?  If so, stop.
    if (!HasVAListArg && numConversions > numDataArgs) break;
    
    // Handle "\0"
    if (Str[StrIdx] == '\0') {
      // The string returned by getStrData() is not null-terminated,
      // so the presence of a null character is likely an error.
      Diag(getLocationOfStringLiteralByte(FExpr, StrIdx),
           diag::warn_printf_format_string_contains_null_char)
        <<  OrigFormatExpr->getSourceRange();
      return;
    }
    
    // Ordinary characters (not processing a format conversion).
    if (CurrentState == state_OrdChr) {
      if (Str[StrIdx] == '%') {
        CurrentState = state_Conversion;
        LastConversionIdx = StrIdx;
      }
      continue;
    }

    // Seen '%'.  Now processing a format conversion.
    switch (Str[StrIdx]) {
    // Handle dynamic precision or width specifier.     
    case '*': {
      ++numConversions;
      
      if (!HasVAListArg && numConversions > numDataArgs) {
        SourceLocation Loc = getLocationOfStringLiteralByte(FExpr, StrIdx);

        if (Str[StrIdx-1] == '.')
          Diag(Loc, diag::warn_printf_asterisk_precision_missing_arg)
            << OrigFormatExpr->getSourceRange();
        else
          Diag(Loc, diag::warn_printf_asterisk_width_missing_arg)
            << OrigFormatExpr->getSourceRange();
        
        // Don't do any more checking.  We'll just emit spurious errors.
        return;
      }
      
      // Perform type checking on width/precision specifier.
      const Expr *E = TheCall->getArg(format_idx+numConversions);
      if (const BuiltinType *BT = E->getType()->getAsBuiltinType())
        if (BT->getKind() == BuiltinType::Int)
          break;

      SourceLocation Loc = getLocationOfStringLiteralByte(FExpr, StrIdx);
      
      if (Str[StrIdx-1] == '.')
        Diag(Loc, diag::warn_printf_asterisk_precision_wrong_type)
          << E->getType() << E->getSourceRange();
      else
        Diag(Loc, diag::warn_printf_asterisk_width_wrong_type)
          << E->getType() << E->getSourceRange();
      
      break;
    }
      
    // Characters which can terminate a format conversion
    // (e.g. "%d").  Characters that specify length modifiers or
    // other flags are handled by the default case below.
    //
    // FIXME: additional checks will go into the following cases.                
    case 'i':
    case 'd':
    case 'o': 
    case 'u': 
    case 'x':
    case 'X':
    case 'D':
    case 'O':
    case 'U':
    case 'e':
    case 'E':
    case 'f':
    case 'F':
    case 'g':
    case 'G':
    case 'a':
    case 'A':
    case 'c':
    case 'C':
    case 'S':
    case 's':
    case 'p': 
      ++numConversions;
      CurrentState = state_OrdChr;
      break;

    // CHECK: Are we using "%n"?  Issue a warning.
    case 'n': {
      ++numConversions;
      CurrentState = state_OrdChr;
      SourceLocation Loc = getLocationOfStringLiteralByte(FExpr,
                                                          LastConversionIdx);
                                   
      Diag(Loc, diag::warn_printf_write_back)<<OrigFormatExpr->getSourceRange();
      break;
    }
             
    // Handle "%@"
    case '@':
      // %@ is allowed in ObjC format strings only.
      if(ObjCFExpr != NULL)
        CurrentState = state_OrdChr; 
      else {
        // Issue a warning: invalid format conversion.
        SourceLocation Loc = 
          getLocationOfStringLiteralByte(FExpr, LastConversionIdx);
    
        Diag(Loc, diag::warn_printf_invalid_conversion)
          <<  std::string(Str+LastConversionIdx,
                          Str+std::min(LastConversionIdx+2, StrLen))
          << OrigFormatExpr->getSourceRange();
      }
      ++numConversions;
      break;
    
    // Handle "%%"
    case '%':
      // Sanity check: Was the first "%" character the previous one?
      // If not, we will assume that we have a malformed format
      // conversion, and that the current "%" character is the start
      // of a new conversion.
      if (StrIdx - LastConversionIdx == 1)
        CurrentState = state_OrdChr; 
      else {
        // Issue a warning: invalid format conversion.
        SourceLocation Loc =
          getLocationOfStringLiteralByte(FExpr, LastConversionIdx);
            
        Diag(Loc, diag::warn_printf_invalid_conversion)
          << std::string(Str+LastConversionIdx, Str+StrIdx)
          << OrigFormatExpr->getSourceRange();
             
        // This conversion is broken.  Advance to the next format
        // conversion.
        LastConversionIdx = StrIdx;
        ++numConversions;
      }
      break;
              
    default:
      // This case catches all other characters: flags, widths, etc.
      // We should eventually process those as well.
      break;
    }
  }

  if (CurrentState == state_Conversion) {
    // Issue a warning: invalid format conversion.
    SourceLocation Loc =
      getLocationOfStringLiteralByte(FExpr, LastConversionIdx);
    
    Diag(Loc, diag::warn_printf_invalid_conversion)
      << std::string(Str+LastConversionIdx,
                     Str+std::min(LastConversionIdx+2, StrLen))
      << OrigFormatExpr->getSourceRange();
    return;
  }
  
  if (!HasVAListArg) {
    // CHECK: Does the number of format conversions exceed the number
    //        of data arguments?
    if (numConversions > numDataArgs) {
      SourceLocation Loc =
        getLocationOfStringLiteralByte(FExpr, LastConversionIdx);
                                   
      Diag(Loc, diag::warn_printf_insufficient_data_args)
        << OrigFormatExpr->getSourceRange();
    }
    // CHECK: Does the number of data arguments exceed the number of
    //        format conversions in the format string?
    else if (numConversions < numDataArgs)
      Diag(TheCall->getArg(format_idx+numConversions+1)->getLocStart(),
           diag::warn_printf_too_many_data_args)
        << OrigFormatExpr->getSourceRange();
  }
}

//===--- CHECK: Return Address of Stack Variable --------------------------===//

static DeclRefExpr* EvalVal(Expr *E);
static DeclRefExpr* EvalAddr(Expr* E);

/// CheckReturnStackAddr - Check if a return statement returns the address
///   of a stack variable.
void
Sema::CheckReturnStackAddr(Expr *RetValExp, QualType lhsType,
                           SourceLocation ReturnLoc) {
   
  // Perform checking for returned stack addresses.
  if (lhsType->isPointerType() || lhsType->isBlockPointerType()) {
    if (DeclRefExpr *DR = EvalAddr(RetValExp))
      Diag(DR->getLocStart(), diag::warn_ret_stack_addr)
       << DR->getDecl()->getDeclName() << RetValExp->getSourceRange();
    
    // Skip over implicit cast expressions when checking for block expressions.
    if (ImplicitCastExpr *IcExpr = 
          dyn_cast_or_null<ImplicitCastExpr>(RetValExp))
      RetValExp = IcExpr->getSubExpr();

    if (BlockExpr *C = dyn_cast_or_null<BlockExpr>(RetValExp))
      if (C->hasBlockDeclRefExprs())
        Diag(C->getLocStart(), diag::err_ret_local_block)
          << C->getSourceRange();
  }
  // Perform checking for stack values returned by reference.
  else if (lhsType->isReferenceType()) {
    // Check for a reference to the stack
    if (DeclRefExpr *DR = EvalVal(RetValExp))
      Diag(DR->getLocStart(), diag::warn_ret_stack_ref)
        << DR->getDecl()->getDeclName() << RetValExp->getSourceRange();
  }
}

/// EvalAddr - EvalAddr and EvalVal are mutually recursive functions that
///  check if the expression in a return statement evaluates to an address
///  to a location on the stack.  The recursion is used to traverse the
///  AST of the return expression, with recursion backtracking when we
///  encounter a subexpression that (1) clearly does not lead to the address
///  of a stack variable or (2) is something we cannot determine leads to
///  the address of a stack variable based on such local checking.
///
///  EvalAddr processes expressions that are pointers that are used as
///  references (and not L-values).  EvalVal handles all other values.
///  At the base case of the recursion is a check for a DeclRefExpr* in 
///  the refers to a stack variable.
///
///  This implementation handles:
///
///   * pointer-to-pointer casts
///   * implicit conversions from array references to pointers
///   * taking the address of fields
///   * arbitrary interplay between "&" and "*" operators
///   * pointer arithmetic from an address of a stack variable
///   * taking the address of an array element where the array is on the stack
static DeclRefExpr* EvalAddr(Expr *E) {
  // We should only be called for evaluating pointer expressions.
  assert((E->getType()->isPointerType() ||
          E->getType()->isBlockPointerType() ||
          E->getType()->isObjCQualifiedIdType()) &&
         "EvalAddr only works on pointers");
    
  // Our "symbolic interpreter" is just a dispatch off the currently
  // viewed AST node.  We then recursively traverse the AST by calling
  // EvalAddr and EvalVal appropriately.
  switch (E->getStmtClass()) {
  case Stmt::ParenExprClass:
    // Ignore parentheses.
    return EvalAddr(cast<ParenExpr>(E)->getSubExpr());

  case Stmt::UnaryOperatorClass: {
    // The only unary operator that make sense to handle here
    // is AddrOf.  All others don't make sense as pointers.
    UnaryOperator *U = cast<UnaryOperator>(E);
    
    if (U->getOpcode() == UnaryOperator::AddrOf)
      return EvalVal(U->getSubExpr());
    else
      return NULL;
  }
  
  case Stmt::BinaryOperatorClass: {
    // Handle pointer arithmetic.  All other binary operators are not valid
    // in this context.
    BinaryOperator *B = cast<BinaryOperator>(E);
    BinaryOperator::Opcode op = B->getOpcode();
      
    if (op != BinaryOperator::Add && op != BinaryOperator::Sub)
      return NULL;
      
    Expr *Base = B->getLHS();

    // Determine which argument is the real pointer base.  It could be
    // the RHS argument instead of the LHS.
    if (!Base->getType()->isPointerType()) Base = B->getRHS();
      
    assert (Base->getType()->isPointerType());
    return EvalAddr(Base);
  }

  // For conditional operators we need to see if either the LHS or RHS are
  // valid DeclRefExpr*s.  If one of them is valid, we return it.
  case Stmt::ConditionalOperatorClass: {
    ConditionalOperator *C = cast<ConditionalOperator>(E);
    
    // Handle the GNU extension for missing LHS.
    if (Expr *lhsExpr = C->getLHS())
      if (DeclRefExpr* LHS = EvalAddr(lhsExpr))
        return LHS;

     return EvalAddr(C->getRHS());
  }
    
  // For casts, we need to handle conversions from arrays to
  // pointer values, and pointer-to-pointer conversions.
  case Stmt::ImplicitCastExprClass:
  case Stmt::CStyleCastExprClass:
  case Stmt::CXXFunctionalCastExprClass: {
    Expr* SubExpr = cast<CastExpr>(E)->getSubExpr();
    QualType T = SubExpr->getType();
    
    if (SubExpr->getType()->isPointerType() ||
        SubExpr->getType()->isBlockPointerType() ||
        SubExpr->getType()->isObjCQualifiedIdType())
      return EvalAddr(SubExpr);
    else if (T->isArrayType())
      return EvalVal(SubExpr);
    else
      return 0;
  }
    
  // C++ casts.  For dynamic casts, static casts, and const casts, we
  // are always converting from a pointer-to-pointer, so we just blow
  // through the cast.  In the case the dynamic cast doesn't fail (and
  // return NULL), we take the conservative route and report cases
  // where we return the address of a stack variable.  For Reinterpre
  // FIXME: The comment about is wrong; we're not always converting
  // from pointer to pointer. I'm guessing that this code should also
  // handle references to objects.  
  case Stmt::CXXStaticCastExprClass: 
  case Stmt::CXXDynamicCastExprClass: 
  case Stmt::CXXConstCastExprClass:
  case Stmt::CXXReinterpretCastExprClass: {
      Expr *S = cast<CXXNamedCastExpr>(E)->getSubExpr();
      if (S->getType()->isPointerType() || S->getType()->isBlockPointerType())
        return EvalAddr(S);
      else
        return NULL;
  }
    
  // Everything else: we simply don't reason about them.
  default:
    return NULL;
  }
}
  

///  EvalVal - This function is complements EvalAddr in the mutual recursion.
///   See the comments for EvalAddr for more details.
static DeclRefExpr* EvalVal(Expr *E) {
  
  // We should only be called for evaluating non-pointer expressions, or
  // expressions with a pointer type that are not used as references but instead
  // are l-values (e.g., DeclRefExpr with a pointer type).
    
  // Our "symbolic interpreter" is just a dispatch off the currently
  // viewed AST node.  We then recursively traverse the AST by calling
  // EvalAddr and EvalVal appropriately.
  switch (E->getStmtClass()) {
  case Stmt::DeclRefExprClass: 
  case Stmt::QualifiedDeclRefExprClass: {
    // DeclRefExpr: the base case.  When we hit a DeclRefExpr we are looking
    //  at code that refers to a variable's name.  We check if it has local
    //  storage within the function, and if so, return the expression.
    DeclRefExpr *DR = cast<DeclRefExpr>(E);
      
    if (VarDecl *V = dyn_cast<VarDecl>(DR->getDecl()))
      if(V->hasLocalStorage() && !V->getType()->isReferenceType()) return DR;
      
    return NULL;
  }
        
  case Stmt::ParenExprClass:
    // Ignore parentheses.
    return EvalVal(cast<ParenExpr>(E)->getSubExpr());
  
  case Stmt::UnaryOperatorClass: {
    // The only unary operator that make sense to handle here
    // is Deref.  All others don't resolve to a "name."  This includes
    // handling all sorts of rvalues passed to a unary operator.
    UnaryOperator *U = cast<UnaryOperator>(E);
              
    if (U->getOpcode() == UnaryOperator::Deref)
      return EvalAddr(U->getSubExpr());

    return NULL;
  }
  
  case Stmt::ArraySubscriptExprClass: {
    // Array subscripts are potential references to data on the stack.  We
    // retrieve the DeclRefExpr* for the array variable if it indeed
    // has local storage.
    return EvalAddr(cast<ArraySubscriptExpr>(E)->getBase());
  }
    
  case Stmt::ConditionalOperatorClass: {
    // For conditional operators we need to see if either the LHS or RHS are
    // non-NULL DeclRefExpr's.  If one is non-NULL, we return it.
    ConditionalOperator *C = cast<ConditionalOperator>(E);

    // Handle the GNU extension for missing LHS.
    if (Expr *lhsExpr = C->getLHS())
      if (DeclRefExpr *LHS = EvalVal(lhsExpr))
        return LHS;

    return EvalVal(C->getRHS());
  }
  
  // Accesses to members are potential references to data on the stack.
  case Stmt::MemberExprClass: {
    MemberExpr *M = cast<MemberExpr>(E);
      
    // Check for indirect access.  We only want direct field accesses.
    if (!M->isArrow())
      return EvalVal(M->getBase());
    else
      return NULL;
  }
    
  // Everything else: we simply don't reason about them.
  default:
    return NULL;
  }
}

//===--- CHECK: Floating-Point comparisons (-Wfloat-equal) ---------------===//

/// Check for comparisons of floating point operands using != and ==.
/// Issue a warning if these are no self-comparisons, as they are not likely
/// to do what the programmer intended.
void Sema::CheckFloatComparison(SourceLocation loc, Expr* lex, Expr *rex) {
  bool EmitWarning = true;
  
  Expr* LeftExprSansParen = lex->IgnoreParens();
  Expr* RightExprSansParen = rex->IgnoreParens();

  // Special case: check for x == x (which is OK).
  // Do not emit warnings for such cases.
  if (DeclRefExpr* DRL = dyn_cast<DeclRefExpr>(LeftExprSansParen))
    if (DeclRefExpr* DRR = dyn_cast<DeclRefExpr>(RightExprSansParen))
      if (DRL->getDecl() == DRR->getDecl())
        EmitWarning = false;
  
  
  // Special case: check for comparisons against literals that can be exactly
  //  represented by APFloat.  In such cases, do not emit a warning.  This
  //  is a heuristic: often comparison against such literals are used to
  //  detect if a value in a variable has not changed.  This clearly can
  //  lead to false negatives.
  if (EmitWarning) {
    if (FloatingLiteral* FLL = dyn_cast<FloatingLiteral>(LeftExprSansParen)) {
      if (FLL->isExact())
        EmitWarning = false;
    }
    else
      if (FloatingLiteral* FLR = dyn_cast<FloatingLiteral>(RightExprSansParen)){
        if (FLR->isExact())
          EmitWarning = false;
    }
  }
  
  // Check for comparisons with builtin types.
  if (EmitWarning)
    if (CallExpr* CL = dyn_cast<CallExpr>(LeftExprSansParen))
      if (CL->isBuiltinCall(Context))
        EmitWarning = false;
  
  if (EmitWarning)
    if (CallExpr* CR = dyn_cast<CallExpr>(RightExprSansParen))
      if (CR->isBuiltinCall(Context))
        EmitWarning = false;
  
  // Emit the diagnostic.
  if (EmitWarning)
    Diag(loc, diag::warn_floatingpoint_eq)
      << lex->getSourceRange() << rex->getSourceRange();
}
