//===--- ExprCXX.h - Classes for representing expressions -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Expr interface and subclasses for C++ expressions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_EXPRCXX_H
#define LLVM_CLANG_AST_EXPRCXX_H

#include "clang/Basic/TypeTraits.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Decl.h"

namespace clang {

  class CXXConstructorDecl;

//===--------------------------------------------------------------------===//
// C++ Expressions.
//===--------------------------------------------------------------------===//

/// CXXOperatorCallExpr - Represents a call to an overloaded operator
/// written using operator syntax, e.g., "x + y" or "*p". While
/// semantically equivalent to a normal call, this AST node provides
/// better information about the syntactic representation of the call.
class CXXOperatorCallExpr : public CallExpr {
public:
  CXXOperatorCallExpr(Expr *fn, Expr **args, unsigned numargs, QualType t,
                      SourceLocation operatorloc)
    : CallExpr(CXXOperatorCallExprClass, fn, args, numargs, t, operatorloc) { }

  /// getOperator - Returns the kind of overloaded operator that this
  /// expression refers to.
  OverloadedOperatorKind getOperator() const;

  /// getOperatorLoc - Returns the location of the operator symbol in
  /// the expression. When @c getOperator()==OO_Call, this is the
  /// location of the right parentheses; when @c
  /// getOperator()==OO_Subscript, this is the location of the right
  /// bracket.
  SourceLocation getOperatorLoc() const { return getRParenLoc(); }

  virtual SourceRange getSourceRange() const;
  
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXOperatorCallExprClass; 
  }
  static bool classof(const CXXOperatorCallExpr *) { return true; }
};

/// CXXMemberCallExpr - Represents a call to a member function that
/// may be written either with member call syntax (e.g., "obj.func()"
/// or "objptr->func()") or with normal function-call syntax
/// ("func()") within a member function that ends up calling a member
/// function. The callee in either case is a MemberExpr that contains
/// both the object argument and the member function, while the
/// arguments are the arguments within the parentheses (not including
/// the object argument).
class CXXMemberCallExpr : public CallExpr {
public:
  CXXMemberCallExpr(Expr *fn, Expr **args, unsigned numargs, QualType t,
                      SourceLocation rparenloc)
    : CallExpr(CXXMemberCallExprClass, fn, args, numargs, t, rparenloc) { }

  /// getImplicitObjectArgument - Retrieves the implicit object
  /// argument for the member call. For example, in "x.f(5)", this
  /// operation would return "x".
  Expr *getImplicitObjectArgument();

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXMemberCallExprClass;
  }
  static bool classof(const CXXMemberCallExpr *) { return true; }
};

/// CXXNamedCastExpr - Abstract class common to all of the C++ "named"
/// casts, @c static_cast, @c dynamic_cast, @c reinterpret_cast, or @c
/// const_cast.
///
/// This abstract class is inherited by all of the classes
/// representing "named" casts, e.g., CXXStaticCastExpr,
/// CXXDynamicCastExpr, CXXReinterpretCastExpr, and CXXConstCastExpr.
class CXXNamedCastExpr : public ExplicitCastExpr {
private:
  SourceLocation Loc; // the location of the casting op

protected:
  CXXNamedCastExpr(StmtClass SC, QualType ty, Expr *op, QualType writtenTy, 
                   SourceLocation l)
    : ExplicitCastExpr(SC, ty, op, writtenTy), Loc(l) {}

public:
  const char *getCastName() const;

  virtual SourceRange getSourceRange() const {
    return SourceRange(Loc, getSubExpr()->getSourceRange().getEnd());
  }
  static bool classof(const Stmt *T) { 
    switch (T->getStmtClass()) {
    case CXXNamedCastExprClass:
    case CXXStaticCastExprClass:
    case CXXDynamicCastExprClass:
    case CXXReinterpretCastExprClass:
    case CXXConstCastExprClass:
      return true;
    default:
      return false;
    }
  }
  static bool classof(const CXXNamedCastExpr *) { return true; }
      
  virtual void EmitImpl(llvm::Serializer& S) const;
  static CXXNamedCastExpr *CreateImpl(llvm::Deserializer& D, ASTContext& C,
                                      StmtClass SC);
};

/// CXXStaticCastExpr - A C++ @c static_cast expression (C++ [expr.static.cast]).
/// 
/// This expression node represents a C++ static cast, e.g.,
/// @c static_cast<int>(1.0).
class CXXStaticCastExpr : public CXXNamedCastExpr {
public:
  CXXStaticCastExpr(QualType ty, Expr *op, QualType writtenTy, SourceLocation l)
    : CXXNamedCastExpr(CXXStaticCastExprClass, ty, op, writtenTy, l) {}

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXStaticCastExprClass;
  }
  static bool classof(const CXXStaticCastExpr *) { return true; }
};

/// CXXDynamicCastExpr - A C++ @c dynamic_cast expression
/// (C++ [expr.dynamic.cast]), which may perform a run-time check to 
/// determine how to perform the type cast.
/// 
/// This expression node represents a dynamic cast, e.g.,
/// @c dynamic_cast<Derived*>(BasePtr).
class CXXDynamicCastExpr : public CXXNamedCastExpr {
public:
  CXXDynamicCastExpr(QualType ty, Expr *op, QualType writtenTy, SourceLocation l)
    : CXXNamedCastExpr(CXXDynamicCastExprClass, ty, op, writtenTy, l) {}

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXDynamicCastExprClass;
  }
  static bool classof(const CXXDynamicCastExpr *) { return true; }
};

/// CXXReinterpretCastExpr - A C++ @c reinterpret_cast expression (C++
/// [expr.reinterpret.cast]), which provides a differently-typed view
/// of a value but performs no actual work at run time.
/// 
/// This expression node represents a reinterpret cast, e.g.,
/// @c reinterpret_cast<int>(VoidPtr).
class CXXReinterpretCastExpr : public CXXNamedCastExpr {
public:
  CXXReinterpretCastExpr(QualType ty, Expr *op, QualType writtenTy, 
                         SourceLocation l)
    : CXXNamedCastExpr(CXXReinterpretCastExprClass, ty, op, writtenTy, l) {}

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXReinterpretCastExprClass;
  }
  static bool classof(const CXXReinterpretCastExpr *) { return true; }
};

/// CXXConstCastExpr - A C++ @c const_cast expression (C++ [expr.const.cast]),
/// which can remove type qualifiers but does not change the underlying value.
/// 
/// This expression node represents a const cast, e.g.,
/// @c const_cast<char*>(PtrToConstChar).
class CXXConstCastExpr : public CXXNamedCastExpr {
public:
  CXXConstCastExpr(QualType ty, Expr *op, QualType writtenTy, 
                   SourceLocation l)
    : CXXNamedCastExpr(CXXConstCastExprClass, ty, op, writtenTy, l) {}

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXConstCastExprClass;
  }
  static bool classof(const CXXConstCastExpr *) { return true; }
};

/// CXXBoolLiteralExpr - [C++ 2.13.5] C++ Boolean Literal.
/// 
class CXXBoolLiteralExpr : public Expr {
  bool Value;
  SourceLocation Loc;
public:
  CXXBoolLiteralExpr(bool val, QualType Ty, SourceLocation l) : 
    Expr(CXXBoolLiteralExprClass, Ty), Value(val), Loc(l) {}
  
  bool getValue() const { return Value; }

  virtual SourceRange getSourceRange() const { return SourceRange(Loc); }
    
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXBoolLiteralExprClass;
  }
  static bool classof(const CXXBoolLiteralExpr *) { return true; }
      
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

/// CXXTypeidExpr - A C++ @c typeid expression (C++ [expr.typeid]), which gets
/// the type_info that corresponds to the supplied type, or the (possibly
/// dynamic) type of the supplied expression.
///
/// This represents code like @c typeid(int) or @c typeid(*objPtr)
class CXXTypeidExpr : public Expr {
private:
  bool isTypeOp : 1;
  union {
    void *Ty;
    Stmt *Ex;
  } Operand;
  SourceRange Range;

public:
  CXXTypeidExpr(bool isTypeOp, void *op, QualType Ty, const SourceRange r) :
      Expr(CXXTypeidExprClass, Ty), isTypeOp(isTypeOp), Range(r) {
    if (isTypeOp)
      Operand.Ty = op;
    else
      // op was an Expr*, so cast it back to that to be safe
      Operand.Ex = static_cast<Stmt*>(op);
  }

  bool isTypeOperand() const { return isTypeOp; }
  QualType getTypeOperand() const {
    assert(isTypeOperand() && "Cannot call getTypeOperand for typeid(expr)");
    return QualType::getFromOpaquePtr(Operand.Ty);
  }
  Expr* getExprOperand() const {
    assert(!isTypeOperand() && "Cannot call getExprOperand for typeid(type)");
    return static_cast<Expr*>(Operand.Ex);
  }

  virtual SourceRange getSourceRange() const {
    return Range;
  }
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CXXTypeidExprClass;
  }
  static bool classof(const CXXTypeidExpr *) { return true; }

  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();

  virtual void EmitImpl(llvm::Serializer& S) const;
  static CXXTypeidExpr* CreateImpl(llvm::Deserializer& D, ASTContext& C);
};

/// CXXThisExpr - Represents the "this" expression in C++, which is a
/// pointer to the object on which the current member function is
/// executing (C++ [expr.prim]p3). Example:
///
/// @code
/// class Foo {
/// public:
///   void bar();
///   void test() { this->bar(); }
/// };
/// @endcode
class CXXThisExpr : public Expr {
  SourceLocation Loc;

public:
  CXXThisExpr(SourceLocation L, QualType Type) 
    : Expr(CXXThisExprClass, Type), Loc(L) { }

  virtual SourceRange getSourceRange() const { return SourceRange(Loc); }

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXThisExprClass;
  }
  static bool classof(const CXXThisExpr *) { return true; }

  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static CXXThisExpr* CreateImpl(llvm::Deserializer& D, ASTContext& C);  
};

///  CXXThrowExpr - [C++ 15] C++ Throw Expression.  This handles
///  'throw' and 'throw' assignment-expression.  When
///  assignment-expression isn't present, Op will be null.
///
class CXXThrowExpr : public Expr {
  Stmt *Op;
  SourceLocation ThrowLoc;
public:
  // Ty is the void type which is used as the result type of the
  // exepression.  The l is the location of the throw keyword.  expr
  // can by null, if the optional expression to throw isn't present.
  CXXThrowExpr(Expr *expr, QualType Ty, SourceLocation l) :
    Expr(CXXThrowExprClass, Ty), Op(expr), ThrowLoc(l) {}
  const Expr *getSubExpr() const { return cast_or_null<Expr>(Op); }
  Expr *getSubExpr() { return cast_or_null<Expr>(Op); }

  virtual SourceRange getSourceRange() const {
    if (getSubExpr() == 0)
      return SourceRange(ThrowLoc, ThrowLoc);
    return SourceRange(ThrowLoc, getSubExpr()->getSourceRange().getEnd());
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CXXThrowExprClass;
  }
  static bool classof(const CXXThrowExpr *) { return true; }

  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();
};

/// CXXDefaultArgExpr - C++ [dcl.fct.default]. This wraps up a
/// function call argument that was created from the corresponding
/// parameter's default argument, when the call did not explicitly
/// supply arguments for all of the parameters.
class CXXDefaultArgExpr : public Expr {
  ParmVarDecl *Param;
public:
  // Param is the parameter whose default argument is used by this
  // expression.
  explicit CXXDefaultArgExpr(ParmVarDecl *param) 
    : Expr(CXXDefaultArgExprClass, 
           param->hasUnparsedDefaultArg()? param->getType().getNonReferenceType()
                                         : param->getDefaultArg()->getType()),
      Param(param) { }

  // Retrieve the parameter that the argument was created from.
  const ParmVarDecl *getParam() const { return Param; }
  ParmVarDecl *getParam() { return Param; }

  // Retrieve the actual argument to the function call.
  const Expr *getExpr() const { return Param->getDefaultArg(); }
  Expr *getExpr() { return Param->getDefaultArg(); }

  virtual SourceRange getSourceRange() const {
    // Default argument expressions have no representation in the
    // source, so they have an empty source range.
    return SourceRange();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CXXDefaultArgExprClass;
  }
  static bool classof(const CXXDefaultArgExpr *) { return true; }

  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();

  // Serialization
  virtual void EmitImpl(llvm::Serializer& S) const;
  static CXXDefaultArgExpr* CreateImpl(llvm::Deserializer& D,
                                       ASTContext& C);
};

/// CXXFunctionalCastExpr - Represents an explicit C++ type conversion
/// that uses "functional" notion (C++ [expr.type.conv]). Example: @c
/// x = int(0.5);
class CXXFunctionalCastExpr : public ExplicitCastExpr {
  SourceLocation TyBeginLoc;
  SourceLocation RParenLoc;
public:
  CXXFunctionalCastExpr(QualType ty, QualType writtenTy, 
                        SourceLocation tyBeginLoc, Expr *castExpr,
                        SourceLocation rParenLoc) : 
    ExplicitCastExpr(CXXFunctionalCastExprClass, ty, castExpr, writtenTy),
    TyBeginLoc(tyBeginLoc), RParenLoc(rParenLoc) {}

  SourceLocation getTypeBeginLoc() const { return TyBeginLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  
  virtual SourceRange getSourceRange() const {
    return SourceRange(TyBeginLoc, RParenLoc);
  }
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXFunctionalCastExprClass; 
  }
  static bool classof(const CXXFunctionalCastExpr *) { return true; }
  
  virtual void EmitImpl(llvm::Serializer& S) const;
  static CXXFunctionalCastExpr *
      CreateImpl(llvm::Deserializer& D, ASTContext& C);
};

/// @brief Represents a C++ functional cast expression that builds a
/// temporary object.
///
/// This expression type represents a C++ "functional" cast 
/// (C++[expr.type.conv]) with N != 1 arguments that invokes a
/// constructor to build a temporary object. If N == 0 but no
/// constructor will be called (because the functional cast is
/// performing a value-initialized an object whose class type has no
/// user-declared constructors), CXXZeroInitValueExpr will represent
/// the functional cast. Finally, with N == 1 arguments the functional
/// cast expression will be represented by CXXFunctionalCastExpr.
/// Example:
/// @code
/// struct X { X(int, float); }
///
/// X create_X() {
///   return X(1, 3.14f); // creates a CXXTemporaryObjectExpr
/// };
/// @endcode
class CXXTemporaryObjectExpr : public Expr {
  SourceLocation TyBeginLoc;
  SourceLocation RParenLoc;
  CXXConstructorDecl *Constructor;
  Stmt **Args;
  unsigned NumArgs;

public:
  CXXTemporaryObjectExpr(CXXConstructorDecl *Cons, QualType writtenTy,
                         SourceLocation tyBeginLoc, Expr **Args,
                         unsigned NumArgs, SourceLocation rParenLoc);

  ~CXXTemporaryObjectExpr();

  SourceLocation getTypeBeginLoc() const { return TyBeginLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  
  typedef ExprIterator arg_iterator;
  typedef ConstExprIterator const_arg_iterator;
    
  arg_iterator arg_begin() { return Args; }
  arg_iterator arg_end() { return Args + NumArgs; }
  const_arg_iterator arg_begin() const { return Args; }
  const_arg_iterator arg_end() const { return Args + NumArgs; }
  
  virtual SourceRange getSourceRange() const {
    return SourceRange(TyBeginLoc, RParenLoc);
  }
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXTemporaryObjectExprClass;
  }
  static bool classof(const CXXTemporaryObjectExpr *) { return true; }
  
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();

  virtual void EmitImpl(llvm::Serializer& S) const;
  static CXXTemporaryObjectExpr *CreateImpl(llvm::Deserializer& D, ASTContext& C);
};

/// CXXZeroInitValueExpr - [C++ 5.2.3p2]
/// Expression "T()" which creates a value-initialized rvalue of type
/// T, which is either a non-class type or a class type without any
/// user-defined constructors.
///
class CXXZeroInitValueExpr : public Expr {
  SourceLocation TyBeginLoc;
  SourceLocation RParenLoc;

public:
  CXXZeroInitValueExpr(QualType ty, SourceLocation tyBeginLoc,
                       SourceLocation rParenLoc ) : 
    Expr(CXXZeroInitValueExprClass, ty),
    TyBeginLoc(tyBeginLoc), RParenLoc(rParenLoc) {}
  
  SourceLocation getTypeBeginLoc() const { return TyBeginLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }

  /// @brief Whether this initialization expression was
  /// implicitly-generated.
  bool isImplicit() const { 
    return TyBeginLoc.isInvalid() && RParenLoc.isInvalid(); 
  }

  virtual SourceRange getSourceRange() const {
    return SourceRange(TyBeginLoc, RParenLoc);
  }
    
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXZeroInitValueExprClass;
  }
  static bool classof(const CXXZeroInitValueExpr *) { return true; }
      
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();

  virtual void EmitImpl(llvm::Serializer& S) const;
  static CXXZeroInitValueExpr *
      CreateImpl(llvm::Deserializer& D, ASTContext& C);
};

/// CXXConditionDeclExpr - Condition declaration of a if/switch/while/for
/// statement, e.g: "if (int x = f()) {...}".
/// The main difference with DeclRefExpr is that CXXConditionDeclExpr owns the
/// decl that it references.
///
class CXXConditionDeclExpr : public DeclRefExpr {
public:
  CXXConditionDeclExpr(SourceLocation startLoc,
                       SourceLocation eqLoc, VarDecl *var)
    : DeclRefExpr(CXXConditionDeclExprClass, var, 
                  var->getType().getNonReferenceType(), startLoc) {}

  virtual void Destroy(ASTContext& Ctx);

  SourceLocation getStartLoc() const { return getLocation(); }
  
  VarDecl *getVarDecl() { return cast<VarDecl>(getDecl()); }
  const VarDecl *getVarDecl() const { return cast<VarDecl>(getDecl()); }

  virtual SourceRange getSourceRange() const {
    return SourceRange(getStartLoc(), getVarDecl()->getInit()->getLocEnd());
  }
    
  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXConditionDeclExprClass;
  }
  static bool classof(const CXXConditionDeclExpr *) { return true; }
      
  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();

  // FIXME: Implement these.
  //virtual void EmitImpl(llvm::Serializer& S) const;
  //static CXXConditionDeclExpr *
  //    CreateImpl(llvm::Deserializer& D, ASTContext& C);
};

/// CXXNewExpr - A new expression for memory allocation and constructor calls,
/// e.g: "new CXXNewExpr(foo)".
class CXXNewExpr : public Expr {
  // Was the usage ::new, i.e. is the global new to be used?
  bool GlobalNew : 1;
  // Was the form (type-id) used? Otherwise, it was new-type-id.
  bool ParenTypeId : 1;
  // Is there an initializer? If not, built-ins are uninitialized, else they're
  // value-initialized.
  bool Initializer : 1;
  // Do we allocate an array? If so, the first SubExpr is the size expression.
  bool Array : 1;
  // The number of placement new arguments.
  unsigned NumPlacementArgs : 14;
  // The number of constructor arguments. This may be 1 even for non-class
  // types; use the pseudo copy constructor.
  unsigned NumConstructorArgs : 14;
  // Contains an optional array size expression, any number of optional
  // placement arguments, and any number of optional constructor arguments,
  // in that order.
  Stmt **SubExprs;
  // Points to the allocation function used.
  FunctionDecl *OperatorNew;
  // Points to the deallocation function used in case of error. May be null.
  FunctionDecl *OperatorDelete;
  // Points to the constructor used. Cannot be null if AllocType is a record;
  // it would still point at the default constructor (even an implicit one).
  // Must be null for all other types.
  CXXConstructorDecl *Constructor;

  SourceLocation StartLoc;
  SourceLocation EndLoc;

  // Deserialization constructor
  CXXNewExpr(QualType ty, bool globalNew, bool parenTypeId, bool initializer,
             bool array, unsigned numPlaceArgs, unsigned numConsArgs,
             Stmt **subExprs, FunctionDecl *operatorNew,
             FunctionDecl *operatorDelete, CXXConstructorDecl *constructor,
             SourceLocation startLoc, SourceLocation endLoc)
    : Expr(CXXNewExprClass, ty), GlobalNew(globalNew), ParenTypeId(parenTypeId),
      Initializer(initializer), Array(array), NumPlacementArgs(numPlaceArgs),
      NumConstructorArgs(numConsArgs), SubExprs(subExprs),
      OperatorNew(operatorNew), OperatorDelete(operatorDelete),
      Constructor(constructor), StartLoc(startLoc), EndLoc(endLoc)
  { }
public:
  CXXNewExpr(bool globalNew, FunctionDecl *operatorNew, Expr **placementArgs,
             unsigned numPlaceArgs, bool ParenTypeId, Expr *arraySize,
             CXXConstructorDecl *constructor, bool initializer,
             Expr **constructorArgs, unsigned numConsArgs,
             FunctionDecl *operatorDelete, QualType ty,
             SourceLocation startLoc, SourceLocation endLoc);
  ~CXXNewExpr() {
    delete[] SubExprs;
  }

  QualType getAllocatedType() const {
    assert(getType()->isPointerType());
    return getType()->getAsPointerType()->getPointeeType();
  }

  FunctionDecl *getOperatorNew() const { return OperatorNew; }
  FunctionDecl *getOperatorDelete() const { return OperatorDelete; }
  CXXConstructorDecl *getConstructor() const { return Constructor; }

  bool isArray() const { return Array; }
  Expr *getArraySize() {
    return Array ? cast<Expr>(SubExprs[0]) : 0;
  }
  const Expr *getArraySize() const {
    return Array ? cast<Expr>(SubExprs[0]) : 0;
  }

  unsigned getNumPlacementArgs() const { return NumPlacementArgs; }
  Expr *getPlacementArg(unsigned i) {
    assert(i < NumPlacementArgs && "Index out of range");
    return cast<Expr>(SubExprs[Array + i]);
  }
  const Expr *getPlacementArg(unsigned i) const {
    assert(i < NumPlacementArgs && "Index out of range");
    return cast<Expr>(SubExprs[Array + i]);
  }

  bool isGlobalNew() const { return GlobalNew; }
  bool isParenTypeId() const { return ParenTypeId; }
  bool hasInitializer() const { return Initializer; }

  unsigned getNumConstructorArgs() const { return NumConstructorArgs; }
  Expr *getConstructorArg(unsigned i) {
    assert(i < NumConstructorArgs && "Index out of range");
    return cast<Expr>(SubExprs[Array + NumPlacementArgs + i]);
  }
  const Expr *getConstructorArg(unsigned i) const {
    assert(i < NumConstructorArgs && "Index out of range");
    return cast<Expr>(SubExprs[Array + NumPlacementArgs + i]);
  }

  typedef ExprIterator arg_iterator;
  typedef ConstExprIterator const_arg_iterator;

  arg_iterator placement_arg_begin() {
    return SubExprs + Array;
  }
  arg_iterator placement_arg_end() {
    return SubExprs + Array + getNumPlacementArgs();
  }
  const_arg_iterator placement_arg_begin() const {
    return SubExprs + Array;
  }
  const_arg_iterator placement_arg_end() const {
    return SubExprs + Array + getNumPlacementArgs();
  }

  arg_iterator constructor_arg_begin() {
    return SubExprs + Array + getNumPlacementArgs();
  }
  arg_iterator constructor_arg_end() {
    return SubExprs + Array + getNumPlacementArgs() + getNumConstructorArgs();
  }
  const_arg_iterator constructor_arg_begin() const {
    return SubExprs + Array + getNumPlacementArgs();
  }
  const_arg_iterator constructor_arg_end() const {
    return SubExprs + Array + getNumPlacementArgs() + getNumConstructorArgs();
  }

  virtual SourceRange getSourceRange() const {
    return SourceRange(StartLoc, EndLoc);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CXXNewExprClass;
  }
  static bool classof(const CXXNewExpr *) { return true; }

  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();

  virtual void EmitImpl(llvm::Serializer& S) const;
  static CXXNewExpr* CreateImpl(llvm::Deserializer& D, ASTContext& C);
};

/// CXXDeleteExpr - A delete expression for memory deallocation and destructor
/// calls, e.g. "delete[] pArray".
class CXXDeleteExpr : public Expr {
  // Is this a forced global delete, i.e. "::delete"?
  bool GlobalDelete : 1;
  // Is this the array form of delete, i.e. "delete[]"?
  bool ArrayForm : 1;
  // Points to the operator delete overload that is used. Could be a member.
  FunctionDecl *OperatorDelete;
  // The pointer expression to be deleted.
  Stmt *Argument;
  // Location of the expression.
  SourceLocation Loc;
public:
  CXXDeleteExpr(QualType ty, bool globalDelete, bool arrayForm,
                FunctionDecl *operatorDelete, Expr *arg, SourceLocation loc)
    : Expr(CXXDeleteExprClass, ty), GlobalDelete(globalDelete),
      ArrayForm(arrayForm), OperatorDelete(operatorDelete), Argument(arg),
      Loc(loc) { }

  bool isGlobalDelete() const { return GlobalDelete; }
  bool isArrayForm() const { return ArrayForm; }

  FunctionDecl *getOperatorDelete() const { return OperatorDelete; }

  Expr *getArgument() { return cast<Expr>(Argument); }
  const Expr *getArgument() const { return cast<Expr>(Argument); }

  virtual SourceRange getSourceRange() const {
    return SourceRange(Loc, Argument->getLocEnd());
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CXXDeleteExprClass;
  }
  static bool classof(const CXXDeleteExpr *) { return true; }

  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();

  virtual void EmitImpl(llvm::Serializer& S) const;
  static CXXDeleteExpr * CreateImpl(llvm::Deserializer& D, ASTContext& C);
};

/// CXXDependentNameExpr - Represents a dependent name in C++ for
/// which we could not locate any definition. These names can only
/// occur as in the example below, with an unqualified call to a
/// function name whose arguments are dependent.
/// @code
/// template<typename T> void f(T x) {
///   g(x); // g is a dependent name.
/// }
/// @endcode
class CXXDependentNameExpr : public Expr {
  /// Name - The name that was present in the source code.
  IdentifierInfo *Name;

  /// Loc - The location 
  SourceLocation Loc;

public:
  CXXDependentNameExpr(IdentifierInfo *N, QualType T, SourceLocation L)
    : Expr(CXXDependentNameExprClass, T, true, true), Name(N), Loc(L) { }

  /// getName - Retrieves the name that occurred in the source code.
  IdentifierInfo *getName() const { return Name; }

  /// getLocation - Retrieves the location in the source code where
  /// the name occurred.
  SourceLocation getLocation() const { return Loc; }

  virtual SourceRange getSourceRange() const { return SourceRange(Loc); }

  static bool classof(const Stmt *T) { 
    return T->getStmtClass() == CXXDependentNameExprClass;
  }
  static bool classof(const CXXDependentNameExpr *) { return true; }

  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();

  virtual void EmitImpl(llvm::Serializer& S) const;
  static CXXDependentNameExpr *CreateImpl(llvm::Deserializer& D, ASTContext& C);
};

/// UnaryTypeTraitExpr - A GCC or MS unary type trait, as used in the
/// implementation of TR1/C++0x type trait templates.
/// Example:
/// __is_pod(int) == true
/// __is_enum(std::string) == false
class UnaryTypeTraitExpr : public Expr {
  /// UTT - The trait.
  UnaryTypeTrait UTT;

  /// Loc - The location of the type trait keyword.
  SourceLocation Loc;

  /// RParen - The location of the closing paren.
  SourceLocation RParen;

  /// QueriedType - The type we're testing.
  QualType QueriedType;

public:
  UnaryTypeTraitExpr(SourceLocation loc, UnaryTypeTrait utt, QualType queried,
                     SourceLocation rparen, QualType ty)
    : Expr(UnaryTypeTraitExprClass, ty, false, queried->isDependentType()),
      UTT(utt), Loc(loc), RParen(rparen), QueriedType(queried) { }

  virtual SourceRange getSourceRange() const { return SourceRange(Loc, RParen);}

  UnaryTypeTrait getTrait() const { return UTT; }

  QualType getQueriedType() const { return QueriedType; }

  bool Evaluate() const;

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == UnaryTypeTraitExprClass;
  }
  static bool classof(const UnaryTypeTraitExpr *) { return true; }

  // Iterators
  virtual child_iterator child_begin();
  virtual child_iterator child_end();

  virtual void EmitImpl(llvm::Serializer& S) const;
  static UnaryTypeTraitExpr *CreateImpl(llvm::Deserializer& D, ASTContext& C);
};

/// QualifiedDeclRefExpr - A reference to a declared variable,
/// function, enum, etc., that includes a qualification, e.g.,
/// "N::foo".
class QualifiedDeclRefExpr : public DeclRefExpr {
  /// NestedNameLoc - The location of the beginning of the
  /// nested-name-specifier that qualifies this declaration.
  SourceLocation NestedNameLoc;

public:
  QualifiedDeclRefExpr(NamedDecl *d, QualType t, SourceLocation l, bool TD, 
                       bool VD, SourceLocation nnl)
    : DeclRefExpr(QualifiedDeclRefExprClass, d, t, l, TD, VD), 
      NestedNameLoc(nnl) { }

  virtual SourceRange getSourceRange() const { 
    return SourceRange(NestedNameLoc, getLocation()); 
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == QualifiedDeclRefExprClass;
  }
  static bool classof(const QualifiedDeclRefExpr *) { return true; }

  virtual void EmitImpl(llvm::Serializer& S) const;
  static QualifiedDeclRefExpr* CreateImpl(llvm::Deserializer& D, ASTContext& C);
};

}  // end namespace clang

#endif
