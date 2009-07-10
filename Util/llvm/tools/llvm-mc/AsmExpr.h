//===- AsmExpr.h - Assembly file expressions --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef ASMEXPR_H
#define ASMEXPR_H

#include "llvm/Support/Casting.h"
#include "llvm/Support/DataTypes.h"

namespace llvm {
class MCContext;
class MCSymbol;
class MCValue;

/// AsmExpr - Base class for the full range of assembler expressions which are
/// needed for parsing.  
class AsmExpr {
public:
  enum AsmExprKind {
    Binary,    ///< Binary expressions.
    Constant,  ///< Constant expressions.
    SymbolRef, ///< References to labels and assigned expressions.
    Unary      ///< Unary expressions.
  };
  
private:
  AsmExprKind Kind;
  
protected:
  AsmExpr(AsmExprKind _Kind) : Kind(_Kind) {}
  
public:
  virtual ~AsmExpr();

  AsmExprKind getKind() const { return Kind; }

  /// EvaluateAsAbsolute - Try to evaluate the expression to an absolute value.
  ///
  /// @param Res - The absolute value, if evaluation succeeds.
  /// @result - True on success.
  bool EvaluateAsAbsolute(MCContext &Ctx, int64_t &Res) const;

  /// EvaluateAsRelocatable - Try to evaluate the expression to a relocatable
  /// value, i.e. an expression of the fixed form (a - b + constant).
  ///
  /// @param Res - The relocatable value, if evaluation succeeds.
  /// @result - True on success.
  bool EvaluateAsRelocatable(MCContext &Ctx, MCValue &Res) const;

  static bool classof(const AsmExpr *) { return true; }
};

//// AsmConstantExpr - Represent a constant integer expression.
class AsmConstantExpr : public AsmExpr {
  int64_t Value;

public:
  AsmConstantExpr(int64_t _Value) 
    : AsmExpr(AsmExpr::Constant), Value(_Value) {}
  
  int64_t getValue() const { return Value; }

  static bool classof(const AsmExpr *E) { 
    return E->getKind() == AsmExpr::Constant; 
  }
  static bool classof(const AsmConstantExpr *) { return true; }
};

/// AsmSymbolRefExpr - Represent a reference to a symbol from inside an
/// expression.
///
/// A symbol reference in an expression may be a use of a label, a use of an
/// assembler variable (defined constant), or constitute an implicit definition
/// of the symbol as external.
class AsmSymbolRefExpr : public AsmExpr {
  MCSymbol *Symbol;

public:
  AsmSymbolRefExpr(MCSymbol *_Symbol) 
    : AsmExpr(AsmExpr::SymbolRef), Symbol(_Symbol) {}
  
  MCSymbol *getSymbol() const { return Symbol; }

  static bool classof(const AsmExpr *E) { 
    return E->getKind() == AsmExpr::SymbolRef; 
  }
  static bool classof(const AsmSymbolRefExpr *) { return true; }
};

/// AsmUnaryExpr - Unary assembler expressions.
class AsmUnaryExpr : public AsmExpr {
public:
  enum Opcode {
    LNot,  ///< Logical negation.
    Minus, ///< Unary minus.
    Not,   ///< Bitwise negation.
    Plus   ///< Unary plus.
  };

private:
  Opcode Op;
  AsmExpr *Expr;

public:
  AsmUnaryExpr(Opcode _Op, AsmExpr *_Expr)
    : AsmExpr(AsmExpr::Unary), Op(_Op), Expr(_Expr) {}
  ~AsmUnaryExpr() {
    delete Expr;
  }

  Opcode getOpcode() const { return Op; }

  AsmExpr *getSubExpr() const { return Expr; }

  static bool classof(const AsmExpr *E) { 
    return E->getKind() == AsmExpr::Unary; 
  }
  static bool classof(const AsmUnaryExpr *) { return true; }
};

/// AsmBinaryExpr - Binary assembler expressions.
class AsmBinaryExpr : public AsmExpr {
public:
  enum Opcode {
    Add,  ///< Addition.
    And,  ///< Bitwise and.
    Div,  ///< Division.
    EQ,   ///< Equality comparison.
    GT,   ///< Greater than comparison.
    GTE,  ///< Greater than or equal comparison.
    LAnd, ///< Logical and.
    LOr,  ///< Logical or.
    LT,   ///< Less than comparison.
    LTE,  ///< Less than or equal comparison.
    Mod,  ///< Modulus.
    Mul,  ///< Multiplication.
    NE,   ///< Inequality comparison.
    Or,   ///< Bitwise or.
    Shl,  ///< Bitwise shift left.
    Shr,  ///< Bitwise shift right.
    Sub,  ///< Subtraction.
    Xor   ///< Bitwise exclusive or.
  };

private:
  Opcode Op;
  AsmExpr *LHS, *RHS;

public:
  AsmBinaryExpr(Opcode _Op, AsmExpr *_LHS, AsmExpr *_RHS)
    : AsmExpr(AsmExpr::Binary), Op(_Op), LHS(_LHS), RHS(_RHS) {}
  ~AsmBinaryExpr() {
    delete LHS;
    delete RHS;
  }

  Opcode getOpcode() const { return Op; }

  /// getLHS - Get the left-hand side expression of the binary operator.
  AsmExpr *getLHS() const { return LHS; }

  /// getRHS - Get the right-hand side expression of the binary operator.
  AsmExpr *getRHS() const { return RHS; }

  static bool classof(const AsmExpr *E) { 
    return E->getKind() == AsmExpr::Binary; 
  }
  static bool classof(const AsmBinaryExpr *) { return true; }
};

} // end namespace llvm

#endif
