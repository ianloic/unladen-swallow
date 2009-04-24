//===--- Expr.cpp - Expression AST Node Implementation --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Expr class and subclasses.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Expr.h"
#include "clang/AST/APValue.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/TargetInfo.h"
using namespace clang;

//===----------------------------------------------------------------------===//
// Primary Expressions.
//===----------------------------------------------------------------------===//

/// getValueAsApproximateDouble - This returns the value as an inaccurate
/// double.  Note that this may cause loss of precision, but is useful for
/// debugging dumps, etc.
double FloatingLiteral::getValueAsApproximateDouble() const {
  llvm::APFloat V = getValue();
  bool ignored;
  V.convert(llvm::APFloat::IEEEdouble, llvm::APFloat::rmNearestTiesToEven,
            &ignored);
  return V.convertToDouble();
}


StringLiteral::StringLiteral(const char *strData, unsigned byteLength, 
                             bool Wide, QualType t, SourceLocation firstLoc,
                             SourceLocation lastLoc) : 
  Expr(StringLiteralClass, t) {
  // OPTIMIZE: could allocate this appended to the StringLiteral.
  char *AStrData = new char[byteLength];
  memcpy(AStrData, strData, byteLength);
  StrData = AStrData;
  ByteLength = byteLength;
  IsWide = Wide;
  firstTokLoc = firstLoc;
  lastTokLoc = lastLoc;
}

StringLiteral::~StringLiteral() {
  delete[] StrData;
}

bool UnaryOperator::isPostfix(Opcode Op) {
  switch (Op) {
  case PostInc:
  case PostDec:
    return true;
  default:
    return false;
  }
}

bool UnaryOperator::isPrefix(Opcode Op) {
  switch (Op) {
    case PreInc:
    case PreDec:
      return true;
    default:
      return false;
  }
}

/// getOpcodeStr - Turn an Opcode enum value into the punctuation char it
/// corresponds to, e.g. "sizeof" or "[pre]++".
const char *UnaryOperator::getOpcodeStr(Opcode Op) {
  switch (Op) {
  default: assert(0 && "Unknown unary operator");
  case PostInc: return "++";
  case PostDec: return "--";
  case PreInc:  return "++";
  case PreDec:  return "--";
  case AddrOf:  return "&";
  case Deref:   return "*";
  case Plus:    return "+";
  case Minus:   return "-";
  case Not:     return "~";
  case LNot:    return "!";
  case Real:    return "__real";
  case Imag:    return "__imag";
  case Extension: return "__extension__";
  case OffsetOf: return "__builtin_offsetof";
  }
}

//===----------------------------------------------------------------------===//
// Postfix Operators.
//===----------------------------------------------------------------------===//

CallExpr::CallExpr(StmtClass SC, Expr *fn, Expr **args, unsigned numargs, 
                   QualType t, SourceLocation rparenloc)
  : Expr(SC, t, 
         fn->isTypeDependent() || hasAnyTypeDependentArguments(args, numargs),
         fn->isValueDependent() || hasAnyValueDependentArguments(args, numargs)),
    NumArgs(numargs) {
  SubExprs = new Stmt*[numargs+1];
  SubExprs[FN] = fn;
  for (unsigned i = 0; i != numargs; ++i)
    SubExprs[i+ARGS_START] = args[i];
  RParenLoc = rparenloc;
}

CallExpr::CallExpr(Expr *fn, Expr **args, unsigned numargs, QualType t,
                   SourceLocation rparenloc)
  : Expr(CallExprClass, t,
         fn->isTypeDependent() || hasAnyTypeDependentArguments(args, numargs),
         fn->isValueDependent() || hasAnyValueDependentArguments(args, numargs)),
    NumArgs(numargs) {
  SubExprs = new Stmt*[numargs+1];
  SubExprs[FN] = fn;
  for (unsigned i = 0; i != numargs; ++i)
    SubExprs[i+ARGS_START] = args[i];
  RParenLoc = rparenloc;
}

/// setNumArgs - This changes the number of arguments present in this call.
/// Any orphaned expressions are deleted by this, and any new operands are set
/// to null.
void CallExpr::setNumArgs(unsigned NumArgs) {
  // No change, just return.
  if (NumArgs == getNumArgs()) return;
  
  // If shrinking # arguments, just delete the extras and forgot them.
  if (NumArgs < getNumArgs()) {
    for (unsigned i = NumArgs, e = getNumArgs(); i != e; ++i)
      delete getArg(i);
    this->NumArgs = NumArgs;
    return;
  }

  // Otherwise, we are growing the # arguments.  New an bigger argument array.
  Stmt **NewSubExprs = new Stmt*[NumArgs+1];
  // Copy over args.
  for (unsigned i = 0; i != getNumArgs()+ARGS_START; ++i)
    NewSubExprs[i] = SubExprs[i];
  // Null out new args.
  for (unsigned i = getNumArgs()+ARGS_START; i != NumArgs+ARGS_START; ++i)
    NewSubExprs[i] = 0;
  
  delete[] SubExprs;
  SubExprs = NewSubExprs;
  this->NumArgs = NumArgs;
}

/// isBuiltinCall - If this is a call to a builtin, return the builtin ID.  If
/// not, return 0.
unsigned CallExpr::isBuiltinCall() const {
  // All simple function calls (e.g. func()) are implicitly cast to pointer to
  // function. As a result, we try and obtain the DeclRefExpr from the 
  // ImplicitCastExpr.
  const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(getCallee());
  if (!ICE) // FIXME: deal with more complex calls (e.g. (func)(), (*func)()).
    return 0;
  
  const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(ICE->getSubExpr());
  if (!DRE)
    return 0;
  
  const FunctionDecl *FDecl = dyn_cast<FunctionDecl>(DRE->getDecl());
  if (!FDecl)
    return 0;
  
  if (!FDecl->getIdentifier())
    return 0;

  return FDecl->getIdentifier()->getBuiltinID();
}


/// getOpcodeStr - Turn an Opcode enum value into the punctuation char it
/// corresponds to, e.g. "<<=".
const char *BinaryOperator::getOpcodeStr(Opcode Op) {
  switch (Op) {
  default: assert(0 && "Unknown binary operator");
  case Mul:       return "*";
  case Div:       return "/";
  case Rem:       return "%";
  case Add:       return "+";
  case Sub:       return "-";
  case Shl:       return "<<";
  case Shr:       return ">>";
  case LT:        return "<";
  case GT:        return ">";
  case LE:        return "<=";
  case GE:        return ">=";
  case EQ:        return "==";
  case NE:        return "!=";
  case And:       return "&";
  case Xor:       return "^";
  case Or:        return "|";
  case LAnd:      return "&&";
  case LOr:       return "||";
  case Assign:    return "=";
  case MulAssign: return "*=";
  case DivAssign: return "/=";
  case RemAssign: return "%=";
  case AddAssign: return "+=";
  case SubAssign: return "-=";
  case ShlAssign: return "<<=";
  case ShrAssign: return ">>=";
  case AndAssign: return "&=";
  case XorAssign: return "^=";
  case OrAssign:  return "|=";
  case Comma:     return ",";
  }
}

InitListExpr::InitListExpr(SourceLocation lbraceloc, 
                           Expr **initExprs, unsigned numInits,
                           SourceLocation rbraceloc)
  : Expr(InitListExprClass, QualType()),
    LBraceLoc(lbraceloc), RBraceLoc(rbraceloc), SyntacticForm(0), 
    UnionFieldInit(0), HadArrayRangeDesignator(false) {

  InitExprs.insert(InitExprs.end(), initExprs, initExprs+numInits);
}

void InitListExpr::resizeInits(ASTContext &Context, unsigned NumInits) {
  for (unsigned Idx = NumInits, LastIdx = InitExprs.size(); Idx < LastIdx; ++Idx)
    delete InitExprs[Idx];
  InitExprs.resize(NumInits, 0);
}

Expr *InitListExpr::updateInit(unsigned Init, Expr *expr) {
  if (Init >= InitExprs.size()) {
    InitExprs.insert(InitExprs.end(), Init - InitExprs.size() + 1, 0);
    InitExprs.back() = expr;
    return 0;
  }
  
  Expr *Result = cast_or_null<Expr>(InitExprs[Init]);
  InitExprs[Init] = expr;
  return Result;
}

/// getFunctionType - Return the underlying function type for this block.
///
const FunctionType *BlockExpr::getFunctionType() const {
  return getType()->getAsBlockPointerType()->
                    getPointeeType()->getAsFunctionType();
}

SourceLocation BlockExpr::getCaretLocation() const { 
  return TheBlock->getCaretLocation(); 
}
const Stmt *BlockExpr::getBody() const { return TheBlock->getBody(); }
Stmt *BlockExpr::getBody() { return TheBlock->getBody(); }


//===----------------------------------------------------------------------===//
// Generic Expression Routines
//===----------------------------------------------------------------------===//

/// hasLocalSideEffect - Return true if this immediate expression has side
/// effects, not counting any sub-expressions.
bool Expr::hasLocalSideEffect() const {
  switch (getStmtClass()) {
  default:
    return false;
  case ParenExprClass:
    return cast<ParenExpr>(this)->getSubExpr()->hasLocalSideEffect();
  case UnaryOperatorClass: {
    const UnaryOperator *UO = cast<UnaryOperator>(this);
    
    switch (UO->getOpcode()) {
    default: return false;
    case UnaryOperator::PostInc:
    case UnaryOperator::PostDec:
    case UnaryOperator::PreInc:
    case UnaryOperator::PreDec:
      return true;                     // ++/--

    case UnaryOperator::Deref:
      // Dereferencing a volatile pointer is a side-effect.
      return getType().isVolatileQualified();
    case UnaryOperator::Real:
    case UnaryOperator::Imag:
      // accessing a piece of a volatile complex is a side-effect.
      return UO->getSubExpr()->getType().isVolatileQualified();

    case UnaryOperator::Extension:
      return UO->getSubExpr()->hasLocalSideEffect();
    }
  }
  case BinaryOperatorClass: {
    const BinaryOperator *BinOp = cast<BinaryOperator>(this);
    // Consider comma to have side effects if the LHS and RHS both do.
    if (BinOp->getOpcode() == BinaryOperator::Comma)
      return BinOp->getLHS()->hasLocalSideEffect() &&
             BinOp->getRHS()->hasLocalSideEffect();
      
    return BinOp->isAssignmentOp();
  }
  case CompoundAssignOperatorClass:
    return true;

  case ConditionalOperatorClass: {
    const ConditionalOperator *Exp = cast<ConditionalOperator>(this);
    return Exp->getCond()->hasLocalSideEffect()
           || (Exp->getLHS() && Exp->getLHS()->hasLocalSideEffect())
           || (Exp->getRHS() && Exp->getRHS()->hasLocalSideEffect());
  }

  case MemberExprClass:
  case ArraySubscriptExprClass:
    // If the base pointer or element is to a volatile pointer/field, accessing
    // if is a side effect.
    return getType().isVolatileQualified();

  case CallExprClass:
  case CXXOperatorCallExprClass:
    // TODO: check attributes for pure/const.   "void foo() { strlen("bar"); }"
    // should warn.
    return true;
  case ObjCMessageExprClass:
    return true;
  case StmtExprClass: {
    // Statement exprs don't logically have side effects themselves, but are
    // sometimes used in macros in ways that give them a type that is unused.
    // For example ({ blah; foo(); }) will end up with a type if foo has a type.
    // however, if the result of the stmt expr is dead, we don't want to emit a
    // warning.
    const CompoundStmt *CS = cast<StmtExpr>(this)->getSubStmt();
    if (!CS->body_empty())
      if (const Expr *E = dyn_cast<Expr>(CS->body_back()))
        return E->hasLocalSideEffect();
    return false;
  }
  case CStyleCastExprClass:
  case CXXFunctionalCastExprClass:
    // If this is a cast to void, check the operand.  Otherwise, the result of
    // the cast is unused.
    if (getType()->isVoidType())
      return cast<CastExpr>(this)->getSubExpr()->hasLocalSideEffect();
    return false;

  case ImplicitCastExprClass:
    // Check the operand, since implicit casts are inserted by Sema
    return cast<ImplicitCastExpr>(this)->getSubExpr()->hasLocalSideEffect();

  case CXXDefaultArgExprClass:
    return cast<CXXDefaultArgExpr>(this)->getExpr()->hasLocalSideEffect();

  case CXXNewExprClass:
    // FIXME: In theory, there might be new expressions that don't have side
    // effects (e.g. a placement new with an uninitialized POD).
  case CXXDeleteExprClass:
    return true;
  }
}

/// DeclCanBeLvalue - Determine whether the given declaration can be
/// an lvalue. This is a helper routine for isLvalue.
static bool DeclCanBeLvalue(const NamedDecl *Decl, ASTContext &Ctx) {
  // C++ [temp.param]p6:
  //   A non-type non-reference template-parameter is not an lvalue.
  if (const NonTypeTemplateParmDecl *NTTParm 
        = dyn_cast<NonTypeTemplateParmDecl>(Decl))
    return NTTParm->getType()->isReferenceType();

  return isa<VarDecl>(Decl) || isa<FieldDecl>(Decl) ||
    // C++ 3.10p2: An lvalue refers to an object or function.
    (Ctx.getLangOptions().CPlusPlus &&
     (isa<FunctionDecl>(Decl) || isa<OverloadedFunctionDecl>(Decl)));
}

/// isLvalue - C99 6.3.2.1: an lvalue is an expression with an object type or an
/// incomplete type other than void. Nonarray expressions that can be lvalues:
///  - name, where name must be a variable
///  - e[i]
///  - (e), where e must be an lvalue
///  - e.name, where e must be an lvalue
///  - e->name
///  - *e, the type of e cannot be a function type
///  - string-constant
///  - (__real__ e) and (__imag__ e) where e is an lvalue  [GNU extension]
///  - reference type [C++ [expr]]
///
Expr::isLvalueResult Expr::isLvalue(ASTContext &Ctx) const {
  // first, check the type (C99 6.3.2.1). Expressions with function
  // type in C are not lvalues, but they can be lvalues in C++.
  if (!Ctx.getLangOptions().CPlusPlus && TR->isFunctionType())
    return LV_NotObjectType;

  // Allow qualified void which is an incomplete type other than void (yuck).
  if (TR->isVoidType() && !Ctx.getCanonicalType(TR).getCVRQualifiers())
    return LV_IncompleteVoidType;

  /// FIXME: Expressions can't have reference type, so the following
  /// isn't needed.
  if (TR->isReferenceType()) // C++ [expr]
    return LV_Valid;

  // the type looks fine, now check the expression
  switch (getStmtClass()) {
  case StringLiteralClass: // C99 6.5.1p4
    return LV_Valid;
  case ArraySubscriptExprClass: // C99 6.5.3p4 (e1[e2] == (*((e1)+(e2))))
    // For vectors, make sure base is an lvalue (i.e. not a function call).
    if (cast<ArraySubscriptExpr>(this)->getBase()->getType()->isVectorType())
      return cast<ArraySubscriptExpr>(this)->getBase()->isLvalue(Ctx);
    return LV_Valid;
  case DeclRefExprClass: 
  case QualifiedDeclRefExprClass: { // C99 6.5.1p2
    const NamedDecl *RefdDecl = cast<DeclRefExpr>(this)->getDecl();
    if (DeclCanBeLvalue(RefdDecl, Ctx))
      return LV_Valid;
    break;
  }
  case BlockDeclRefExprClass: {
    const BlockDeclRefExpr *BDR = cast<BlockDeclRefExpr>(this);
    if (isa<VarDecl>(BDR->getDecl()))
      return LV_Valid;
    break;
  }
  case MemberExprClass: { 
    const MemberExpr *m = cast<MemberExpr>(this);
    if (Ctx.getLangOptions().CPlusPlus) { // C++ [expr.ref]p4:
      NamedDecl *Member = m->getMemberDecl();
      // C++ [expr.ref]p4:
      //   If E2 is declared to have type "reference to T", then E1.E2
      //   is an lvalue.
      if (ValueDecl *Value = dyn_cast<ValueDecl>(Member))
        if (Value->getType()->isReferenceType())
          return LV_Valid;

      //   -- If E2 is a static data member [...] then E1.E2 is an lvalue.
      if (isa<CXXClassVarDecl>(Member))
        return LV_Valid;

      //   -- If E2 is a non-static data member [...]. If E1 is an
      //      lvalue, then E1.E2 is an lvalue.
      if (isa<FieldDecl>(Member))
        return m->isArrow() ? LV_Valid : m->getBase()->isLvalue(Ctx);

      //   -- If it refers to a static member function [...], then
      //      E1.E2 is an lvalue.
      //   -- Otherwise, if E1.E2 refers to a non-static member
      //      function [...], then E1.E2 is not an lvalue.
      if (CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(Member))
        return Method->isStatic()? LV_Valid : LV_MemberFunction;

      //   -- If E2 is a member enumerator [...], the expression E1.E2
      //      is not an lvalue.
      if (isa<EnumConstantDecl>(Member))
        return LV_InvalidExpression;

        // Not an lvalue.
      return LV_InvalidExpression;
    } 

    // C99 6.5.2.3p4
    return m->isArrow() ? LV_Valid : m->getBase()->isLvalue(Ctx);
  }
  case UnaryOperatorClass:
    if (cast<UnaryOperator>(this)->getOpcode() == UnaryOperator::Deref)
      return LV_Valid; // C99 6.5.3p4

    if (cast<UnaryOperator>(this)->getOpcode() == UnaryOperator::Real ||
        cast<UnaryOperator>(this)->getOpcode() == UnaryOperator::Imag ||
        cast<UnaryOperator>(this)->getOpcode() == UnaryOperator::Extension)
      return cast<UnaryOperator>(this)->getSubExpr()->isLvalue(Ctx);  // GNU.

    if (Ctx.getLangOptions().CPlusPlus && // C++ [expr.pre.incr]p1
        (cast<UnaryOperator>(this)->getOpcode() == UnaryOperator::PreInc ||
         cast<UnaryOperator>(this)->getOpcode() == UnaryOperator::PreDec))
      return LV_Valid;
    break;
  case ImplicitCastExprClass:
    return cast<ImplicitCastExpr>(this)->isLvalueCast()? LV_Valid 
                                                       : LV_InvalidExpression;
  case ParenExprClass: // C99 6.5.1p5
    return cast<ParenExpr>(this)->getSubExpr()->isLvalue(Ctx);
  case BinaryOperatorClass:
  case CompoundAssignOperatorClass: {
    const BinaryOperator *BinOp = cast<BinaryOperator>(this);

    if (Ctx.getLangOptions().CPlusPlus && // C++ [expr.comma]p1
        BinOp->getOpcode() == BinaryOperator::Comma)
      return BinOp->getRHS()->isLvalue(Ctx);

    if (!BinOp->isAssignmentOp())
      return LV_InvalidExpression;

    if (Ctx.getLangOptions().CPlusPlus)
      // C++ [expr.ass]p1: 
      //   The result of an assignment operation [...] is an lvalue.
      return LV_Valid;


    // C99 6.5.16:
    //   An assignment expression [...] is not an lvalue.
    return LV_InvalidExpression;
  }
  // FIXME: OverloadExprClass
  case CallExprClass: 
  case CXXOperatorCallExprClass:
  case CXXMemberCallExprClass: {
    // C++ [expr.call]p10:
    //   A function call is an lvalue if and only if the result type
    //   is a reference.
    QualType CalleeType = cast<CallExpr>(this)->getCallee()->getType();
    if (const PointerType *FnTypePtr = CalleeType->getAsPointerType())
      CalleeType = FnTypePtr->getPointeeType();
    if (const FunctionType *FnType = CalleeType->getAsFunctionType())
      if (FnType->getResultType()->isReferenceType())
        return LV_Valid;
    
    break;
  }
  case CompoundLiteralExprClass: // C99 6.5.2.5p5
    return LV_Valid;
  case ChooseExprClass:
    // __builtin_choose_expr is an lvalue if the selected operand is.
    if (cast<ChooseExpr>(this)->isConditionTrue(Ctx))
      return cast<ChooseExpr>(this)->getLHS()->isLvalue(Ctx);
    else
      return cast<ChooseExpr>(this)->getRHS()->isLvalue(Ctx);

  case ExtVectorElementExprClass:
    if (cast<ExtVectorElementExpr>(this)->containsDuplicateElements())
      return LV_DuplicateVectorComponents;
    return LV_Valid;
  case ObjCIvarRefExprClass: // ObjC instance variables are lvalues.
    return LV_Valid;
  case ObjCPropertyRefExprClass: // FIXME: check if read-only property.
    return LV_Valid;
  case ObjCKVCRefExprClass: // FIXME: check if read-only property.
    return LV_Valid;
  case PredefinedExprClass:
    return LV_Valid;
  case VAArgExprClass:
    return LV_Valid;
  case CXXDefaultArgExprClass:
    return cast<CXXDefaultArgExpr>(this)->getExpr()->isLvalue(Ctx);
  case CXXConditionDeclExprClass:
    return LV_Valid;
  case CStyleCastExprClass:
  case CXXFunctionalCastExprClass:
  case CXXStaticCastExprClass:
  case CXXDynamicCastExprClass:
  case CXXReinterpretCastExprClass:
  case CXXConstCastExprClass:
    // The result of an explicit cast is an lvalue if the type we are
    // casting to is a reference type. See C++ [expr.cast]p1, 
    // C++ [expr.static.cast]p2, C++ [expr.dynamic.cast]p2,
    // C++ [expr.reinterpret.cast]p1, C++ [expr.const.cast]p1.
    if (cast<ExplicitCastExpr>(this)->getTypeAsWritten()->isReferenceType())
      return LV_Valid;
    break;
  case CXXTypeidExprClass:
    // C++ 5.2.8p1: The result of a typeid expression is an lvalue of ...
    return LV_Valid;
  default:
    break;
  }
  return LV_InvalidExpression;
}

/// isModifiableLvalue - C99 6.3.2.1: an lvalue that does not have array type,
/// does not have an incomplete type, does not have a const-qualified type, and
/// if it is a structure or union, does not have any member (including, 
/// recursively, any member or element of all contained aggregates or unions)
/// with a const-qualified type.
Expr::isModifiableLvalueResult Expr::isModifiableLvalue(ASTContext &Ctx) const {
  isLvalueResult lvalResult = isLvalue(Ctx);
    
  switch (lvalResult) {
  case LV_Valid: 
    // C++ 3.10p11: Functions cannot be modified, but pointers to
    // functions can be modifiable.
    if (Ctx.getLangOptions().CPlusPlus && TR->isFunctionType())
      return MLV_NotObjectType;
    break;

  case LV_NotObjectType: return MLV_NotObjectType;
  case LV_IncompleteVoidType: return MLV_IncompleteVoidType;
  case LV_DuplicateVectorComponents: return MLV_DuplicateVectorComponents;
  case LV_InvalidExpression:
    // If the top level is a C-style cast, and the subexpression is a valid
    // lvalue, then this is probably a use of the old-school "cast as lvalue"
    // GCC extension.  We don't support it, but we want to produce good
    // diagnostics when it happens so that the user knows why.
    if (const CStyleCastExpr *CE = dyn_cast<CStyleCastExpr>(this))
      if (CE->getSubExpr()->isLvalue(Ctx) == LV_Valid)
        return MLV_LValueCast;
    return MLV_InvalidExpression;
  case LV_MemberFunction: return MLV_MemberFunction;
  }
  
  QualType CT = Ctx.getCanonicalType(getType());
  
  if (CT.isConstQualified())
    return MLV_ConstQualified;
  if (CT->isArrayType())
    return MLV_ArrayType;
  if (CT->isIncompleteType())
    return MLV_IncompleteType;
    
  if (const RecordType *r = CT->getAsRecordType()) {
    if (r->hasConstFields()) 
      return MLV_ConstQualified;
  }
  // The following is illegal:
  //   void takeclosure(void (^C)(void));
  //   void func() { int x = 1; takeclosure(^{ x = 7 }); }
  //
  if (getStmtClass() == BlockDeclRefExprClass) {
    const BlockDeclRefExpr *BDR = cast<BlockDeclRefExpr>(this);
    if (!BDR->isByRef() && isa<VarDecl>(BDR->getDecl()))
      return MLV_NotBlockQualified;
  }
  
  // Assigning to an 'implicit' property?
  else if (getStmtClass() == ObjCKVCRefExprClass) {
    const ObjCKVCRefExpr* KVCExpr = cast<ObjCKVCRefExpr>(this);
    if (KVCExpr->getSetterMethod() == 0)
      return MLV_NoSetterProperty;
  }
  return MLV_Valid;    
}

/// hasGlobalStorage - Return true if this expression has static storage
/// duration.  This means that the address of this expression is a link-time
/// constant.
bool Expr::hasGlobalStorage() const {
  switch (getStmtClass()) {
  default:
    return false;
  case ParenExprClass:
    return cast<ParenExpr>(this)->getSubExpr()->hasGlobalStorage();
  case ImplicitCastExprClass:
    return cast<ImplicitCastExpr>(this)->getSubExpr()->hasGlobalStorage();
  case CompoundLiteralExprClass:
    return cast<CompoundLiteralExpr>(this)->isFileScope();
  case DeclRefExprClass:
  case QualifiedDeclRefExprClass: {
    const Decl *D = cast<DeclRefExpr>(this)->getDecl();
    if (const VarDecl *VD = dyn_cast<VarDecl>(D))
      return VD->hasGlobalStorage();
    if (isa<FunctionDecl>(D))
      return true;
    return false;
  }
  case MemberExprClass: {
    const MemberExpr *M = cast<MemberExpr>(this);
    return !M->isArrow() && M->getBase()->hasGlobalStorage();
  }
  case ArraySubscriptExprClass:
    return cast<ArraySubscriptExpr>(this)->getBase()->hasGlobalStorage();
  case PredefinedExprClass:
    return true;
  case CXXDefaultArgExprClass:
    return cast<CXXDefaultArgExpr>(this)->getExpr()->hasGlobalStorage();
  }
}

Expr* Expr::IgnoreParens() {
  Expr* E = this;
  while (ParenExpr* P = dyn_cast<ParenExpr>(E))
    E = P->getSubExpr();
  
  return E;
}

/// IgnoreParenCasts - Ignore parentheses and casts.  Strip off any ParenExpr
/// or CastExprs or ImplicitCastExprs, returning their operand.
Expr *Expr::IgnoreParenCasts() {
  Expr *E = this;
  while (true) {
    if (ParenExpr *P = dyn_cast<ParenExpr>(E))
      E = P->getSubExpr();
    else if (CastExpr *P = dyn_cast<CastExpr>(E))
      E = P->getSubExpr();
    else
      return E;
  }
}

/// hasAnyTypeDependentArguments - Determines if any of the expressions
/// in Exprs is type-dependent.
bool Expr::hasAnyTypeDependentArguments(Expr** Exprs, unsigned NumExprs) {
  for (unsigned I = 0; I < NumExprs; ++I)
    if (Exprs[I]->isTypeDependent())
      return true;

  return false;
}

/// hasAnyValueDependentArguments - Determines if any of the expressions
/// in Exprs is value-dependent.
bool Expr::hasAnyValueDependentArguments(Expr** Exprs, unsigned NumExprs) {
  for (unsigned I = 0; I < NumExprs; ++I)
    if (Exprs[I]->isValueDependent())
      return true;

  return false;
}

bool Expr::isConstantInitializer(ASTContext &Ctx) const {
  // This function is attempting whether an expression is an initializer
  // which can be evaluated at compile-time.  isEvaluatable handles most
  // of the cases, but it can't deal with some initializer-specific
  // expressions, and it can't deal with aggregates; we deal with those here,
  // and fall back to isEvaluatable for the other cases.

  switch (getStmtClass()) {
  default: break;
  case StringLiteralClass:
    return true;
  case CompoundLiteralExprClass: {
    const Expr *Exp = cast<CompoundLiteralExpr>(this)->getInitializer();
    return Exp->isConstantInitializer(Ctx);
  }
  case InitListExprClass: {
    const InitListExpr *Exp = cast<InitListExpr>(this);
    unsigned numInits = Exp->getNumInits();
    for (unsigned i = 0; i < numInits; i++) {
      if (!Exp->getInit(i)->isConstantInitializer(Ctx)) 
        return false;
    }
    return true;
  }
  case ImplicitValueInitExprClass:
    return true;
  case ParenExprClass: {
    return cast<ParenExpr>(this)->getSubExpr()->isConstantInitializer(Ctx);
  }
  case UnaryOperatorClass: {
    const UnaryOperator* Exp = cast<UnaryOperator>(this);
    if (Exp->getOpcode() == UnaryOperator::Extension)
      return Exp->getSubExpr()->isConstantInitializer(Ctx);
    break;
  }
  case CStyleCastExprClass:
    // Handle casts with a destination that's a struct or union; this
    // deals with both the gcc no-op struct cast extension and the
    // cast-to-union extension.
    if (getType()->isRecordType())
      return cast<CastExpr>(this)->getSubExpr()->isConstantInitializer(Ctx);
    break;
  case DesignatedInitExprClass:
    return cast<DesignatedInitExpr>(this)->
            getInit()->isConstantInitializer(Ctx);
  }

  return isEvaluatable(Ctx);
}

/// isIntegerConstantExpr - this recursive routine will test if an expression is
/// an integer constant expression. Note: With the introduction of VLA's in
/// C99 the result of the sizeof operator is no longer always a constant
/// expression. The generalization of the wording to include any subexpression
/// that is not evaluated (C99 6.6p3) means that nonconstant subexpressions
/// can appear as operands to other operators (e.g. &&, ||, ?:). For instance,
/// "0 || f()" can be treated as a constant expression. In C90 this expression,
/// occurring in a context requiring a constant, would have been a constraint
/// violation. FIXME: This routine currently implements C90 semantics.
/// To properly implement C99 semantics this routine will need to evaluate
/// expressions involving operators previously mentioned.

/// FIXME: Pass up a reason why! Invalid operation in i-c-e, division by zero,
/// comma, etc
///
/// FIXME: This should ext-warn on overflow during evaluation!  ISO C does not
/// permit this.  This includes things like (int)1e1000
///
/// FIXME: Handle offsetof.  Two things to do:  Handle GCC's __builtin_offsetof
/// to support gcc 4.0+  and handle the idiom GCC recognizes with a null pointer
/// cast+dereference.
bool Expr::isIntegerConstantExpr(llvm::APSInt &Result, ASTContext &Ctx,
                                 SourceLocation *Loc, bool isEvaluated) const {
  // Pretest for integral type; some parts of the code crash for types that
  // can't be sized.
  if (!getType()->isIntegralType()) {
    if (Loc) *Loc = getLocStart();
    return false;
  }
  switch (getStmtClass()) {
  default:
    if (Loc) *Loc = getLocStart();
    return false;
  case ParenExprClass:
    return cast<ParenExpr>(this)->getSubExpr()->
                     isIntegerConstantExpr(Result, Ctx, Loc, isEvaluated);
  case IntegerLiteralClass:
    Result = cast<IntegerLiteral>(this)->getValue();
    break;
  case CharacterLiteralClass: {
    const CharacterLiteral *CL = cast<CharacterLiteral>(this);
    Result.zextOrTrunc(static_cast<uint32_t>(Ctx.getTypeSize(getType())));
    Result = CL->getValue();
    Result.setIsUnsigned(!getType()->isSignedIntegerType());
    break;
  }
  case CXXBoolLiteralExprClass: {
    const CXXBoolLiteralExpr *BL = cast<CXXBoolLiteralExpr>(this);
    Result.zextOrTrunc(static_cast<uint32_t>(Ctx.getTypeSize(getType())));
    Result = BL->getValue();
    Result.setIsUnsigned(!getType()->isSignedIntegerType());
    break;
  }
  case CXXZeroInitValueExprClass:
    Result.clear();
    break;
  case TypesCompatibleExprClass: {
    const TypesCompatibleExpr *TCE = cast<TypesCompatibleExpr>(this);
    Result.zextOrTrunc(static_cast<uint32_t>(Ctx.getTypeSize(getType())));
    // Per gcc docs "this built-in function ignores top level
    // qualifiers".  We need to use the canonical version to properly
    // be able to strip CRV qualifiers from the type.
    QualType T0 = Ctx.getCanonicalType(TCE->getArgType1());
    QualType T1 = Ctx.getCanonicalType(TCE->getArgType2());
    Result = Ctx.typesAreCompatible(T0.getUnqualifiedType(), 
                                    T1.getUnqualifiedType());
    break;
  }
  case CallExprClass: 
  case CXXOperatorCallExprClass: {
    const CallExpr *CE = cast<CallExpr>(this);
    Result.zextOrTrunc(static_cast<uint32_t>(Ctx.getTypeSize(getType())));
    
    // If this is a call to a builtin function, constant fold it otherwise
    // reject it.
    if (CE->isBuiltinCall()) {
      EvalResult EvalResult;
      if (CE->Evaluate(EvalResult, Ctx)) {
        assert(!EvalResult.HasSideEffects && 
               "Foldable builtin call should not have side effects!");
        Result = EvalResult.Val.getInt();
        break;  // It is a constant, expand it.
      }
    }
    
    if (Loc) *Loc = getLocStart();
    return false;
  }
  case DeclRefExprClass:
  case QualifiedDeclRefExprClass:
    if (const EnumConstantDecl *D = 
          dyn_cast<EnumConstantDecl>(cast<DeclRefExpr>(this)->getDecl())) {
      Result = D->getInitVal();
      break;
    }
    if (Loc) *Loc = getLocStart();
    return false;
  case UnaryOperatorClass: {
    const UnaryOperator *Exp = cast<UnaryOperator>(this);
    
    // Get the operand value.  If this is offsetof, do not evalute the
    // operand.  This affects C99 6.6p3.
    if (!Exp->isOffsetOfOp() && !Exp->getSubExpr()->
                        isIntegerConstantExpr(Result, Ctx, Loc, isEvaluated))
      return false;

    switch (Exp->getOpcode()) {
    // Address, indirect, pre/post inc/dec, etc are not valid constant exprs.
    // See C99 6.6p3.
    default:
      if (Loc) *Loc = Exp->getOperatorLoc();
      return false;
    case UnaryOperator::Extension:
      return true;  // FIXME: this is wrong.
    case UnaryOperator::LNot: {
      bool Val = Result == 0;
      Result.zextOrTrunc(static_cast<uint32_t>(Ctx.getTypeSize(getType())));
      Result = Val;
      break;
    }
    case UnaryOperator::Plus:
      break;
    case UnaryOperator::Minus:
      Result = -Result;
      break;
    case UnaryOperator::Not:
      Result = ~Result;
      break;
    case UnaryOperator::OffsetOf:
      Result.zextOrTrunc(static_cast<uint32_t>(Ctx.getTypeSize(getType())));
      Result = Exp->evaluateOffsetOf(Ctx);
    }
    break;
  }
  case SizeOfAlignOfExprClass: {
    const SizeOfAlignOfExpr *Exp = cast<SizeOfAlignOfExpr>(this);
    
    // Return the result in the right width.
    Result.zextOrTrunc(static_cast<uint32_t>(Ctx.getTypeSize(getType())));
    
    QualType ArgTy = Exp->getTypeOfArgument();
    // sizeof(void) and __alignof__(void) = 1 as a gcc extension.
    if (ArgTy->isVoidType()) {
      Result = 1;
      break;
    }
    
    // alignof always evaluates to a constant, sizeof does if arg is not VLA.
    if (Exp->isSizeOf() && !ArgTy->isConstantSizeType()) {
      if (Loc) *Loc = Exp->getOperatorLoc();
      return false;
    }

    // Get information about the size or align.
    if (ArgTy->isFunctionType()) {
      // GCC extension: sizeof(function) = 1.
      Result = Exp->isSizeOf() ? 1 : 4;
    } else { 
      unsigned CharSize = Ctx.Target.getCharWidth();
      if (Exp->isSizeOf())
        Result = Ctx.getTypeSize(ArgTy) / CharSize;
      else
        Result = Ctx.getTypeAlign(ArgTy) / CharSize;
    }
    break;
  }
  case BinaryOperatorClass: {
    const BinaryOperator *Exp = cast<BinaryOperator>(this);
    llvm::APSInt LHS, RHS;

    // Initialize result to have correct signedness and width.
    Result = llvm::APSInt(static_cast<uint32_t>(Ctx.getTypeSize(getType())),
                          !getType()->isSignedIntegerType());                          
    
    // The LHS of a constant expr is always evaluated and needed.
    if (!Exp->getLHS()->isIntegerConstantExpr(LHS, Ctx, Loc, isEvaluated))
      return false;
    
    // The short-circuiting &&/|| operators don't necessarily evaluate their
    // RHS.  Make sure to pass isEvaluated down correctly.
    if (Exp->isLogicalOp()) {
      bool RHSEval;
      if (Exp->getOpcode() == BinaryOperator::LAnd)
        RHSEval = LHS != 0;
      else {
        assert(Exp->getOpcode() == BinaryOperator::LOr &&"Unexpected logical");
        RHSEval = LHS == 0;
      }
      
      if (!Exp->getRHS()->isIntegerConstantExpr(RHS, Ctx, Loc,
                                                isEvaluated & RHSEval))
        return false;
    } else {
      if (!Exp->getRHS()->isIntegerConstantExpr(RHS, Ctx, Loc, isEvaluated))
        return false;
    }
    
    switch (Exp->getOpcode()) {
    default:
      if (Loc) *Loc = getLocStart();
      return false;
    case BinaryOperator::Mul:
      Result = LHS * RHS;
      break;
    case BinaryOperator::Div:
      if (RHS == 0) {
        if (!isEvaluated) break;
        if (Loc) *Loc = getLocStart();
        return false;
      }
      Result = LHS / RHS;
      break;
    case BinaryOperator::Rem:
      if (RHS == 0) {
        if (!isEvaluated) break;
        if (Loc) *Loc = getLocStart();
        return false;
      }
      Result = LHS % RHS;
      break;
    case BinaryOperator::Add: Result = LHS + RHS; break;
    case BinaryOperator::Sub: Result = LHS - RHS; break;
    case BinaryOperator::Shl:
      Result = LHS << 
        static_cast<uint32_t>(RHS.getLimitedValue(LHS.getBitWidth()-1));
    break;
    case BinaryOperator::Shr:
      Result = LHS >>
        static_cast<uint32_t>(RHS.getLimitedValue(LHS.getBitWidth()-1));
      break;
    case BinaryOperator::LT:  Result = LHS < RHS; break;
    case BinaryOperator::GT:  Result = LHS > RHS; break;
    case BinaryOperator::LE:  Result = LHS <= RHS; break;
    case BinaryOperator::GE:  Result = LHS >= RHS; break;
    case BinaryOperator::EQ:  Result = LHS == RHS; break;
    case BinaryOperator::NE:  Result = LHS != RHS; break;
    case BinaryOperator::And: Result = LHS & RHS; break;
    case BinaryOperator::Xor: Result = LHS ^ RHS; break;
    case BinaryOperator::Or:  Result = LHS | RHS; break;
    case BinaryOperator::LAnd:
      Result = LHS != 0 && RHS != 0;
      break;
    case BinaryOperator::LOr:
      Result = LHS != 0 || RHS != 0;
      break;
      
    case BinaryOperator::Comma:
      // C99 6.6p3: "shall not contain assignment, ..., or comma operators,
      // *except* when they are contained within a subexpression that is not
      // evaluated".  Note that Assignment can never happen due to constraints
      // on the LHS subexpr, so we don't need to check it here.
      if (isEvaluated) {
        if (Loc) *Loc = getLocStart();
        return false;
      }
      
      // The result of the constant expr is the RHS.
      Result = RHS;
      return true;
    }
    
    assert(!Exp->isAssignmentOp() && "LHS can't be a constant expr!");
    break;
  }
  case ImplicitCastExprClass:
  case CStyleCastExprClass:
  case CXXFunctionalCastExprClass: {
    const Expr *SubExpr = cast<CastExpr>(this)->getSubExpr();
    SourceLocation CastLoc = getLocStart();
    
    // C99 6.6p6: shall only convert arithmetic types to integer types.
    if (!SubExpr->getType()->isArithmeticType() ||
        !getType()->isIntegerType()) {
      if (Loc) *Loc = SubExpr->getLocStart();
      return false;
    }

    uint32_t DestWidth = static_cast<uint32_t>(Ctx.getTypeSize(getType()));
    
    // Handle simple integer->integer casts.
    if (SubExpr->getType()->isIntegerType()) {
      if (!SubExpr->isIntegerConstantExpr(Result, Ctx, Loc, isEvaluated))
        return false;
      
      // Figure out if this is a truncate, extend or noop cast.
      // If the input is signed, do a sign extend, noop, or truncate.
      if (getType()->isBooleanType()) {
        // Conversion to bool compares against zero.
        Result = Result != 0;
        Result.zextOrTrunc(DestWidth);
      } else if (SubExpr->getType()->isSignedIntegerType())
        Result.sextOrTrunc(DestWidth);
      else  // If the input is unsigned, do a zero extend, noop, or truncate.
        Result.zextOrTrunc(DestWidth);
      break;
    }
    
    // Allow floating constants that are the immediate operands of casts or that
    // are parenthesized.
    const Expr *Operand = SubExpr;
    while (const ParenExpr *PE = dyn_cast<ParenExpr>(Operand))
      Operand = PE->getSubExpr();

    // If this isn't a floating literal, we can't handle it.
    const FloatingLiteral *FL = dyn_cast<FloatingLiteral>(Operand);
    if (!FL) {
      if (Loc) *Loc = Operand->getLocStart();
      return false;
    }

    // If the destination is boolean, compare against zero.
    if (getType()->isBooleanType()) {
      Result = !FL->getValue().isZero();
      Result.zextOrTrunc(DestWidth);
      break;
    }     
    
    // Determine whether we are converting to unsigned or signed.
    bool DestSigned = getType()->isSignedIntegerType();

    // TODO: Warn on overflow, but probably not here: isIntegerConstantExpr can
    // be called multiple times per AST.
    uint64_t Space[4];
    bool ignored;
    (void)FL->getValue().convertToInteger(Space, DestWidth, DestSigned,
                                          llvm::APFloat::rmTowardZero,
                                          &ignored);
    Result = llvm::APInt(DestWidth, 4, Space);
    break;
  }
  case ConditionalOperatorClass: {
    const ConditionalOperator *Exp = cast<ConditionalOperator>(this);
    
    const Expr *Cond = Exp->getCond();
    
    if (!Cond->isIntegerConstantExpr(Result, Ctx, Loc, isEvaluated))
      return false;
    
    const Expr *TrueExp  = Exp->getLHS();
    const Expr *FalseExp = Exp->getRHS();
    if (Result == 0) std::swap(TrueExp, FalseExp);
    
    // If the condition (ignoring parens) is a __builtin_constant_p call, 
    // then only the true side is actually considered in an integer constant
    // expression, and it is fully evaluated.  This is an important GNU
    // extension.  See GCC PR38377 for discussion.
    if (const CallExpr *CallCE = dyn_cast<CallExpr>(Cond->IgnoreParenCasts()))
      if (CallCE->isBuiltinCall() == Builtin::BI__builtin_constant_p) {
        EvalResult EVResult;
        if (!Evaluate(EVResult, Ctx) || EVResult.HasSideEffects)
          return false;
        assert(EVResult.Val.isInt() && "FP conditional expr not expected");
        Result = EVResult.Val.getInt();
        if (Loc) *Loc = EVResult.DiagLoc;
        return true;
      }
    
    // Evaluate the false one first, discard the result.
    if (FalseExp && !FalseExp->isIntegerConstantExpr(Result, Ctx, Loc, false))
      return false;
    // Evalute the true one, capture the result.
    if (TrueExp && 
        !TrueExp->isIntegerConstantExpr(Result, Ctx, Loc, isEvaluated))
      return false;
    break;
  }
  case CXXDefaultArgExprClass:
    return cast<CXXDefaultArgExpr>(this)
             ->isIntegerConstantExpr(Result, Ctx, Loc, isEvaluated);

  case UnaryTypeTraitExprClass:
    Result = cast<UnaryTypeTraitExpr>(this)->Evaluate();
    return true;
  }

  // Cases that are valid constant exprs fall through to here.
  Result.setIsUnsigned(getType()->isUnsignedIntegerType());
  return true;
}

/// isNullPointerConstant - C99 6.3.2.3p3 -  Return true if this is either an
/// integer constant expression with the value zero, or if this is one that is
/// cast to void*.
bool Expr::isNullPointerConstant(ASTContext &Ctx) const
{
  // Strip off a cast to void*, if it exists. Except in C++.
  if (const ExplicitCastExpr *CE = dyn_cast<ExplicitCastExpr>(this)) {
    if (!Ctx.getLangOptions().CPlusPlus) {
      // Check that it is a cast to void*.
      if (const PointerType *PT = CE->getType()->getAsPointerType()) {
        QualType Pointee = PT->getPointeeType();
        if (Pointee.getCVRQualifiers() == 0 && 
            Pointee->isVoidType() &&                              // to void*
            CE->getSubExpr()->getType()->isIntegerType())         // from int.
          return CE->getSubExpr()->isNullPointerConstant(Ctx);
      }
    }
  } else if (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(this)) {
    // Ignore the ImplicitCastExpr type entirely.
    return ICE->getSubExpr()->isNullPointerConstant(Ctx);
  } else if (const ParenExpr *PE = dyn_cast<ParenExpr>(this)) {
    // Accept ((void*)0) as a null pointer constant, as many other
    // implementations do.
    return PE->getSubExpr()->isNullPointerConstant(Ctx);
  } else if (const CXXDefaultArgExpr *DefaultArg 
               = dyn_cast<CXXDefaultArgExpr>(this)) {
    // See through default argument expressions
    return DefaultArg->getExpr()->isNullPointerConstant(Ctx);
  } else if (isa<GNUNullExpr>(this)) {
    // The GNU __null extension is always a null pointer constant.
    return true;
  }

  // This expression must be an integer type.
  if (!getType()->isIntegerType())
    return false;
  
  // If we have an integer constant expression, we need to *evaluate* it and
  // test for the value 0.
  // FIXME: We should probably return false if we're compiling in strict mode
  // and Diag is not null (this indicates that the value was foldable but not
  // an ICE.
  EvalResult Result;
  return Evaluate(Result, Ctx) && !Result.HasSideEffects &&
        Result.Val.isInt() && Result.Val.getInt() == 0;
}

/// isBitField - Return true if this expression is a bit-field.
bool Expr::isBitField() {
  Expr *E = this->IgnoreParenCasts();
  if (MemberExpr *MemRef = dyn_cast<MemberExpr>(E))
    if (FieldDecl *Field = dyn_cast<FieldDecl>(MemRef->getMemberDecl()))
        return Field->isBitField();
  return false;
}

unsigned ExtVectorElementExpr::getNumElements() const {
  if (const VectorType *VT = getType()->getAsVectorType())
    return VT->getNumElements();
  return 1;
}

/// containsDuplicateElements - Return true if any element access is repeated.
bool ExtVectorElementExpr::containsDuplicateElements() const {
  const char *compStr = Accessor.getName();
  unsigned length = Accessor.getLength();

  // Halving swizzles do not contain duplicate elements.
  if (!strcmp(compStr, "hi") || !strcmp(compStr, "lo") || 
      !strcmp(compStr, "even") || !strcmp(compStr, "odd"))
    return false;
  
  // Advance past s-char prefix on hex swizzles.
  if (*compStr == 's') {
    compStr++;
    length--;
  }
  
  for (unsigned i = 0; i != length-1; i++) {
    const char *s = compStr+i;
    for (const char c = *s++; *s; s++)
      if (c == *s) 
        return true;
  }
  return false;
}

/// getEncodedElementAccess - We encode the fields as a llvm ConstantArray.
void ExtVectorElementExpr::getEncodedElementAccess(
                                  llvm::SmallVectorImpl<unsigned> &Elts) const {
  const char *compStr = Accessor.getName();
  if (*compStr == 's')
    compStr++;
 
  bool isHi =   !strcmp(compStr, "hi");
  bool isLo =   !strcmp(compStr, "lo");
  bool isEven = !strcmp(compStr, "even");
  bool isOdd  = !strcmp(compStr, "odd");
    
  for (unsigned i = 0, e = getNumElements(); i != e; ++i) {
    uint64_t Index;
    
    if (isHi)
      Index = e + i;
    else if (isLo)
      Index = i;
    else if (isEven)
      Index = 2 * i;
    else if (isOdd)
      Index = 2 * i + 1;
    else
      Index = ExtVectorType::getAccessorIdx(compStr[i]);

    Elts.push_back(Index);
  }
}

// constructor for instance messages.
ObjCMessageExpr::ObjCMessageExpr(Expr *receiver, Selector selInfo,
                QualType retType, ObjCMethodDecl *mproto,
                SourceLocation LBrac, SourceLocation RBrac,
                Expr **ArgExprs, unsigned nargs)
  : Expr(ObjCMessageExprClass, retType), SelName(selInfo), 
    MethodProto(mproto) {
  NumArgs = nargs;
  SubExprs = new Stmt*[NumArgs+1];
  SubExprs[RECEIVER] = receiver;
  if (NumArgs) {
    for (unsigned i = 0; i != NumArgs; ++i)
      SubExprs[i+ARGS_START] = static_cast<Expr *>(ArgExprs[i]);
  }
  LBracloc = LBrac;
  RBracloc = RBrac;
}

// constructor for class messages. 
// FIXME: clsName should be typed to ObjCInterfaceType
ObjCMessageExpr::ObjCMessageExpr(IdentifierInfo *clsName, Selector selInfo,
                QualType retType, ObjCMethodDecl *mproto,
                SourceLocation LBrac, SourceLocation RBrac,
                Expr **ArgExprs, unsigned nargs)
  : Expr(ObjCMessageExprClass, retType), SelName(selInfo), 
    MethodProto(mproto) {
  NumArgs = nargs;
  SubExprs = new Stmt*[NumArgs+1];
  SubExprs[RECEIVER] = (Expr*) ((uintptr_t) clsName | IsClsMethDeclUnknown);
  if (NumArgs) {
    for (unsigned i = 0; i != NumArgs; ++i)
      SubExprs[i+ARGS_START] = static_cast<Expr *>(ArgExprs[i]);
  }
  LBracloc = LBrac;
  RBracloc = RBrac;
}

// constructor for class messages. 
ObjCMessageExpr::ObjCMessageExpr(ObjCInterfaceDecl *cls, Selector selInfo,
                                 QualType retType, ObjCMethodDecl *mproto,
                                 SourceLocation LBrac, SourceLocation RBrac,
                                 Expr **ArgExprs, unsigned nargs)
: Expr(ObjCMessageExprClass, retType), SelName(selInfo), 
MethodProto(mproto) {
  NumArgs = nargs;
  SubExprs = new Stmt*[NumArgs+1];
  SubExprs[RECEIVER] = (Expr*) ((uintptr_t) cls | IsClsMethDeclKnown);
  if (NumArgs) {
    for (unsigned i = 0; i != NumArgs; ++i)
      SubExprs[i+ARGS_START] = static_cast<Expr *>(ArgExprs[i]);
  }
  LBracloc = LBrac;
  RBracloc = RBrac;
}

ObjCMessageExpr::ClassInfo ObjCMessageExpr::getClassInfo() const {
  uintptr_t x = (uintptr_t) SubExprs[RECEIVER];
  switch (x & Flags) {
    default:
      assert(false && "Invalid ObjCMessageExpr.");
    case IsInstMeth:
      return ClassInfo(0, 0);
    case IsClsMethDeclUnknown:
      return ClassInfo(0, (IdentifierInfo*) (x & ~Flags));
    case IsClsMethDeclKnown: {
      ObjCInterfaceDecl* D = (ObjCInterfaceDecl*) (x & ~Flags);
      return ClassInfo(D, D->getIdentifier());
    }
  }
}

bool ChooseExpr::isConditionTrue(ASTContext &C) const {
  return getCond()->getIntegerConstantExprValue(C) != 0;
}

static int64_t evaluateOffsetOf(ASTContext& C, const Expr *E) {
  if (const MemberExpr *ME = dyn_cast<MemberExpr>(E)) {
    QualType Ty = ME->getBase()->getType();
    
    RecordDecl *RD = Ty->getAsRecordType()->getDecl();
    const ASTRecordLayout &RL = C.getASTRecordLayout(RD);
    if (FieldDecl *FD = dyn_cast<FieldDecl>(ME->getMemberDecl())) {
      // FIXME: This is linear time. And the fact that we're indexing
      // into the layout by position in the record means that we're
      // either stuck numbering the fields in the AST or we have to keep
      // the linear search (yuck and yuck).
      unsigned i = 0;
      for (RecordDecl::field_iterator Field = RD->field_begin(),
                                   FieldEnd = RD->field_end();
           Field != FieldEnd; (void)++Field, ++i) {
        if (*Field == FD)
          break;
      }
      
      return RL.getFieldOffset(i) + evaluateOffsetOf(C, ME->getBase());
    }
  } else if (const ArraySubscriptExpr *ASE = dyn_cast<ArraySubscriptExpr>(E)) {
    const Expr *Base = ASE->getBase();
    
    int64_t size = C.getTypeSize(ASE->getType());
    size *= ASE->getIdx()->getIntegerConstantExprValue(C).getSExtValue();
    
    return size + evaluateOffsetOf(C, Base);
  } else if (isa<CompoundLiteralExpr>(E))
    return 0;  

  assert(0 && "Unknown offsetof subexpression!");
  return 0;
}

int64_t UnaryOperator::evaluateOffsetOf(ASTContext& C) const
{
  assert(Opc == OffsetOf && "Unary operator not offsetof!");
  
  unsigned CharSize = C.Target.getCharWidth();
  return ::evaluateOffsetOf(C, cast<Expr>(Val)) / CharSize;
}

void SizeOfAlignOfExpr::Destroy(ASTContext& C) {
  // Override default behavior of traversing children. If this has a type
  // operand and the type is a variable-length array, the child iteration
  // will iterate over the size expression. However, this expression belongs
  // to the type, not to this, so we don't want to delete it.
  // We still want to delete this expression.
  // FIXME: Same as in Stmt::Destroy - will be eventually in ASTContext's
  // pool allocator.
  if (isArgumentType())
    delete this;
  else
    Expr::Destroy(C);
}

//===----------------------------------------------------------------------===//
//  DesignatedInitExpr
//===----------------------------------------------------------------------===//

IdentifierInfo *DesignatedInitExpr::Designator::getFieldName() {
  assert(Kind == FieldDesignator && "Only valid on a field designator");
  if (Field.NameOrField & 0x01)
    return reinterpret_cast<IdentifierInfo *>(Field.NameOrField&~0x01);
  else
    return getField()->getIdentifier();
}

DesignatedInitExpr *
DesignatedInitExpr::Create(ASTContext &C, Designator *Designators, 
                           unsigned NumDesignators,
                           Expr **IndexExprs, unsigned NumIndexExprs,
                           SourceLocation ColonOrEqualLoc,
                           bool UsesColonSyntax, Expr *Init) {
  void *Mem = C.Allocate(sizeof(DesignatedInitExpr) +
                         sizeof(Designator) * NumDesignators +
                         sizeof(Stmt *) * (NumIndexExprs + 1), 8);
  DesignatedInitExpr *DIE 
    = new (Mem) DesignatedInitExpr(C.VoidTy, NumDesignators,
                                   ColonOrEqualLoc, UsesColonSyntax,
                                   NumIndexExprs + 1);

  // Fill in the designators
  unsigned ExpectedNumSubExprs = 0;
  designators_iterator Desig = DIE->designators_begin();
  for (unsigned Idx = 0; Idx < NumDesignators; ++Idx, ++Desig) {
    new (static_cast<void*>(Desig)) Designator(Designators[Idx]);
    if (Designators[Idx].isArrayDesignator())
      ++ExpectedNumSubExprs;
    else if (Designators[Idx].isArrayRangeDesignator())
      ExpectedNumSubExprs += 2;
  }
  assert(ExpectedNumSubExprs == NumIndexExprs && "Wrong number of indices!");

  // Fill in the subexpressions, including the initializer expression.
  child_iterator Child = DIE->child_begin();
  *Child++ = Init;
  for (unsigned Idx = 0; Idx < NumIndexExprs; ++Idx, ++Child)
    *Child = IndexExprs[Idx];

  return DIE;
}

SourceRange DesignatedInitExpr::getSourceRange() const {
  SourceLocation StartLoc;
  Designator &First = *const_cast<DesignatedInitExpr*>(this)->designators_begin();
  if (First.isFieldDesignator()) {
    if (UsesColonSyntax)
      StartLoc = SourceLocation::getFromRawEncoding(First.Field.FieldLoc);
    else
      StartLoc = SourceLocation::getFromRawEncoding(First.Field.DotLoc);
  } else
    StartLoc = SourceLocation::getFromRawEncoding(First.ArrayOrRange.LBracketLoc);
  return SourceRange(StartLoc, getInit()->getSourceRange().getEnd());
}

DesignatedInitExpr::designators_iterator DesignatedInitExpr::designators_begin() {
  char* Ptr = static_cast<char*>(static_cast<void *>(this));
  Ptr += sizeof(DesignatedInitExpr);
  return static_cast<Designator*>(static_cast<void*>(Ptr));
}

DesignatedInitExpr::designators_iterator DesignatedInitExpr::designators_end() {
  return designators_begin() + NumDesignators;
}

Expr *DesignatedInitExpr::getArrayIndex(const Designator& D) {
  assert(D.Kind == Designator::ArrayDesignator && "Requires array designator");
  char* Ptr = static_cast<char*>(static_cast<void *>(this));
  Ptr += sizeof(DesignatedInitExpr);
  Ptr += sizeof(Designator) * NumDesignators;
  Stmt **SubExprs = reinterpret_cast<Stmt**>(reinterpret_cast<void**>(Ptr));
  return cast<Expr>(*(SubExprs + D.ArrayOrRange.Index + 1));
}

Expr *DesignatedInitExpr::getArrayRangeStart(const Designator& D) {
  assert(D.Kind == Designator::ArrayRangeDesignator && 
         "Requires array range designator");
  char* Ptr = static_cast<char*>(static_cast<void *>(this));
  Ptr += sizeof(DesignatedInitExpr);
  Ptr += sizeof(Designator) * NumDesignators;
  Stmt **SubExprs = reinterpret_cast<Stmt**>(reinterpret_cast<void**>(Ptr));
  return cast<Expr>(*(SubExprs + D.ArrayOrRange.Index + 1));
}

Expr *DesignatedInitExpr::getArrayRangeEnd(const Designator& D) {
  assert(D.Kind == Designator::ArrayRangeDesignator && 
         "Requires array range designator");
  char* Ptr = static_cast<char*>(static_cast<void *>(this));
  Ptr += sizeof(DesignatedInitExpr);
  Ptr += sizeof(Designator) * NumDesignators;
  Stmt **SubExprs = reinterpret_cast<Stmt**>(reinterpret_cast<void**>(Ptr));
  return cast<Expr>(*(SubExprs + D.ArrayOrRange.Index + 2));
}

//===----------------------------------------------------------------------===//
//  ExprIterator.
//===----------------------------------------------------------------------===//

Expr* ExprIterator::operator[](size_t idx) { return cast<Expr>(I[idx]); }
Expr* ExprIterator::operator*() const { return cast<Expr>(*I); }
Expr* ExprIterator::operator->() const { return cast<Expr>(*I); }
const Expr* ConstExprIterator::operator[](size_t idx) const {
  return cast<Expr>(I[idx]);
}
const Expr* ConstExprIterator::operator*() const { return cast<Expr>(*I); }
const Expr* ConstExprIterator::operator->() const { return cast<Expr>(*I); }

//===----------------------------------------------------------------------===//
//  Child Iterators for iterating over subexpressions/substatements
//===----------------------------------------------------------------------===//

// DeclRefExpr
Stmt::child_iterator DeclRefExpr::child_begin() { return child_iterator(); }
Stmt::child_iterator DeclRefExpr::child_end() { return child_iterator(); }

// ObjCIvarRefExpr
Stmt::child_iterator ObjCIvarRefExpr::child_begin() { return &Base; }
Stmt::child_iterator ObjCIvarRefExpr::child_end() { return &Base+1; }

// ObjCPropertyRefExpr
Stmt::child_iterator ObjCPropertyRefExpr::child_begin() { return &Base; }
Stmt::child_iterator ObjCPropertyRefExpr::child_end() { return &Base+1; }

// ObjCKVCRefExpr
Stmt::child_iterator ObjCKVCRefExpr::child_begin() { return &Base; }
Stmt::child_iterator ObjCKVCRefExpr::child_end() { return &Base+1; }

// ObjCSuperExpr
Stmt::child_iterator ObjCSuperExpr::child_begin() { return child_iterator(); }
Stmt::child_iterator ObjCSuperExpr::child_end() { return child_iterator(); }

// PredefinedExpr
Stmt::child_iterator PredefinedExpr::child_begin() { return child_iterator(); }
Stmt::child_iterator PredefinedExpr::child_end() { return child_iterator(); }

// IntegerLiteral
Stmt::child_iterator IntegerLiteral::child_begin() { return child_iterator(); }
Stmt::child_iterator IntegerLiteral::child_end() { return child_iterator(); }

// CharacterLiteral
Stmt::child_iterator CharacterLiteral::child_begin() { return child_iterator(); }
Stmt::child_iterator CharacterLiteral::child_end() { return child_iterator(); }

// FloatingLiteral
Stmt::child_iterator FloatingLiteral::child_begin() { return child_iterator(); }
Stmt::child_iterator FloatingLiteral::child_end() { return child_iterator(); }

// ImaginaryLiteral
Stmt::child_iterator ImaginaryLiteral::child_begin() { return &Val; }
Stmt::child_iterator ImaginaryLiteral::child_end() { return &Val+1; }

// StringLiteral
Stmt::child_iterator StringLiteral::child_begin() { return child_iterator(); }
Stmt::child_iterator StringLiteral::child_end() { return child_iterator(); }

// ParenExpr
Stmt::child_iterator ParenExpr::child_begin() { return &Val; }
Stmt::child_iterator ParenExpr::child_end() { return &Val+1; }

// UnaryOperator
Stmt::child_iterator UnaryOperator::child_begin() { return &Val; }
Stmt::child_iterator UnaryOperator::child_end() { return &Val+1; }

// SizeOfAlignOfExpr
Stmt::child_iterator SizeOfAlignOfExpr::child_begin() { 
  // If this is of a type and the type is a VLA type (and not a typedef), the
  // size expression of the VLA needs to be treated as an executable expression.
  // Why isn't this weirdness documented better in StmtIterator?
  if (isArgumentType()) {
    if (VariableArrayType* T = dyn_cast<VariableArrayType>(
                                   getArgumentType().getTypePtr()))
      return child_iterator(T);
    return child_iterator();
  }
  return child_iterator(&Argument.Ex);
}
Stmt::child_iterator SizeOfAlignOfExpr::child_end() {
  if (isArgumentType())
    return child_iterator();
  return child_iterator(&Argument.Ex + 1);
}

// ArraySubscriptExpr
Stmt::child_iterator ArraySubscriptExpr::child_begin() {
  return &SubExprs[0];
}
Stmt::child_iterator ArraySubscriptExpr::child_end() {
  return &SubExprs[0]+END_EXPR;
}

// CallExpr
Stmt::child_iterator CallExpr::child_begin() {
  return &SubExprs[0];
}
Stmt::child_iterator CallExpr::child_end() {
  return &SubExprs[0]+NumArgs+ARGS_START;
}

// MemberExpr
Stmt::child_iterator MemberExpr::child_begin() { return &Base; }
Stmt::child_iterator MemberExpr::child_end() { return &Base+1; }

// ExtVectorElementExpr
Stmt::child_iterator ExtVectorElementExpr::child_begin() { return &Base; }
Stmt::child_iterator ExtVectorElementExpr::child_end() { return &Base+1; }

// CompoundLiteralExpr
Stmt::child_iterator CompoundLiteralExpr::child_begin() { return &Init; }
Stmt::child_iterator CompoundLiteralExpr::child_end() { return &Init+1; }

// CastExpr
Stmt::child_iterator CastExpr::child_begin() { return &Op; }
Stmt::child_iterator CastExpr::child_end() { return &Op+1; }

// BinaryOperator
Stmt::child_iterator BinaryOperator::child_begin() {
  return &SubExprs[0];
}
Stmt::child_iterator BinaryOperator::child_end() {
  return &SubExprs[0]+END_EXPR;
}

// ConditionalOperator
Stmt::child_iterator ConditionalOperator::child_begin() {
  return &SubExprs[0];
}
Stmt::child_iterator ConditionalOperator::child_end() {
  return &SubExprs[0]+END_EXPR;
}

// AddrLabelExpr
Stmt::child_iterator AddrLabelExpr::child_begin() { return child_iterator(); }
Stmt::child_iterator AddrLabelExpr::child_end() { return child_iterator(); }

// StmtExpr
Stmt::child_iterator StmtExpr::child_begin() { return &SubStmt; }
Stmt::child_iterator StmtExpr::child_end() { return &SubStmt+1; }

// TypesCompatibleExpr
Stmt::child_iterator TypesCompatibleExpr::child_begin() {
  return child_iterator();
}

Stmt::child_iterator TypesCompatibleExpr::child_end() {
  return child_iterator();
}

// ChooseExpr
Stmt::child_iterator ChooseExpr::child_begin() { return &SubExprs[0]; }
Stmt::child_iterator ChooseExpr::child_end() { return &SubExprs[0]+END_EXPR; }

// GNUNullExpr
Stmt::child_iterator GNUNullExpr::child_begin() { return child_iterator(); }
Stmt::child_iterator GNUNullExpr::child_end() { return child_iterator(); }

// OverloadExpr
Stmt::child_iterator OverloadExpr::child_begin() { return &SubExprs[0]; }
Stmt::child_iterator OverloadExpr::child_end() { return &SubExprs[0]+NumExprs; }

// ShuffleVectorExpr
Stmt::child_iterator ShuffleVectorExpr::child_begin() {
  return &SubExprs[0];
}
Stmt::child_iterator ShuffleVectorExpr::child_end() {
  return &SubExprs[0]+NumExprs;
}

// VAArgExpr
Stmt::child_iterator VAArgExpr::child_begin() { return &Val; }
Stmt::child_iterator VAArgExpr::child_end() { return &Val+1; }

// InitListExpr
Stmt::child_iterator InitListExpr::child_begin() {
  return InitExprs.size() ? &InitExprs[0] : 0;
}
Stmt::child_iterator InitListExpr::child_end() {
  return InitExprs.size() ? &InitExprs[0] + InitExprs.size() : 0;
}

// DesignatedInitExpr
Stmt::child_iterator DesignatedInitExpr::child_begin() {
  char* Ptr = static_cast<char*>(static_cast<void *>(this));
  Ptr += sizeof(DesignatedInitExpr);
  Ptr += sizeof(Designator) * NumDesignators;
  return reinterpret_cast<Stmt**>(reinterpret_cast<void**>(Ptr));
}
Stmt::child_iterator DesignatedInitExpr::child_end() {
  return child_iterator(&*child_begin() + NumSubExprs);
}

// ImplicitValueInitExpr
Stmt::child_iterator ImplicitValueInitExpr::child_begin() { 
  return child_iterator(); 
}

Stmt::child_iterator ImplicitValueInitExpr::child_end() { 
  return child_iterator(); 
}

// ObjCStringLiteral
Stmt::child_iterator ObjCStringLiteral::child_begin() { 
  return child_iterator();
}
Stmt::child_iterator ObjCStringLiteral::child_end() {
  return child_iterator();
}

// ObjCEncodeExpr
Stmt::child_iterator ObjCEncodeExpr::child_begin() { return child_iterator(); }
Stmt::child_iterator ObjCEncodeExpr::child_end() { return child_iterator(); }

// ObjCSelectorExpr
Stmt::child_iterator ObjCSelectorExpr::child_begin() { 
  return child_iterator();
}
Stmt::child_iterator ObjCSelectorExpr::child_end() {
  return child_iterator();
}

// ObjCProtocolExpr
Stmt::child_iterator ObjCProtocolExpr::child_begin() {
  return child_iterator();
}
Stmt::child_iterator ObjCProtocolExpr::child_end() {
  return child_iterator();
}

// ObjCMessageExpr
Stmt::child_iterator ObjCMessageExpr::child_begin() {  
  return getReceiver() ? &SubExprs[0] : &SubExprs[0] + ARGS_START;
}
Stmt::child_iterator ObjCMessageExpr::child_end() {
  return &SubExprs[0]+ARGS_START+getNumArgs();
}

// Blocks
Stmt::child_iterator BlockExpr::child_begin() { return child_iterator(); }
Stmt::child_iterator BlockExpr::child_end() { return child_iterator(); }

Stmt::child_iterator BlockDeclRefExpr::child_begin() { return child_iterator();}
Stmt::child_iterator BlockDeclRefExpr::child_end() { return child_iterator(); }
