//===--- Action.h - Parser Action Interface ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Action and EmptyAction interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_PARSE_ACTION_H
#define LLVM_CLANG_PARSE_ACTION_H

#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/TypeTraits.h"
#include "clang/Parse/AccessSpecifier.h"
#include "clang/Parse/Ownership.h"

namespace clang {
  // Semantic.
  class DeclSpec;
  class ObjCDeclSpec;
  class CXXScopeSpec;
  class Declarator;
  class AttributeList;
  struct FieldDeclarator;
  // Parse.
  class Scope;
  class Action;
  class Selector;
  class Designation;
  class InitListDesignations;
  // Lex.
  class Preprocessor;
  class Token;

  // We can re-use the low bit of expression, statement, base, and
  // member-initializer pointers for the "invalid" flag of
  // ActionResult.
  template<> struct IsResultPtrLowBitFree<0> { static const bool value = true; };
  template<> struct IsResultPtrLowBitFree<1> { static const bool value = true; };
  template<> struct IsResultPtrLowBitFree<3> { static const bool value = true; };
  template<> struct IsResultPtrLowBitFree<4> { static const bool value = true; };

/// Action - As the parser reads the input file and recognizes the productions
/// of the grammar, it invokes methods on this class to turn the parsed input
/// into something useful: e.g. a parse tree.
///
/// The callback methods that this class provides are phrased as actions that
/// the parser has just done or is about to do when the method is called.  They
/// are not requests that the actions module do the specified action.
///
/// All of the methods here are optional except getTypeName() and
/// isCurrentClassName(), which must be specified in order for the
/// parse to complete accurately.  The MinimalAction class does this
/// bare-minimum of tracking to implement this functionality.
class Action : public ActionBase {
public:
  /// Out-of-line virtual destructor to provide home for this class.
  virtual ~Action();

  // Types - Though these don't actually enforce strong typing, they document
  // what types are required to be identical for the actions.
  typedef ActionBase::ExprTy ExprTy;
  typedef ActionBase::StmtTy StmtTy;
  typedef void DeclTy;
  typedef void TypeTy;
  typedef void AttrTy;
  typedef void BaseTy;
  typedef void MemInitTy;
  typedef void CXXScopeTy;
  typedef void TemplateParamsTy;
  typedef void TemplateArgTy;

  /// Expr/Stmt/Type/BaseResult - Provide a unique type to wrap
  /// ExprTy/StmtTy/TypeTy/BaseTy, providing strong typing and
  /// allowing for failure.
  typedef ActionResult<0> ExprResult;
  typedef ActionResult<1> StmtResult;
  typedef ActionResult<2> TypeResult;
  typedef ActionResult<3> BaseResult;
  typedef ActionResult<4> MemInitResult;

  /// Same, but with ownership.
  typedef ASTOwningResult<&ActionBase::DeleteExpr> OwningExprResult;
  typedef ASTOwningResult<&ActionBase::DeleteStmt> OwningStmtResult;
  typedef ASTOwningResult<&ActionBase::DeleteTemplateArg> 
    OwningTemplateArgResult;
  // Note that these will replace ExprResult and StmtResult when the transition
  // is complete.

  /// Single expressions or statements as arguments.
  typedef ASTOwningPtr<&ActionBase::DeleteExpr> ExprArg;
  typedef ASTOwningPtr<&ActionBase::DeleteStmt> StmtArg;
  typedef ASTOwningPtr<&ActionBase::DeleteTemplateArg> TemplateArgArg;

  /// Multiple expressions or statements as arguments.
  typedef ASTMultiPtr<&ActionBase::DeleteExpr> MultiExprArg;
  typedef ASTMultiPtr<&ActionBase::DeleteStmt> MultiStmtArg;
  typedef ASTMultiPtr<&ActionBase::DeleteTemplateParams> MultiTemplateParamsArg;
  typedef ASTMultiPtr<&ActionBase::DeleteTemplateArg> MultiTemplateArgArg;

  // Utilities for Action implementations to return smart results.

  OwningExprResult ExprError() { return OwningExprResult(*this, true); }
  OwningStmtResult StmtError() { return OwningStmtResult(*this, true); }
  OwningTemplateArgResult TemplateArgError() { 
    return OwningTemplateArgResult(*this, true); 
  }

  OwningExprResult ExprError(const DiagnosticBuilder&) { return ExprError(); }
  OwningStmtResult StmtError(const DiagnosticBuilder&) { return StmtError(); }
  OwningTemplateArgResult TemplateArgError(const DiagnosticBuilder&) {
    return TemplateArgError();
  }

  OwningExprResult ExprEmpty() { return OwningExprResult(*this, false); }
  OwningStmtResult StmtEmpty() { return OwningStmtResult(*this, false); }
  OwningTemplateArgResult TemplateArgEmpty() { 
    return OwningTemplateArgResult(*this, false); 
  }

  /// Statistics.
  virtual void PrintStats() const {}
  //===--------------------------------------------------------------------===//
  // Declaration Tracking Callbacks.
  //===--------------------------------------------------------------------===//
  
  /// getTypeName - Return non-null if the specified identifier is a type name
  /// in the current scope.
  /// An optional CXXScopeSpec can be passed to indicate the C++ scope (class or
  /// namespace) that the identifier must be a member of.
  /// i.e. for "foo::bar", 'II' will be "bar" and 'SS' will be "foo::".
  virtual TypeTy *getTypeName(IdentifierInfo &II, Scope *S,
                             const CXXScopeSpec *SS = 0) = 0;

  /// isCurrentClassName - Return true if the specified name is the
  /// name of the innermost C++ class type currently being defined.
  virtual bool isCurrentClassName(const IdentifierInfo &II, Scope *S,
                                  const CXXScopeSpec *SS = 0) = 0;

  /// isTemplateName - Determines whether the identifier II is a
  /// template name in the current scope, and returns the template
  /// declaration if II names a template. An optional CXXScope can be
  /// passed to indicate the C++ scope in which the identifier will be
  /// found. 
  virtual DeclTy *isTemplateName(IdentifierInfo &II, Scope *S,
                                 const CXXScopeSpec *SS = 0) = 0;

  /// ActOnCXXGlobalScopeSpecifier - Return the object that represents the
  /// global scope ('::').
  virtual CXXScopeTy *ActOnCXXGlobalScopeSpecifier(Scope *S,
                                                   SourceLocation CCLoc) {
    return 0;
  }

  /// ActOnCXXNestedNameSpecifier - Called during parsing of a
  /// nested-name-specifier. e.g. for "foo::bar::" we parsed "foo::" and now
  /// we want to resolve "bar::". 'SS' is empty or the previously parsed
  /// nested-name part ("foo::"), 'IdLoc' is the source location of 'bar',
  /// 'CCLoc' is the location of '::' and 'II' is the identifier for 'bar'.
  /// Returns a CXXScopeTy* object representing the C++ scope.
  virtual CXXScopeTy *ActOnCXXNestedNameSpecifier(Scope *S,
                                                  const CXXScopeSpec &SS,
                                                  SourceLocation IdLoc,
                                                  SourceLocation CCLoc,
                                                  IdentifierInfo &II) {
    return 0;
  }

  /// ActOnCXXEnterDeclaratorScope - Called when a C++ scope specifier (global
  /// scope or nested-name-specifier) is parsed, part of a declarator-id.
  /// After this method is called, according to [C++ 3.4.3p3], names should be
  /// looked up in the declarator-id's scope, until the declarator is parsed and
  /// ActOnCXXExitDeclaratorScope is called.
  /// The 'SS' should be a non-empty valid CXXScopeSpec.
  virtual void ActOnCXXEnterDeclaratorScope(Scope *S, const CXXScopeSpec &SS) {
  }

  /// ActOnCXXExitDeclaratorScope - Called when a declarator that previously
  /// invoked ActOnCXXEnterDeclaratorScope(), is finished. 'SS' is the same
  /// CXXScopeSpec that was passed to ActOnCXXEnterDeclaratorScope as well.
  /// Used to indicate that names should revert to being looked up in the
  /// defining scope.
  virtual void ActOnCXXExitDeclaratorScope(Scope *S, const CXXScopeSpec &SS) {
  }

  /// ActOnDeclarator - This callback is invoked when a declarator is parsed and
  /// 'Init' specifies the initializer if any.  This is for things like:
  /// "int X = 4" or "typedef int foo".
  ///
  /// LastInGroup is non-null for cases where one declspec has multiple
  /// declarators on it.  For example in 'int A, B', ActOnDeclarator will be
  /// called with LastInGroup=A when invoked for B.
  virtual DeclTy *ActOnDeclarator(Scope *S, Declarator &D,DeclTy *LastInGroup) {
    return 0;
  }

  /// ActOnParamDeclarator - This callback is invoked when a parameter
  /// declarator is parsed. This callback only occurs for functions
  /// with prototypes. S is the function prototype scope for the
  /// parameters (C++ [basic.scope.proto]).
  virtual DeclTy *ActOnParamDeclarator(Scope *S, Declarator &D) {
    return 0;
  }

  /// AddInitializerToDecl - This action is called immediately after 
  /// ActOnDeclarator (when an initializer is present). The code is factored 
  /// this way to make sure we are able to handle the following:
  ///   void func() { int xx = xx; }
  /// This allows ActOnDeclarator to register "xx" prior to parsing the
  /// initializer. The declaration above should still result in a warning, 
  /// since the reference to "xx" is uninitialized.
  virtual void AddInitializerToDecl(DeclTy *Dcl, ExprArg Init) {
    return;
  }

  /// ActOnUninitializedDecl - This action is called immediately after
  /// ActOnDeclarator (when an initializer is *not* present).
  virtual void ActOnUninitializedDecl(DeclTy *Dcl) {
    return;
  }

  /// FinalizeDeclaratorGroup - After a sequence of declarators are parsed, this
  /// gives the actions implementation a chance to process the group as a whole.
  virtual DeclTy *FinalizeDeclaratorGroup(Scope *S, DeclTy *Group) {
    return Group;
  }

  /// @brief Indicates that all K&R-style parameter declarations have
  /// been parsed prior to a function definition.
  /// @param S  The function prototype scope.
  /// @param D  The function declarator.
  virtual void ActOnFinishKNRParamDeclarations(Scope *S, Declarator &D) {
  }

  /// ActOnStartOfFunctionDef - This is called at the start of a function
  /// definition, instead of calling ActOnDeclarator.  The Declarator includes
  /// information about formal arguments that are part of this function.
  virtual DeclTy *ActOnStartOfFunctionDef(Scope *FnBodyScope, Declarator &D) {
    // Default to ActOnDeclarator.
    return ActOnStartOfFunctionDef(FnBodyScope,
                                   ActOnDeclarator(FnBodyScope, D, 0));
  }

  /// ActOnStartOfFunctionDef - This is called at the start of a function
  /// definition, after the FunctionDecl has already been created.
  virtual DeclTy *ActOnStartOfFunctionDef(Scope *FnBodyScope, DeclTy *D) {
    return D;
  }

  virtual void ObjCActOnStartOfMethodDef(Scope *FnBodyScope, DeclTy *D) {
    return;
  }

  /// ActOnFinishFunctionBody - This is called when a function body has completed
  /// parsing.  Decl is the DeclTy returned by ParseStartOfFunctionDef.
  virtual DeclTy *ActOnFinishFunctionBody(DeclTy *Decl, StmtArg Body) {
    return Decl;
  }

  virtual DeclTy *ActOnFileScopeAsmDecl(SourceLocation Loc, ExprArg AsmString) {
    return 0;
  }
  
  /// ActOnPopScope - This callback is called immediately before the specified
  /// scope is popped and deleted.
  virtual void ActOnPopScope(SourceLocation Loc, Scope *S) {}

  /// ActOnTranslationUnitScope - This callback is called once, immediately
  /// after creating the translation unit scope (in Parser::Initialize).
  virtual void ActOnTranslationUnitScope(SourceLocation Loc, Scope *S) {}
    
  /// ParsedFreeStandingDeclSpec - This method is invoked when a declspec with
  /// no declarator (e.g. "struct foo;") is parsed.
  virtual DeclTy *ParsedFreeStandingDeclSpec(Scope *S, DeclSpec &DS) {
    return 0;
  }

  /// ActOnStartLinkageSpecification - Parsed the beginning of a C++
  /// linkage specification, including the language and (if present)
  /// the '{'. ExternLoc is the location of the 'extern', LangLoc is
  /// the location of the language string literal, which is provided
  /// by Lang/StrSize. LBraceLoc, if valid, provides the location of
  /// the '{' brace. Otherwise, this linkage specification does not
  /// have any braces.
  virtual DeclTy *ActOnStartLinkageSpecification(Scope *S,
                                                 SourceLocation ExternLoc,
                                                 SourceLocation LangLoc,
                                                 const char *Lang,
                                                 unsigned StrSize,
                                                 SourceLocation LBraceLoc) {
    return 0;
  }

  /// ActOnFinishLinkageSpecification - Completely the definition of
  /// the C++ linkage specification LinkageSpec. If RBraceLoc is
  /// valid, it's the position of the closing '}' brace in a linkage
  /// specification that uses braces.
  virtual DeclTy *ActOnFinishLinkageSpecification(Scope *S,
                                                  DeclTy *LinkageSpec,
                                                  SourceLocation RBraceLoc) {
    return LinkageSpec;
  }

  /// ActOnEndOfTranslationUnit - This is called at the very end of the
  /// translation unit when EOF is reached and all but the top-level scope is
  /// popped.
  virtual void ActOnEndOfTranslationUnit() {}
  
  //===--------------------------------------------------------------------===//
  // Type Parsing Callbacks.
  //===--------------------------------------------------------------------===//

  /// ActOnTypeName - A type-name (type-id in C++) was parsed.
  virtual TypeResult ActOnTypeName(Scope *S, Declarator &D) {
    return 0;
  }
  
  enum TagKind {
    TK_Reference,   // Reference to a tag:  'struct foo *X;'
    TK_Declaration, // Fwd decl of a tag:   'struct foo;'
    TK_Definition   // Definition of a tag: 'struct foo { int X; } Y;'
  };
  virtual DeclTy *ActOnTag(Scope *S, unsigned TagSpec, TagKind TK,
                           SourceLocation KWLoc, const CXXScopeSpec &SS,
                           IdentifierInfo *Name, SourceLocation NameLoc,
                           AttributeList *Attr,
                           MultiTemplateParamsArg TemplateParameterLists) {
    // TagType is an instance of DeclSpec::TST, indicating what kind of tag this
    // is (struct/union/enum/class).
    return 0;
  }
  
  /// Act on @defs() element found when parsing a structure.  ClassName is the
  /// name of the referenced class.   
  virtual void ActOnDefs(Scope *S, DeclTy *TagD, SourceLocation DeclStart,
                         IdentifierInfo *ClassName,
                         llvm::SmallVectorImpl<DeclTy*> &Decls) {}
  virtual DeclTy *ActOnField(Scope *S, DeclTy *TagD, SourceLocation DeclStart,
                             Declarator &D, ExprTy *BitfieldWidth) {
    return 0;
  }
  
  virtual DeclTy *ActOnIvar(Scope *S, SourceLocation DeclStart,
                            Declarator &D, ExprTy *BitfieldWidth,
                            tok::ObjCKeywordKind visibility) {
    return 0;
  }
  
  virtual void ActOnFields(Scope* S, SourceLocation RecLoc, DeclTy *TagDecl,
                           DeclTy **Fields, unsigned NumFields, 
                           SourceLocation LBrac, SourceLocation RBrac,
                           AttributeList *AttrList) {}
  
  /// ActOnTagStartDefinition - Invoked when we have entered the
  /// scope of a tag's definition (e.g., for an enumeration, class,
  /// struct, or union).
  virtual void ActOnTagStartDefinition(Scope *S, DeclTy *TagDecl) { }

  /// ActOnTagFinishDefinition - Invoked once we have finished parsing
  /// the definition of a tag (enumeration, class, struct, or union).
  virtual void ActOnTagFinishDefinition(Scope *S, DeclTy *TagDecl) { }

  virtual DeclTy *ActOnEnumConstant(Scope *S, DeclTy *EnumDecl,
                                    DeclTy *LastEnumConstant,
                                    SourceLocation IdLoc, IdentifierInfo *Id,
                                    SourceLocation EqualLoc, ExprTy *Val) {
    return 0;
  }
  virtual void ActOnEnumBody(SourceLocation EnumLoc, DeclTy *EnumDecl,
                             DeclTy **Elements, unsigned NumElements) {}

  //===--------------------------------------------------------------------===//
  // Statement Parsing Callbacks.
  //===--------------------------------------------------------------------===//

  virtual OwningStmtResult ActOnNullStmt(SourceLocation SemiLoc) {
    return StmtEmpty();
  }

  virtual OwningStmtResult ActOnCompoundStmt(SourceLocation L, SourceLocation R,
                                             MultiStmtArg Elts,
                                             bool isStmtExpr) {
    return StmtEmpty();
  }
  virtual OwningStmtResult ActOnDeclStmt(DeclTy *Decl, SourceLocation StartLoc,
                                   SourceLocation EndLoc) {
    return StmtEmpty();
  }

  virtual OwningStmtResult ActOnExprStmt(ExprArg Expr) {
    return OwningStmtResult(*this, Expr.release());
  }

  /// ActOnCaseStmt - Note that this handles the GNU 'case 1 ... 4' extension,
  /// which can specify an RHS value.
  virtual OwningStmtResult ActOnCaseStmt(SourceLocation CaseLoc, ExprArg LHSVal,
                                    SourceLocation DotDotDotLoc, ExprArg RHSVal,
                                    SourceLocation ColonLoc, StmtArg SubStmt) {
    return StmtEmpty();
  }
  virtual OwningStmtResult ActOnDefaultStmt(SourceLocation DefaultLoc,
                                            SourceLocation ColonLoc,
                                            StmtArg SubStmt, Scope *CurScope){
    return StmtEmpty();
  }

  virtual OwningStmtResult ActOnLabelStmt(SourceLocation IdentLoc,
                                          IdentifierInfo *II,
                                          SourceLocation ColonLoc,
                                          StmtArg SubStmt) {
    return StmtEmpty();
  }

  virtual OwningStmtResult ActOnIfStmt(SourceLocation IfLoc, ExprArg CondVal,
                                       StmtArg ThenVal, SourceLocation ElseLoc,
                                       StmtArg ElseVal) {
    return StmtEmpty();
  }

  virtual OwningStmtResult ActOnStartOfSwitchStmt(ExprArg Cond) {
    return StmtEmpty();
  }

  virtual OwningStmtResult ActOnFinishSwitchStmt(SourceLocation SwitchLoc,
                                                 StmtArg Switch, StmtArg Body) {
    return StmtEmpty();
  }

  virtual OwningStmtResult ActOnWhileStmt(SourceLocation WhileLoc, ExprArg Cond,
                                          StmtArg Body) {
    return StmtEmpty();
  }
  virtual OwningStmtResult ActOnDoStmt(SourceLocation DoLoc, StmtArg Body,
                                       SourceLocation WhileLoc, ExprArg Cond) {
    return StmtEmpty();
  }
  virtual OwningStmtResult ActOnForStmt(SourceLocation ForLoc,
                                        SourceLocation LParenLoc,
                                        StmtArg First, ExprArg Second,
                                        ExprArg Third, SourceLocation RParenLoc,
                                        StmtArg Body) {
    return StmtEmpty();
  }
  virtual OwningStmtResult ActOnObjCForCollectionStmt(SourceLocation ForColLoc,
                                       SourceLocation LParenLoc,
                                       StmtArg First, ExprArg Second,
                                       SourceLocation RParenLoc, StmtArg Body) {
    return StmtEmpty();
  }
  virtual OwningStmtResult ActOnGotoStmt(SourceLocation GotoLoc,
                                         SourceLocation LabelLoc,
                                         IdentifierInfo *LabelII) {
    return StmtEmpty();
  }
  virtual OwningStmtResult ActOnIndirectGotoStmt(SourceLocation GotoLoc,
                                                 SourceLocation StarLoc,
                                                 ExprArg DestExp) {
    return StmtEmpty();
  }
  virtual OwningStmtResult ActOnContinueStmt(SourceLocation ContinueLoc,
                                             Scope *CurScope) {
    return StmtEmpty();
  }
  virtual OwningStmtResult ActOnBreakStmt(SourceLocation GotoLoc,
                                          Scope *CurScope) {
    return StmtEmpty();
  }
  virtual OwningStmtResult ActOnReturnStmt(SourceLocation ReturnLoc,
                                           ExprArg RetValExp) {
    return StmtEmpty();
  }
  virtual OwningStmtResult ActOnAsmStmt(SourceLocation AsmLoc,
                                        bool IsSimple,                                  
                                        bool IsVolatile,
                                        unsigned NumOutputs,
                                        unsigned NumInputs,
                                        std::string *Names,
                                        MultiExprArg Constraints,
                                        MultiExprArg Exprs,
                                        ExprArg AsmString,
                                        MultiExprArg Clobbers,
                                        SourceLocation RParenLoc) {
    return StmtEmpty();
  }

  // Objective-c statements
  virtual OwningStmtResult ActOnObjCAtCatchStmt(SourceLocation AtLoc,
                                                SourceLocation RParen,
                                                StmtArg Parm, StmtArg Body,
                                                StmtArg CatchList) {
    return StmtEmpty();
  }

  virtual OwningStmtResult ActOnObjCAtFinallyStmt(SourceLocation AtLoc,
                                                  StmtArg Body) {
    return StmtEmpty();
  }

  virtual OwningStmtResult ActOnObjCAtTryStmt(SourceLocation AtLoc,
                                              StmtArg Try, StmtArg Catch,
                                              StmtArg Finally) {
    return StmtEmpty();
  }

  virtual OwningStmtResult ActOnObjCAtThrowStmt(SourceLocation AtLoc,
                                                ExprArg Throw) {
    return StmtEmpty();
  }

  virtual OwningStmtResult ActOnObjCAtSynchronizedStmt(SourceLocation AtLoc,
                                                       ExprArg SynchExpr,
                                                       StmtArg SynchBody) {
    return StmtEmpty();
  }

  // C++ Statements
  virtual DeclTy *ActOnExceptionDeclarator(Scope *S, Declarator &D) {
    return 0;
  }

  virtual OwningStmtResult ActOnCXXCatchBlock(SourceLocation CatchLoc,
                                              DeclTy *ExceptionDecl,
                                              StmtArg HandlerBlock) {
    return StmtEmpty();
  }

  virtual OwningStmtResult ActOnCXXTryBlock(SourceLocation TryLoc,
                                            StmtArg TryBlock,
                                            MultiStmtArg Handlers) {
    return StmtEmpty();
  }

  //===--------------------------------------------------------------------===//
  // Expression Parsing Callbacks.
  //===--------------------------------------------------------------------===//

  // Primary Expressions.

  /// ActOnIdentifierExpr - Parse an identifier in expression context.
  /// 'HasTrailingLParen' indicates whether or not the identifier has a '('
  /// token immediately after it.
  /// An optional CXXScopeSpec can be passed to indicate the C++ scope (class or
  /// namespace) that the identifier must be a member of.
  /// i.e. for "foo::bar", 'II' will be "bar" and 'SS' will be "foo::".
  virtual OwningExprResult ActOnIdentifierExpr(Scope *S, SourceLocation Loc,
                                               IdentifierInfo &II,
                                               bool HasTrailingLParen,
                                               const CXXScopeSpec *SS = 0) {
    return ExprEmpty();
  }

  /// ActOnOperatorFunctionIdExpr - Parse a C++ overloaded operator
  /// name (e.g., @c operator+ ) as an expression. This is very
  /// similar to ActOnIdentifierExpr, except that instead of providing
  /// an identifier the parser provides the kind of overloaded
  /// operator that was parsed.
  virtual OwningExprResult ActOnCXXOperatorFunctionIdExpr(
                             Scope *S, SourceLocation OperatorLoc,
                             OverloadedOperatorKind Op,
                             bool HasTrailingLParen, const CXXScopeSpec &SS) {
    return ExprEmpty();
  }

  /// ActOnCXXConversionFunctionExpr - Parse a C++ conversion function
  /// name (e.g., @c operator void const *) as an expression. This is
  /// very similar to ActOnIdentifierExpr, except that instead of
  /// providing an identifier the parser provides the type of the
  /// conversion function.
  virtual OwningExprResult ActOnCXXConversionFunctionExpr(
                             Scope *S, SourceLocation OperatorLoc,
                             TypeTy *Type, bool HasTrailingLParen,
                             const CXXScopeSpec &SS) {
    return ExprEmpty();
  }

  virtual OwningExprResult ActOnPredefinedExpr(SourceLocation Loc,
                                               tok::TokenKind Kind) {
    return ExprEmpty();
  }
  virtual OwningExprResult ActOnCharacterConstant(const Token &) {
    return ExprEmpty();
  }
  virtual OwningExprResult ActOnNumericConstant(const Token &) {
    return ExprEmpty();
  }

  /// ActOnStringLiteral - The specified tokens were lexed as pasted string
  /// fragments (e.g. "foo" "bar" L"baz").
  virtual OwningExprResult ActOnStringLiteral(const Token *Toks,
                                              unsigned NumToks) {
    return ExprEmpty();
  }

  virtual OwningExprResult ActOnParenExpr(SourceLocation L, SourceLocation R,
                                          ExprArg Val) {
    return move_res(Val);  // Default impl returns operand.
  }

  // Postfix Expressions.
  virtual OwningExprResult ActOnPostfixUnaryOp(Scope *S, SourceLocation OpLoc,
                                               tok::TokenKind Kind,
                                               ExprArg Input) {
    return ExprEmpty();
  }
  virtual OwningExprResult ActOnArraySubscriptExpr(Scope *S, ExprArg Base,
                                                   SourceLocation LLoc,
                                                   ExprArg Idx,
                                                   SourceLocation RLoc) {
    return ExprEmpty();
  }
  virtual OwningExprResult ActOnMemberReferenceExpr(Scope *S, ExprArg Base,
                                                    SourceLocation OpLoc,
                                                    tok::TokenKind OpKind,
                                                    SourceLocation MemberLoc,
                                                    IdentifierInfo &Member) {
    return ExprEmpty();
  }

  /// ActOnCallExpr - Handle a call to Fn with the specified array of arguments.
  /// This provides the location of the left/right parens and a list of comma
  /// locations.  There are guaranteed to be one fewer commas than arguments,
  /// unless there are zero arguments.
  virtual OwningExprResult ActOnCallExpr(Scope *S, ExprArg Fn,
                                         SourceLocation LParenLoc,
                                         MultiExprArg Args,
                                         SourceLocation *CommaLocs,
                                         SourceLocation RParenLoc) {
    return ExprEmpty();
  }

  // Unary Operators.  'Tok' is the token for the operator.
  virtual OwningExprResult ActOnUnaryOp(Scope *S, SourceLocation OpLoc,
                                        tok::TokenKind Op, ExprArg Input) {
    return ExprEmpty();
  }
  virtual OwningExprResult
    ActOnSizeOfAlignOfExpr(SourceLocation OpLoc, bool isSizeof, bool isType,
                           void *TyOrEx, const SourceRange &ArgRange) {
    return ExprEmpty();
  }

  virtual OwningExprResult ActOnCompoundLiteral(SourceLocation LParen,
                                                TypeTy *Ty,
                                                SourceLocation RParen,
                                                ExprArg Op) {
    return ExprEmpty();
  }
  virtual OwningExprResult ActOnInitList(SourceLocation LParenLoc,
                                         MultiExprArg InitList,
                                         InitListDesignations &Designators,
                                         SourceLocation RParenLoc) {
    return ExprEmpty();
  }
  /// @brief Parsed a C99 designated initializer. 
  /// 
  /// @param Desig Contains the designation with one or more designators.
  ///
  /// @param Loc The location of the '=' or ':' prior to the
  /// initialization expression.
  ///
  /// @param UsedColonSyntax If true, then this designated initializer
  /// used the deprecated GNU syntax @c fieldname:foo rather than the
  /// C99 syntax @c .fieldname=foo.
  ///
  /// @param Init The value that the entity (or entities) described by
  /// the designation will be initialized with.
  virtual OwningExprResult ActOnDesignatedInitializer(Designation &Desig,
                                                      SourceLocation Loc,
                                                      bool UsedColonSyntax,
                                                      OwningExprResult Init) {
    return ExprEmpty();
  }

  virtual OwningExprResult ActOnCastExpr(SourceLocation LParenLoc, TypeTy *Ty,
                                         SourceLocation RParenLoc, ExprArg Op) {
    return ExprEmpty();
  }

  virtual OwningExprResult ActOnBinOp(Scope *S, SourceLocation TokLoc,
                                      tok::TokenKind Kind,
                                      ExprArg LHS, ExprArg RHS) {
    return ExprEmpty();
  }

  /// ActOnConditionalOp - Parse a ?: operation.  Note that 'LHS' may be null
  /// in the case of a the GNU conditional expr extension.
  virtual OwningExprResult ActOnConditionalOp(SourceLocation QuestionLoc,
                                              SourceLocation ColonLoc,
                                              ExprArg Cond, ExprArg LHS,
                                              ExprArg RHS) {
    return ExprEmpty();
  }

  //===---------------------- GNU Extension Expressions -------------------===//

  virtual ExprResult ActOnAddrLabel(SourceLocation OpLoc, SourceLocation LabLoc,
                                    IdentifierInfo *LabelII) { // "&&foo"
    return 0;
  }
  
  virtual ExprResult ActOnStmtExpr(SourceLocation LPLoc, StmtTy *SubStmt,
                                   SourceLocation RPLoc) { // "({..})"
    return 0;
  }
  
  // __builtin_offsetof(type, identifier(.identifier|[expr])*)
  struct OffsetOfComponent {
    SourceLocation LocStart, LocEnd;
    bool isBrackets;  // true if [expr], false if .ident
    union {
      IdentifierInfo *IdentInfo;
      ExprTy *E;
    } U;
  };
  
  virtual ExprResult ActOnBuiltinOffsetOf(Scope *S, SourceLocation BuiltinLoc,
                                          SourceLocation TypeLoc, TypeTy *Arg1,
                                          OffsetOfComponent *CompPtr,
                                          unsigned NumComponents,
                                          SourceLocation RParenLoc) {
    return 0;
  }
  
  // __builtin_types_compatible_p(type1, type2)
  virtual ExprResult ActOnTypesCompatibleExpr(SourceLocation BuiltinLoc, 
                                              TypeTy *arg1, TypeTy *arg2,
                                              SourceLocation RPLoc) {
    return 0;
  }
  // __builtin_choose_expr(constExpr, expr1, expr2)
  virtual ExprResult ActOnChooseExpr(SourceLocation BuiltinLoc, 
                                     ExprTy *cond, ExprTy *expr1, ExprTy *expr2,
                                     SourceLocation RPLoc) {
    return 0;
  }
  // __builtin_overload(...)
  virtual ExprResult ActOnOverloadExpr(ExprTy **Args, unsigned NumArgs,
                                       SourceLocation *CommaLocs,
                                       SourceLocation BuiltinLoc, 
                                       SourceLocation RPLoc) {
    return 0;
  }
  

  // __builtin_va_arg(expr, type)
  virtual ExprResult ActOnVAArg(SourceLocation BuiltinLoc,
                                ExprTy *expr, TypeTy *type,
                                SourceLocation RPLoc) {
    return 0;
  }

  /// ActOnGNUNullExpr - Parsed the GNU __null expression, the token
  /// for which is at position TokenLoc.
  virtual ExprResult ActOnGNUNullExpr(SourceLocation TokenLoc) {
    return 0;
  }

  //===------------------------- "Block" Extension ------------------------===//

  /// ActOnBlockStart - This callback is invoked when a block literal is
  /// started.  The result pointer is passed into the block finalizers.
  virtual void ActOnBlockStart(SourceLocation CaretLoc, Scope *CurScope) {}

  /// ActOnBlockArguments - This callback allows processing of block arguments.
  /// If there are no arguments, this is still invoked.
  virtual void ActOnBlockArguments(Declarator &ParamInfo) {}
  
  /// ActOnBlockError - If there is an error parsing a block, this callback
  /// is invoked to pop the information about the block from the action impl.
  virtual void ActOnBlockError(SourceLocation CaretLoc, Scope *CurScope) {}
  
  /// ActOnBlockStmtExpr - This is called when the body of a block statement
  /// literal was successfully completed.  ^(int x){...}
  virtual ExprResult ActOnBlockStmtExpr(SourceLocation CaretLoc, StmtTy *Body,
                                        Scope *CurScope) { return 0; }

  //===------------------------- C++ Declarations -------------------------===//

  /// ActOnStartNamespaceDef - This is called at the start of a namespace
  /// definition.
  virtual DeclTy *ActOnStartNamespaceDef(Scope *S, SourceLocation IdentLoc,
                                        IdentifierInfo *Ident,
                                        SourceLocation LBrace) {
    return 0;
  }

  /// ActOnFinishNamespaceDef - This callback is called after a namespace is
  /// exited. Decl is the DeclTy returned by ActOnStartNamespaceDef.
  virtual void ActOnFinishNamespaceDef(DeclTy *Dcl,SourceLocation RBrace) {
    return;
  }

  /// ActOnUsingDirective - This is called when using-directive is parsed.
  virtual DeclTy *ActOnUsingDirective(Scope *CurScope,
                                      SourceLocation UsingLoc,
                                      SourceLocation NamespcLoc,
                                      const CXXScopeSpec &SS,
                                      SourceLocation IdentLoc,
                                      IdentifierInfo *NamespcName,
                                      AttributeList *AttrList);

  /// ActOnParamDefaultArgument - Parse default argument for function parameter
  virtual void ActOnParamDefaultArgument(DeclTy *param,
                                         SourceLocation EqualLoc,
                                         ExprTy *defarg) {
  }

  /// ActOnParamUnparsedDefaultArgument - We've seen a default
  /// argument for a function parameter, but we can't parse it yet
  /// because we're inside a class definition. Note that this default
  /// argument will be parsed later.
  virtual void ActOnParamUnparsedDefaultArgument(DeclTy *param, 
                                                 SourceLocation EqualLoc) { }

  /// ActOnParamDefaultArgumentError - Parsing or semantic analysis of
  /// the default argument for the parameter param failed.
  virtual void ActOnParamDefaultArgumentError(DeclTy *param) { }

  /// AddCXXDirectInitializerToDecl - This action is called immediately after 
  /// ActOnDeclarator, when a C++ direct initializer is present.
  /// e.g: "int x(1);"
  virtual void AddCXXDirectInitializerToDecl(DeclTy *Dcl,
                                             SourceLocation LParenLoc,
                                             ExprTy **Exprs, unsigned NumExprs,
                                             SourceLocation *CommaLocs,
                                             SourceLocation RParenLoc) {
    return;
  }
  
  /// ActOnStartDelayedCXXMethodDeclaration - We have completed
  /// parsing a top-level (non-nested) C++ class, and we are now
  /// parsing those parts of the given Method declaration that could
  /// not be parsed earlier (C++ [class.mem]p2), such as default
  /// arguments. This action should enter the scope of the given
  /// Method declaration as if we had just parsed the qualified method
  /// name. However, it should not bring the parameters into scope;
  /// that will be performed by ActOnDelayedCXXMethodParameter.
  virtual void ActOnStartDelayedCXXMethodDeclaration(Scope *S, DeclTy *Method) {
  }

  /// ActOnDelayedCXXMethodParameter - We've already started a delayed
  /// C++ method declaration. We're (re-)introducing the given
  /// function parameter into scope for use in parsing later parts of
  /// the method declaration. For example, we could see an
  /// ActOnParamDefaultArgument event for this parameter.
  virtual void ActOnDelayedCXXMethodParameter(Scope *S, DeclTy *Param) {
  }

  /// ActOnFinishDelayedCXXMethodDeclaration - We have finished
  /// processing the delayed method declaration for Method. The method
  /// declaration is now considered finished. There may be a separate
  /// ActOnStartOfFunctionDef action later (not necessarily
  /// immediately!) for this method, if it was also defined inside the
  /// class body.
  virtual void ActOnFinishDelayedCXXMethodDeclaration(Scope *S, DeclTy *Method) {
  }

  //===------------------------- C++ Expressions --------------------------===//
  
  /// ActOnCXXNamedCast - Parse {dynamic,static,reinterpret,const}_cast's.
  virtual ExprResult ActOnCXXNamedCast(SourceLocation OpLoc, tok::TokenKind Kind,
                                       SourceLocation LAngleBracketLoc, TypeTy *Ty,
                                       SourceLocation RAngleBracketLoc,
                                       SourceLocation LParenLoc, ExprTy *Op,
                                       SourceLocation RParenLoc) {
    return 0;
  }

  /// ActOnCXXTypeidOfType - Parse typeid( type-id ).
  virtual ExprResult ActOnCXXTypeid(SourceLocation OpLoc,
                                    SourceLocation LParenLoc, bool isType,
                                    void *TyOrExpr, SourceLocation RParenLoc) {
    return 0;
  }

  /// ActOnCXXThis - Parse the C++ 'this' pointer.
  virtual ExprResult ActOnCXXThis(SourceLocation ThisLoc) {
    return 0;
  }

  /// ActOnCXXBoolLiteral - Parse {true,false} literals.
  virtual ExprResult ActOnCXXBoolLiteral(SourceLocation OpLoc,
                                         tok::TokenKind Kind) {
    return 0;
  }

  /// ActOnCXXThrow - Parse throw expressions.
  virtual ExprResult ActOnCXXThrow(SourceLocation OpLoc,
                                   ExprTy *Op = 0) {
    return 0;
  }

  /// ActOnCXXTypeConstructExpr - Parse construction of a specified type.
  /// Can be interpreted either as function-style casting ("int(x)")
  /// or class type construction ("ClassType(x,y,z)")
  /// or creation of a value-initialized type ("int()").
  virtual ExprResult ActOnCXXTypeConstructExpr(SourceRange TypeRange,
                                               TypeTy *TypeRep,
                                               SourceLocation LParenLoc,
                                               ExprTy **Exprs,
                                               unsigned NumExprs,
                                               SourceLocation *CommaLocs,
                                               SourceLocation RParenLoc) {
    return 0;
  }

  /// ActOnCXXConditionDeclarationExpr - Parsed a condition declaration of a
  /// C++ if/switch/while/for statement.
  /// e.g: "if (int x = f()) {...}"
  virtual ExprResult ActOnCXXConditionDeclarationExpr(Scope *S,
                                                      SourceLocation StartLoc,
                                                      Declarator &D,
                                                      SourceLocation EqualLoc,
                                                      ExprTy *AssignExprVal) {
    return 0;
  }

  /// ActOnCXXNew - Parsed a C++ 'new' expression. UseGlobal is true if the
  /// new was qualified (::new). In a full new like
  /// @code new (p1, p2) type(c1, c2) @endcode
  /// the p1 and p2 expressions will be in PlacementArgs and the c1 and c2
  /// expressions in ConstructorArgs. The type is passed as a declarator.
  virtual ExprResult ActOnCXXNew(SourceLocation StartLoc, bool UseGlobal,
                                 SourceLocation PlacementLParen,
                                 ExprTy **PlacementArgs, unsigned NumPlaceArgs,
                                 SourceLocation PlacementRParen,
                                 bool ParenTypeId, Declarator &D,
                                 SourceLocation ConstructorLParen,
                                 ExprTy **ConstructorArgs, unsigned NumConsArgs,
                                 SourceLocation ConstructorRParen) {
    return 0;
  }

  /// ActOnCXXDelete - Parsed a C++ 'delete' expression. UseGlobal is true if
  /// the delete was qualified (::delete). ArrayForm is true if the array form
  /// was used (delete[]).
  virtual ExprResult ActOnCXXDelete(SourceLocation StartLoc, bool UseGlobal,
                                    bool ArrayForm, ExprTy *Operand) {
    return 0;
  }

  virtual OwningExprResult ActOnUnaryTypeTrait(UnaryTypeTrait OTT,
                                               SourceLocation KWLoc,
                                               SourceLocation LParen,
                                               TypeTy *Ty,
                                               SourceLocation RParen) {
    return ExprEmpty();
  }

  //===---------------------------- C++ Classes ---------------------------===//
  /// ActOnBaseSpecifier - Parsed a base specifier
  virtual BaseResult ActOnBaseSpecifier(DeclTy *classdecl, 
                                        SourceRange SpecifierRange,
                                        bool Virtual, AccessSpecifier Access,
                                        TypeTy *basetype, 
                                        SourceLocation BaseLoc) {
    return 0;
  }

  virtual void ActOnBaseSpecifiers(DeclTy *ClassDecl, BaseTy **Bases, 
                                   unsigned NumBases) {
  }
                                   
  /// ActOnCXXMemberDeclarator - This is invoked when a C++ class member
  /// declarator is parsed. 'AS' is the access specifier, 'BitfieldWidth'
  /// specifies the bitfield width if there is one and 'Init' specifies the
  /// initializer if any. 'LastInGroup' is non-null for cases where one declspec
  /// has multiple declarators on it.
  virtual DeclTy *ActOnCXXMemberDeclarator(Scope *S, AccessSpecifier AS,
                                           Declarator &D, ExprTy *BitfieldWidth,
                                           ExprTy *Init, DeclTy *LastInGroup) {
    return 0;
  }

  virtual MemInitResult ActOnMemInitializer(DeclTy *ConstructorDecl,
                                            Scope *S,
                                            IdentifierInfo *MemberOrBase,
                                            SourceLocation IdLoc,
                                            SourceLocation LParenLoc,
                                            ExprTy **Args, unsigned NumArgs,
                                            SourceLocation *CommaLocs,
                                            SourceLocation RParenLoc) {
    return true;
  }

  /// ActOnMemInitializers - This is invoked when all of the member
  /// initializers of a constructor have been parsed. ConstructorDecl
  /// is the function declaration (which will be a C++ constructor in
  /// a well-formed program), ColonLoc is the location of the ':' that
  /// starts the constructor initializer, and MemInit/NumMemInits
  /// contains the individual member (and base) initializers. 
  virtual void ActOnMemInitializers(DeclTy *ConstructorDecl, 
                                    SourceLocation ColonLoc,
                                    MemInitTy **MemInits, unsigned NumMemInits) {
  }

  /// ActOnFinishCXXMemberSpecification - Invoked after all member declarators
  /// are parsed but *before* parsing of inline method definitions.
  virtual void ActOnFinishCXXMemberSpecification(Scope* S, SourceLocation RLoc,
                                                 DeclTy *TagDecl,
                                                 SourceLocation LBrac,
                                                 SourceLocation RBrac) {
  }

  //===---------------------------C++ Templates----------------------------===//

  /// ActOnTypeParameter - Called when a C++ template type parameter
  /// (e.g., "typename T") has been parsed. Typename specifies whether
  /// the keyword "typename" was used to declare the type parameter
  /// (otherwise, "class" was used), and KeyLoc is the location of the
  /// "class" or "typename" keyword. ParamName is the name of the
  /// parameter (NULL indicates an unnamed template parameter) and
  /// ParamName is the location of the parameter name (if any). 
  /// If the type parameter has a default argument, it will be added
  /// later via ActOnTypeParameterDefault. Depth and Position provide
  /// the number of enclosing templates (see
  /// ActOnTemplateParameterList) and the number of previous
  /// parameters within this template parameter list.
  virtual DeclTy *ActOnTypeParameter(Scope *S, bool Typename, 
				     SourceLocation KeyLoc,
				     IdentifierInfo *ParamName,
				     SourceLocation ParamNameLoc,
                                     unsigned Depth, unsigned Position) {
    return 0;
  }

  /// ActOnTypeParameterDefault - Adds a default argument (the type
  /// Default) to the given template type parameter (TypeParam). 
  virtual void ActOnTypeParameterDefault(DeclTy *TypeParam, TypeTy *Default) {
  }

  /// ActOnNonTypeTemplateParameter - Called when a C++ non-type
  /// template parameter (e.g., "int Size" in "template<int Size>
  /// class Array") has been parsed. S is the current scope and D is
  /// the parsed declarator. Depth and Position provide           
  /// the number of enclosing templates (see
  /// ActOnTemplateParameterList) and the number of previous
  /// parameters within this template parameter list.
  virtual DeclTy *ActOnNonTypeTemplateParameter(Scope *S, Declarator &D,
                                                unsigned Depth, 
                                                unsigned Position) {
    return 0;
  }

  /// ActOnTemplateParameterList - Called when a complete template
  /// parameter list has been parsed, e.g.,
  ///
  /// @code
  /// export template<typename T, T Size>
  /// @endcode
  ///
  /// Depth is the number of enclosing template parameter lists. This
  /// value does not include templates from outer scopes. For example:
  ///
  /// @code
  /// template<typename T> // depth = 0
  ///   class A {
  ///     template<typename U> // depth = 0
  ///       class B;
  ///   };
  ///
  /// template<typename T> // depth = 0
  ///   template<typename U> // depth = 1
  ///     class A<T>::B { ... };
  /// @endcode
  ///
  /// ExportLoc, if valid, is the position of the "export"
  /// keyword. Otherwise, "export" was not specified. 
  /// TemplateLoc is the position of the template keyword, LAngleLoc
  /// is the position of the left angle bracket, and RAngleLoc is the
  /// position of the corresponding right angle bracket.
  /// Params/NumParams provides the template parameters that were
  /// parsed as part of the template-parameter-list.
  virtual TemplateParamsTy *
  ActOnTemplateParameterList(unsigned Depth,
                             SourceLocation ExportLoc,
                             SourceLocation TemplateLoc, 
                             SourceLocation LAngleLoc,
                             DeclTy **Params, unsigned NumParams,
                             SourceLocation RAngleLoc) {
    return 0;
  }

  //===----------------------- Obj-C Declarations -------------------------===//
  
  // ActOnStartClassInterface - this action is called immediately after parsing
  // the prologue for a class interface (before parsing the instance 
  // variables). Instance variables are processed by ActOnFields().
  virtual DeclTy *ActOnStartClassInterface(SourceLocation AtInterfaceLoc,
                                           IdentifierInfo *ClassName, 
                                           SourceLocation ClassLoc,
                                           IdentifierInfo *SuperName, 
                                           SourceLocation SuperLoc,
                                           DeclTy * const *ProtoRefs, 
                                           unsigned NumProtoRefs,
                                           SourceLocation EndProtoLoc,
                                           AttributeList *AttrList) {
    return 0;
  }
  
  /// ActOnCompatiblityAlias - this action is called after complete parsing of
  /// @compaatibility_alias declaration. It sets up the alias relationships.
  virtual DeclTy *ActOnCompatiblityAlias(
    SourceLocation AtCompatibilityAliasLoc,
    IdentifierInfo *AliasName,  SourceLocation AliasLocation,
    IdentifierInfo *ClassName, SourceLocation ClassLocation) {
    return 0;
  }
  
  // ActOnStartProtocolInterface - this action is called immdiately after
  // parsing the prologue for a protocol interface.
  virtual DeclTy *ActOnStartProtocolInterface(SourceLocation AtProtoLoc,
                                              IdentifierInfo *ProtocolName, 
                                              SourceLocation ProtocolLoc,
                                              DeclTy * const *ProtoRefs,
                                              unsigned NumProtoRefs,
                                              SourceLocation EndProtoLoc,
                                              AttributeList *AttrList) {
    return 0;
  }
  // ActOnStartCategoryInterface - this action is called immdiately after
  // parsing the prologue for a category interface.
  virtual DeclTy *ActOnStartCategoryInterface(SourceLocation AtInterfaceLoc,
                                              IdentifierInfo *ClassName, 
                                              SourceLocation ClassLoc,
                                              IdentifierInfo *CategoryName, 
                                              SourceLocation CategoryLoc,
                                              DeclTy * const *ProtoRefs,
                                              unsigned NumProtoRefs,
                                              SourceLocation EndProtoLoc) {
    return 0;
  }
  // ActOnStartClassImplementation - this action is called immdiately after
  // parsing the prologue for a class implementation. Instance variables are 
  // processed by ActOnFields().
  virtual DeclTy *ActOnStartClassImplementation(
    SourceLocation AtClassImplLoc,
    IdentifierInfo *ClassName, 
    SourceLocation ClassLoc,
    IdentifierInfo *SuperClassname, 
    SourceLocation SuperClassLoc) {
    return 0;
  }
  // ActOnStartCategoryImplementation - this action is called immdiately after
  // parsing the prologue for a category implementation.
  virtual DeclTy *ActOnStartCategoryImplementation(
    SourceLocation AtCatImplLoc,
    IdentifierInfo *ClassName, 
    SourceLocation ClassLoc,
    IdentifierInfo *CatName,
    SourceLocation CatLoc) {
    return 0;
  }  
  // ActOnPropertyImplDecl - called for every property implementation
  virtual DeclTy *ActOnPropertyImplDecl(
   SourceLocation AtLoc,              // location of the @synthesize/@dynamic
   SourceLocation PropertyNameLoc,    // location for the property name
   bool ImplKind,                     // true for @synthesize, false for
                                      // @dynamic
   DeclTy *ClassImplDecl,             // class or category implementation
   IdentifierInfo *propertyId,        // name of property
   IdentifierInfo *propertyIvar) {    // name of the ivar
    return 0;
  }

  // ActOnMethodDeclaration - called for all method declarations. 
  virtual DeclTy *ActOnMethodDeclaration(
    SourceLocation BeginLoc,   // location of the + or -.
    SourceLocation EndLoc,     // location of the ; or {.
    tok::TokenKind MethodType, // tok::minus for instance, tok::plus for class.
    DeclTy *ClassDecl,         // class this methods belongs to.
    ObjCDeclSpec &ReturnQT,    // for return type's in inout etc.
    TypeTy *ReturnType,        // the method return type.
    Selector Sel,              // a unique name for the method.
    ObjCDeclSpec *ArgQT,       // for arguments' in inout etc.
    TypeTy **ArgTypes,         // non-zero when Sel.getNumArgs() > 0
    IdentifierInfo **ArgNames, // non-zero when Sel.getNumArgs() > 0
    llvm::SmallVectorImpl<Declarator> &Cdecls, // c-style args
    AttributeList *AttrList,   // optional
    // tok::objc_not_keyword, tok::objc_optional, tok::objc_required    
    tok::ObjCKeywordKind impKind,
    bool isVariadic = false) {
    return 0;
  }
  // ActOnAtEnd - called to mark the @end. For declarations (interfaces,
  // protocols, categories), the parser passes all methods/properties. 
  // For class implementations, these values default to 0. For implementations,
  // methods are processed incrementally (by ActOnMethodDeclaration above).
  virtual void ActOnAtEnd(
    SourceLocation AtEndLoc, 
    DeclTy *classDecl,
    DeclTy **allMethods = 0, 
    unsigned allNum = 0,
    DeclTy **allProperties = 0, 
    unsigned pNum = 0) {
    return;
  }
  // ActOnProperty - called to build one property AST
  virtual DeclTy *ActOnProperty (Scope *S, SourceLocation AtLoc,
                                 FieldDeclarator &FD, ObjCDeclSpec &ODS,
                                 Selector GetterSel, Selector SetterSel,
                                 DeclTy *ClassCategory,
                                 bool *OverridingProperty,
                                 tok::ObjCKeywordKind MethodImplKind) {
    return 0;
  }
                                     
  // ActOnClassMessage - used for both unary and keyword messages.
  // ArgExprs is optional - if it is present, the number of expressions
  // is obtained from NumArgs.
  virtual ExprResult ActOnClassMessage(
    Scope *S,
    IdentifierInfo *receivingClassName, 
    Selector Sel,
    SourceLocation lbrac,
    SourceLocation receiverLoc,
    SourceLocation rbrac, 
    ExprTy **ArgExprs, unsigned NumArgs) {
    return 0;
  }
  // ActOnInstanceMessage - used for both unary and keyword messages.
  // ArgExprs is optional - if it is present, the number of expressions
  // is obtained from NumArgs.
  virtual ExprResult ActOnInstanceMessage(
    ExprTy *receiver, Selector Sel,
    SourceLocation lbrac, SourceLocation rbrac, 
    ExprTy **ArgExprs, unsigned NumArgs) {
    return 0;
  }
  virtual DeclTy *ActOnForwardClassDeclaration(
    SourceLocation AtClassLoc,
    IdentifierInfo **IdentList,
    unsigned NumElts) {
    return 0;
  }
  virtual DeclTy *ActOnForwardProtocolDeclaration(
    SourceLocation AtProtocolLoc,
    const IdentifierLocPair*IdentList,
    unsigned NumElts,
    AttributeList *AttrList) {
    return 0;
  }
  
  /// FindProtocolDeclaration - This routine looks up protocols and
  /// issues error if they are not declared. It returns list of valid
  /// protocols found.
  virtual void FindProtocolDeclaration(bool WarnOnDeclarations,
                                       const IdentifierLocPair *ProtocolId,
                                       unsigned NumProtocols,
                                 llvm::SmallVectorImpl<DeclTy*> &ResProtos) {
  }

  //===----------------------- Obj-C Expressions --------------------------===//

  virtual ExprResult ParseObjCStringLiteral(SourceLocation *AtLocs, 
                                            ExprTy **Strings,
                                            unsigned NumStrings) {
    return 0;
  }

  virtual ExprResult ParseObjCEncodeExpression(SourceLocation AtLoc,
                                               SourceLocation EncLoc,
                                               SourceLocation LParenLoc,
                                               TypeTy *Ty,
                                               SourceLocation RParenLoc) {
    return 0;
  }
  
  virtual ExprResult ParseObjCSelectorExpression(Selector Sel,
                                                 SourceLocation AtLoc,
                                                 SourceLocation SelLoc,
                                                 SourceLocation LParenLoc,
                                                 SourceLocation RParenLoc) {
    return 0;
  }
  
  virtual ExprResult ParseObjCProtocolExpression(IdentifierInfo *ProtocolId,
                                                 SourceLocation AtLoc,
                                                 SourceLocation ProtoLoc,
                                                 SourceLocation LParenLoc,
                                                 SourceLocation RParenLoc) {
    return 0;
  } 

  //===---------------------------- Pragmas -------------------------------===//

  enum PragmaPackKind {
    PPK_Default, // #pragma pack([n]) 
    PPK_Show,    // #pragma pack(show), only supported by MSVC.
    PPK_Push,    // #pragma pack(push, [identifier], [n])
    PPK_Pop      // #pragma pack(pop, [identifier], [n])
  };
  
  /// ActOnPragmaPack - Called on well formed #pragma pack(...).
  virtual void ActOnPragmaPack(PragmaPackKind Kind,
                               IdentifierInfo *Name,
                               ExprTy *Alignment,
                               SourceLocation PragmaLoc, 
                               SourceLocation LParenLoc,
                               SourceLocation RParenLoc) {
    return;
  }
};

/// MinimalAction - Minimal actions are used by light-weight clients of the
/// parser that do not need name resolution or significant semantic analysis to
/// be performed.  The actions implemented here are in the form of unresolved
/// identifiers.  By using a simpler interface than the SemanticAction class,
/// the parser doesn't have to build complex data structures and thus runs more
/// quickly.
class MinimalAction : public Action {
  /// Translation Unit Scope - useful to Objective-C actions that need
  /// to lookup file scope declarations in the "ordinary" C decl namespace.
  /// For example, user-defined classes, built-in "id" type, etc.
  Scope *TUScope;
  IdentifierTable &Idents;
  Preprocessor &PP;
  void *TypeNameInfoTablePtr;
public:
  MinimalAction(Preprocessor &pp);
  ~MinimalAction();

  /// getTypeName - This looks at the IdentifierInfo::FETokenInfo field to
  /// determine whether the name is a typedef or not in this scope.
  virtual TypeTy *getTypeName(IdentifierInfo &II, Scope *S,
                              const CXXScopeSpec *SS);

  /// isCurrentClassName - Always returns false, because MinimalAction
  /// does not support C++ classes with constructors.
  virtual bool isCurrentClassName(const IdentifierInfo& II, Scope *S,
                                  const CXXScopeSpec *SS);

  /// isTemplateName - Determines whether the identifier II is a
  /// template name in the current scope, and returns the template
  /// declaration if II names a template. An optional CXXScope can be
  /// passed to indicate the C++ scope in which the identifier will be
  /// found. 
  virtual DeclTy *isTemplateName(IdentifierInfo &II, Scope *S,
                                 const CXXScopeSpec *SS = 0);

  /// ActOnDeclarator - If this is a typedef declarator, we modify the
  /// IdentifierInfo::FETokenInfo field to keep track of this fact, until S is
  /// popped.
  virtual DeclTy *ActOnDeclarator(Scope *S, Declarator &D, DeclTy *LastInGroup);
  
  /// ActOnPopScope - When a scope is popped, if any typedefs are now 
  /// out-of-scope, they are removed from the IdentifierInfo::FETokenInfo field.
  virtual void ActOnPopScope(SourceLocation Loc, Scope *S);
  virtual void ActOnTranslationUnitScope(SourceLocation Loc, Scope *S);
  
  virtual DeclTy *ActOnForwardClassDeclaration(SourceLocation AtClassLoc,
                                               IdentifierInfo **IdentList,
                                               unsigned NumElts);
  
  virtual DeclTy *ActOnStartClassInterface(SourceLocation interLoc,
                                           IdentifierInfo *ClassName,
                                           SourceLocation ClassLoc,
                                           IdentifierInfo *SuperName,
                                           SourceLocation SuperLoc,
                                           DeclTy * const *ProtoRefs, 
                                           unsigned NumProtoRefs,
                                           SourceLocation EndProtoLoc,
                                           AttributeList *AttrList);
};

}  // end namespace clang

#endif
