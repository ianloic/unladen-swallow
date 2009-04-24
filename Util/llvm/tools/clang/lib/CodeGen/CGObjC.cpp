//===---- CGBuiltin.cpp - Emit LLVM Code for builtins ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit Objective-C code as LLVM code.
//
//===----------------------------------------------------------------------===//

#include "CGObjCRuntime.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclObjC.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Target/TargetData.h"

using namespace clang;
using namespace CodeGen;

/// Emits an instance of NSConstantString representing the object.
llvm::Value *CodeGenFunction::EmitObjCStringLiteral(const ObjCStringLiteral *E) 
{
  std::string String(E->getString()->getStrData(), 
                     E->getString()->getByteLength());
  llvm::Constant *C = CGM.getObjCRuntime().GenerateConstantString(String);
  // FIXME: This bitcast should just be made an invariant on the Runtime.
  return llvm::ConstantExpr::getBitCast(C, ConvertType(E->getType()));
}

/// Emit a selector.
llvm::Value *CodeGenFunction::EmitObjCSelectorExpr(const ObjCSelectorExpr *E) {
  // Untyped selector.
  // Note that this implementation allows for non-constant strings to be passed
  // as arguments to @selector().  Currently, the only thing preventing this
  // behaviour is the type checking in the front end.
  return CGM.getObjCRuntime().GetSelector(Builder, E->getSelector());
}

llvm::Value *CodeGenFunction::EmitObjCProtocolExpr(const ObjCProtocolExpr *E) {
  // FIXME: This should pass the Decl not the name.
  return CGM.getObjCRuntime().GenerateProtocolRef(Builder, E->getProtocol());
}


RValue CodeGenFunction::EmitObjCMessageExpr(const ObjCMessageExpr *E) {
  // Only the lookup mechanism and first two arguments of the method
  // implementation vary between runtimes.  We can get the receiver and
  // arguments in generic code.
  
  CGObjCRuntime &Runtime = CGM.getObjCRuntime();
  const Expr *ReceiverExpr = E->getReceiver();
  bool isSuperMessage = false;
  bool isClassMessage = false;
  // Find the receiver
  llvm::Value *Receiver;
  if (!ReceiverExpr) {
    const ObjCInterfaceDecl *OID = E->getClassInfo().first;

    // Very special case, super send in class method. The receiver is
    // self (the class object) and the send uses super semantics.
    if (!OID) {
      assert(E->getClassName()->isStr("super") &&
             "Unexpected missing class interface in message send.");
      isSuperMessage = true;
      Receiver = LoadObjCSelf();
    } else {
      Receiver = Runtime.GetClass(Builder, OID);
    }
    
    isClassMessage = true;
  } else if (isa<ObjCSuperExpr>(E->getReceiver())) {
    isSuperMessage = true;
    Receiver = LoadObjCSelf();
  } else {
    Receiver = EmitScalarExpr(E->getReceiver());
  }

  CallArgList Args;
  for (CallExpr::const_arg_iterator i = E->arg_begin(), e = E->arg_end(); 
       i != e; ++i)
    Args.push_back(std::make_pair(EmitAnyExprToTemp(*i), (*i)->getType()));
  
  if (isSuperMessage) {
    // super is only valid in an Objective-C method
    const ObjCMethodDecl *OMD = cast<ObjCMethodDecl>(CurFuncDecl);
    return Runtime.GenerateMessageSendSuper(*this, E->getType(),
                                            E->getSelector(),
                                            OMD->getClassInterface(),
                                            Receiver,
                                            isClassMessage,
                                            Args);
  }
  return Runtime.GenerateMessageSend(*this, E->getType(), E->getSelector(), 
                                     Receiver, isClassMessage, Args);
}

/// StartObjCMethod - Begin emission of an ObjCMethod. This generates
/// the LLVM function and sets the other context used by
/// CodeGenFunction.
void CodeGenFunction::StartObjCMethod(const ObjCMethodDecl *OMD,
                                      const ObjCContainerDecl *CD) {
  FunctionArgList Args;
  llvm::Function *Fn = CGM.getObjCRuntime().GenerateMethod(OMD, CD);

  CGM.SetMethodAttributes(OMD, Fn);

  Args.push_back(std::make_pair(OMD->getSelfDecl(), 
                                OMD->getSelfDecl()->getType()));
  Args.push_back(std::make_pair(OMD->getCmdDecl(),
                                OMD->getCmdDecl()->getType()));

  for (unsigned i = 0, e = OMD->getNumParams(); i != e; ++i) {
    ParmVarDecl *IPD = OMD->getParamDecl(i);
    Args.push_back(std::make_pair(IPD, IPD->getType()));
  }

  StartFunction(OMD, OMD->getResultType(), Fn, Args, OMD->getLocEnd());
}

/// Generate an Objective-C method.  An Objective-C method is a C function with
/// its pointer, name, and types registered in the class struture.  
void CodeGenFunction::GenerateObjCMethod(const ObjCMethodDecl *OMD) {
  StartObjCMethod(OMD, OMD->getClassInterface());
  EmitStmt(OMD->getBody());
  FinishFunction(cast<CompoundStmt>(OMD->getBody())->getRBracLoc());
}

// FIXME: I wasn't sure about the synthesis approach. If we end up
// generating an AST for the whole body we can just fall back to
// having a GenerateFunction which takes the body Stmt.

/// GenerateObjCGetter - Generate an Objective-C property getter
/// function. The given Decl must be an ObjCImplementationDecl. @synthesize
/// is illegal within a category.
void CodeGenFunction::GenerateObjCGetter(ObjCImplementationDecl *IMP,
                                         const ObjCPropertyImplDecl *PID) {
  ObjCIvarDecl *Ivar = PID->getPropertyIvarDecl();
  const ObjCPropertyDecl *PD = PID->getPropertyDecl();
  ObjCMethodDecl *OMD = PD->getGetterMethodDecl();
  assert(OMD && "Invalid call to generate getter (empty method)");
  // FIXME: This is rather murky, we create this here since they will
  // not have been created by Sema for us.
  OMD->createImplicitParams(getContext(), IMP->getClassInterface());
  StartObjCMethod(OMD, IMP->getClassInterface());

  // Determine if we should use an objc_getProperty call for
  // this. Non-atomic properties are directly evaluated.
  // atomic 'copy' and 'retain' properties are also directly
  // evaluated in gc-only mode.
  if (CGM.getLangOptions().getGCMode() != LangOptions::GCOnly &&
      !(PD->getPropertyAttributes() & ObjCPropertyDecl::OBJC_PR_nonatomic) &&
      (PD->getSetterKind() == ObjCPropertyDecl::Copy ||
       PD->getSetterKind() == ObjCPropertyDecl::Retain)) {
    llvm::Value *GetPropertyFn = 
      CGM.getObjCRuntime().GetPropertyGetFunction();
    
    if (!GetPropertyFn) {
      CGM.ErrorUnsupported(PID, "Obj-C getter requiring atomic copy");
      FinishFunction();
      return;
    }

    // Return (ivar-type) objc_getProperty((id) self, _cmd, offset, true).
    // FIXME: Can't this be simpler? This might even be worse than the
    // corresponding gcc code.
    CodeGenTypes &Types = CGM.getTypes();
    ValueDecl *Cmd = OMD->getCmdDecl();
    llvm::Value *CmdVal = Builder.CreateLoad(LocalDeclMap[Cmd], "cmd");
    QualType IdTy = getContext().getObjCIdType();
    llvm::Value *SelfAsId = 
      Builder.CreateBitCast(LoadObjCSelf(), Types.ConvertType(IdTy));
    llvm::Value *Offset = EmitIvarOffset(IMP->getClassInterface(), Ivar);
    llvm::Value *True =
      llvm::ConstantInt::get(Types.ConvertTypeForMem(getContext().BoolTy), 1);
    CallArgList Args;
    Args.push_back(std::make_pair(RValue::get(SelfAsId), IdTy));
    Args.push_back(std::make_pair(RValue::get(CmdVal), Cmd->getType()));
    Args.push_back(std::make_pair(RValue::get(Offset), getContext().LongTy));
    Args.push_back(std::make_pair(RValue::get(True), getContext().BoolTy));
    RValue RV = EmitCall(Types.getFunctionInfo(PD->getType(), Args), 
                         GetPropertyFn, Args);
    // We need to fix the type here. Ivars with copy & retain are
    // always objects so we don't need to worry about complex or
    // aggregates.
    RV = RValue::get(Builder.CreateBitCast(RV.getScalarVal(), 
                                           Types.ConvertType(PD->getType())));
    EmitReturnOfRValue(RV, PD->getType());
  } else {
    FieldDecl *Field = 
      IMP->getClassInterface()->lookupFieldDeclForIvar(getContext(), Ivar);
    LValue LV = EmitLValueForIvar(TypeOfSelfObject(),
                                  LoadObjCSelf(), Ivar, Field, 0);
    if (hasAggregateLLVMType(Ivar->getType())) {
      EmitAggregateCopy(ReturnValue, LV.getAddress(), Ivar->getType());
    }
    else
      EmitReturnOfRValue(EmitLoadOfLValue(LV, Ivar->getType()), 
                                          PD->getType());
  }

  FinishFunction();
}

/// GenerateObjCSetter - Generate an Objective-C property setter
/// function. The given Decl must be an ObjCImplementationDecl. @synthesize
/// is illegal within a category.
void CodeGenFunction::GenerateObjCSetter(ObjCImplementationDecl *IMP,
                                         const ObjCPropertyImplDecl *PID) {
  ObjCIvarDecl *Ivar = PID->getPropertyIvarDecl();
  const ObjCPropertyDecl *PD = PID->getPropertyDecl();
  ObjCMethodDecl *OMD = PD->getSetterMethodDecl();
  assert(OMD && "Invalid call to generate setter (empty method)");
  // FIXME: This is rather murky, we create this here since they will
  // not have been created by Sema for us.  
  OMD->createImplicitParams(getContext(), IMP->getClassInterface());
  StartObjCMethod(OMD, IMP->getClassInterface());

  bool IsCopy = PD->getSetterKind() == ObjCPropertyDecl::Copy;
  bool IsAtomic = 
    !(PD->getPropertyAttributes() & ObjCPropertyDecl::OBJC_PR_nonatomic);

  // Determine if we should use an objc_setProperty call for
  // this. Properties with 'copy' semantics always use it, as do
  // non-atomic properties with 'release' semantics as long as we are
  // not in gc-only mode.
  if (IsCopy ||
      (CGM.getLangOptions().getGCMode() != LangOptions::GCOnly &&
       PD->getSetterKind() == ObjCPropertyDecl::Retain)) {
    llvm::Value *SetPropertyFn = 
      CGM.getObjCRuntime().GetPropertySetFunction();
    
    if (!SetPropertyFn) {
      CGM.ErrorUnsupported(PID, "Obj-C getter requiring atomic copy");
      FinishFunction();
      return;
    }
    
    // Emit objc_setProperty((id) self, _cmd, offset, arg, 
    //                       <is-atomic>, <is-copy>).
    // FIXME: Can't this be simpler? This might even be worse than the
    // corresponding gcc code.
    CodeGenTypes &Types = CGM.getTypes();
    ValueDecl *Cmd = OMD->getCmdDecl();
    llvm::Value *CmdVal = Builder.CreateLoad(LocalDeclMap[Cmd], "cmd");
    QualType IdTy = getContext().getObjCIdType();
    llvm::Value *SelfAsId = 
      Builder.CreateBitCast(LoadObjCSelf(), Types.ConvertType(IdTy));
    llvm::Value *Offset = EmitIvarOffset(IMP->getClassInterface(), Ivar);
    llvm::Value *Arg = LocalDeclMap[OMD->getParamDecl(0)];
    llvm::Value *ArgAsId = 
      Builder.CreateBitCast(Builder.CreateLoad(Arg, "arg"),
                            Types.ConvertType(IdTy));
    llvm::Value *True =
      llvm::ConstantInt::get(Types.ConvertTypeForMem(getContext().BoolTy), 1);
    llvm::Value *False =
      llvm::ConstantInt::get(Types.ConvertTypeForMem(getContext().BoolTy), 0);
    CallArgList Args;
    Args.push_back(std::make_pair(RValue::get(SelfAsId), IdTy));
    Args.push_back(std::make_pair(RValue::get(CmdVal), Cmd->getType()));
    Args.push_back(std::make_pair(RValue::get(Offset), getContext().LongTy));
    Args.push_back(std::make_pair(RValue::get(ArgAsId), IdTy));
    Args.push_back(std::make_pair(RValue::get(IsAtomic ? True : False), 
                                  getContext().BoolTy));
    Args.push_back(std::make_pair(RValue::get(IsCopy ? True : False), 
                                  getContext().BoolTy));
    EmitCall(Types.getFunctionInfo(PD->getType(), Args), 
             SetPropertyFn, Args);
  } else {
    SourceLocation Loc = PD->getLocation();
    ValueDecl *Self = OMD->getSelfDecl();
    ObjCIvarDecl *Ivar = PID->getPropertyIvarDecl();
    DeclRefExpr Base(Self, Self->getType(), Loc);
    ParmVarDecl *ArgDecl = OMD->getParamDecl(0);
    DeclRefExpr Arg(ArgDecl, ArgDecl->getType(), Loc);
    ObjCInterfaceDecl *OI = IMP->getClassInterface();
    ObjCIvarRefExpr IvarRef(Ivar, Ivar->getType(), Loc, &Base,
                            true, true);
    getContext().setFieldDecl(OI, Ivar, &IvarRef);
    BinaryOperator Assign(&IvarRef, &Arg, BinaryOperator::Assign,
                          Ivar->getType(), Loc);
    EmitStmt(&Assign);
  }

  FinishFunction();
}

llvm::Value *CodeGenFunction::LoadObjCSelf() {
  const ObjCMethodDecl *OMD = cast<ObjCMethodDecl>(CurFuncDecl);
  return Builder.CreateLoad(LocalDeclMap[OMD->getSelfDecl()], "self");
}

QualType CodeGenFunction::TypeOfSelfObject() {
  const ObjCMethodDecl *OMD = cast<ObjCMethodDecl>(CurFuncDecl);
  ImplicitParamDecl *selfDecl = OMD->getSelfDecl();
  const PointerType *PTy = 
    cast<PointerType>(getContext().getCanonicalType(selfDecl->getType()));
  return PTy->getPointeeType();
}

RValue CodeGenFunction::EmitObjCPropertyGet(const Expr *Exp) {
  // FIXME: Split it into two separate routines.
  if (const ObjCPropertyRefExpr *E = dyn_cast<ObjCPropertyRefExpr>(Exp)) {
    Selector S = E->getProperty()->getGetterName();
    return CGM.getObjCRuntime().
             GenerateMessageSend(*this, Exp->getType(), S, 
                                 EmitScalarExpr(E->getBase()), 
                                 false, CallArgList());
  }
  else {
    const ObjCKVCRefExpr *KE = cast<ObjCKVCRefExpr>(Exp);
    Selector S = KE->getGetterMethod()->getSelector();
    return CGM.getObjCRuntime().
             GenerateMessageSend(*this, Exp->getType(), S, 
                                 EmitScalarExpr(KE->getBase()), 
                                 false, CallArgList());
  }
}

void CodeGenFunction::EmitObjCPropertySet(const Expr *Exp,
                                          RValue Src) {
  // FIXME: Split it into two separate routines.
  if (const ObjCPropertyRefExpr *E = dyn_cast<ObjCPropertyRefExpr>(Exp)) {
    Selector S = E->getProperty()->getSetterName();
    CallArgList Args;
    Args.push_back(std::make_pair(Src, E->getType()));
    CGM.getObjCRuntime().GenerateMessageSend(*this, getContext().VoidTy, S, 
                                             EmitScalarExpr(E->getBase()), 
                                             false, Args);
  }
  else if (const ObjCKVCRefExpr *E = dyn_cast<ObjCKVCRefExpr>(Exp)) {
    Selector S = E->getSetterMethod()->getSelector();
    CallArgList Args;
    Args.push_back(std::make_pair(Src, E->getType()));
    CGM.getObjCRuntime().GenerateMessageSend(*this, getContext().VoidTy, S, 
                                             EmitScalarExpr(E->getBase()), 
                                             false, Args);
  }
  else
    assert (0 && "bad expression node in EmitObjCPropertySet");
}

void CodeGenFunction::EmitObjCForCollectionStmt(const ObjCForCollectionStmt &S)
{
  llvm::Function *EnumerationMutationFn = 
    CGM.getObjCRuntime().EnumerationMutationFunction();
  llvm::Value *DeclAddress;
  QualType ElementTy;
  
  if (!EnumerationMutationFn) {
    CGM.ErrorUnsupported(&S, "Obj-C fast enumeration for this runtime");
    return;
  }

  if (const DeclStmt *SD = dyn_cast<DeclStmt>(S.getElement())) {
    EmitStmt(SD);
    assert(HaveInsertPoint() && "DeclStmt destroyed insert point!");
    const Decl* D = SD->getSolitaryDecl();
    ElementTy = cast<ValueDecl>(D)->getType();
    DeclAddress = LocalDeclMap[D];    
  } else {
    ElementTy = cast<Expr>(S.getElement())->getType();
    DeclAddress = 0;
  }
  
  // Fast enumeration state.
  QualType StateTy = getContext().getObjCFastEnumerationStateType();
  llvm::AllocaInst *StatePtr = CreateTempAlloca(ConvertType(StateTy), 
                                                "state.ptr");
  StatePtr->setAlignment(getContext().getTypeAlign(StateTy) >> 3);  
  EmitMemSetToZero(StatePtr, StateTy);
  
  // Number of elements in the items array.
  static const unsigned NumItems = 16;
  
  // Get selector
  llvm::SmallVector<IdentifierInfo*, 3> II;
  II.push_back(&CGM.getContext().Idents.get("countByEnumeratingWithState"));
  II.push_back(&CGM.getContext().Idents.get("objects"));
  II.push_back(&CGM.getContext().Idents.get("count"));
  Selector FastEnumSel = CGM.getContext().Selectors.getSelector(II.size(), 
                                                                &II[0]);

  QualType ItemsTy =
    getContext().getConstantArrayType(getContext().getObjCIdType(),
                                      llvm::APInt(32, NumItems), 
                                      ArrayType::Normal, 0);
  llvm::Value *ItemsPtr = CreateTempAlloca(ConvertType(ItemsTy), "items.ptr");
  
  llvm::Value *Collection = EmitScalarExpr(S.getCollection());
  
  CallArgList Args;
  Args.push_back(std::make_pair(RValue::get(StatePtr), 
                                getContext().getPointerType(StateTy)));
  
  Args.push_back(std::make_pair(RValue::get(ItemsPtr), 
                                getContext().getPointerType(ItemsTy)));
  
  const llvm::Type *UnsignedLongLTy = ConvertType(getContext().UnsignedLongTy);
  llvm::Constant *Count = llvm::ConstantInt::get(UnsignedLongLTy, NumItems);
  Args.push_back(std::make_pair(RValue::get(Count), 
                                getContext().UnsignedLongTy));
  
  RValue CountRV = 
    CGM.getObjCRuntime().GenerateMessageSend(*this, 
                                             getContext().UnsignedLongTy,
                                             FastEnumSel,
                                             Collection, false, Args);

  llvm::Value *LimitPtr = CreateTempAlloca(UnsignedLongLTy, "limit.ptr");
  Builder.CreateStore(CountRV.getScalarVal(), LimitPtr);
  
  llvm::BasicBlock *NoElements = createBasicBlock("noelements");
  llvm::BasicBlock *SetStartMutations = createBasicBlock("setstartmutations");
  
  llvm::Value *Limit = Builder.CreateLoad(LimitPtr);
  llvm::Value *Zero = llvm::Constant::getNullValue(UnsignedLongLTy);

  llvm::Value *IsZero = Builder.CreateICmpEQ(Limit, Zero, "iszero");
  Builder.CreateCondBr(IsZero, NoElements, SetStartMutations);

  EmitBlock(SetStartMutations);
  
  llvm::Value *StartMutationsPtr = 
    CreateTempAlloca(UnsignedLongLTy);
  
  llvm::Value *StateMutationsPtrPtr = 
    Builder.CreateStructGEP(StatePtr, 2, "mutationsptr.ptr");
  llvm::Value *StateMutationsPtr = Builder.CreateLoad(StateMutationsPtrPtr, 
                                                      "mutationsptr");
  
  llvm::Value *StateMutations = Builder.CreateLoad(StateMutationsPtr, 
                                                   "mutations");
  
  Builder.CreateStore(StateMutations, StartMutationsPtr);
  
  llvm::BasicBlock *LoopStart = createBasicBlock("loopstart");
  EmitBlock(LoopStart);

  llvm::Value *CounterPtr = CreateTempAlloca(UnsignedLongLTy, "counter.ptr");
  Builder.CreateStore(Zero, CounterPtr);
  
  llvm::BasicBlock *LoopBody = createBasicBlock("loopbody"); 
  EmitBlock(LoopBody);

  StateMutationsPtr = Builder.CreateLoad(StateMutationsPtrPtr, "mutationsptr");
  StateMutations = Builder.CreateLoad(StateMutationsPtr, "statemutations");

  llvm::Value *StartMutations = Builder.CreateLoad(StartMutationsPtr, 
                                                   "mutations");
  llvm::Value *MutationsEqual = Builder.CreateICmpEQ(StateMutations, 
                                                     StartMutations,
                                                     "tobool");
  
  
  llvm::BasicBlock *WasMutated = createBasicBlock("wasmutated");
  llvm::BasicBlock *WasNotMutated = createBasicBlock("wasnotmutated");
  
  Builder.CreateCondBr(MutationsEqual, WasNotMutated, WasMutated);
  
  EmitBlock(WasMutated);
  llvm::Value *V =
    Builder.CreateBitCast(Collection, 
                          ConvertType(getContext().getObjCIdType()),
                          "tmp");
  Builder.CreateCall(EnumerationMutationFn, V);
  
  EmitBlock(WasNotMutated);
  
  llvm::Value *StateItemsPtr = 
    Builder.CreateStructGEP(StatePtr, 1, "stateitems.ptr");

  llvm::Value *Counter = Builder.CreateLoad(CounterPtr, "counter");

  llvm::Value *EnumStateItems = Builder.CreateLoad(StateItemsPtr,
                                                   "stateitems");

  llvm::Value *CurrentItemPtr = 
    Builder.CreateGEP(EnumStateItems, Counter, "currentitem.ptr");
  
  llvm::Value *CurrentItem = Builder.CreateLoad(CurrentItemPtr, "currentitem");
  
  // Cast the item to the right type.
  CurrentItem = Builder.CreateBitCast(CurrentItem,
                                      ConvertType(ElementTy), "tmp");
  
  if (!DeclAddress) {
    LValue LV = EmitLValue(cast<Expr>(S.getElement()));
    
    // Set the value to null.
    Builder.CreateStore(CurrentItem, LV.getAddress());
  } else
    Builder.CreateStore(CurrentItem, DeclAddress);
  
  // Increment the counter.
  Counter = Builder.CreateAdd(Counter, 
                              llvm::ConstantInt::get(UnsignedLongLTy, 1));
  Builder.CreateStore(Counter, CounterPtr);
  
  llvm::BasicBlock *LoopEnd = createBasicBlock("loopend");
  llvm::BasicBlock *AfterBody = createBasicBlock("afterbody");
  
  BreakContinueStack.push_back(BreakContinue(LoopEnd, AfterBody, 
                                             ObjCEHStack.size()));

  EmitStmt(S.getBody());
  
  BreakContinueStack.pop_back();
  
  EmitBlock(AfterBody);
  
  llvm::BasicBlock *FetchMore = createBasicBlock("fetchmore");

  Counter = Builder.CreateLoad(CounterPtr);
  Limit = Builder.CreateLoad(LimitPtr);
  llvm::Value *IsLess = Builder.CreateICmpULT(Counter, Limit, "isless");
  Builder.CreateCondBr(IsLess, LoopBody, FetchMore);

  // Fetch more elements.
  EmitBlock(FetchMore);
  
  CountRV = 
    CGM.getObjCRuntime().GenerateMessageSend(*this, 
                                             getContext().UnsignedLongTy,
                                             FastEnumSel, 
                                             Collection, false, Args);
  Builder.CreateStore(CountRV.getScalarVal(), LimitPtr);
  Limit = Builder.CreateLoad(LimitPtr);
  
  IsZero = Builder.CreateICmpEQ(Limit, Zero, "iszero");
  Builder.CreateCondBr(IsZero, NoElements, LoopStart);
  
  // No more elements.
  EmitBlock(NoElements);

  if (!DeclAddress) {
    // If the element was not a declaration, set it to be null.

    LValue LV = EmitLValue(cast<Expr>(S.getElement()));
    
    // Set the value to null.
    Builder.CreateStore(llvm::Constant::getNullValue(ConvertType(ElementTy)),
                        LV.getAddress());
  }

  EmitBlock(LoopEnd);
}

void CodeGenFunction::EmitObjCAtTryStmt(const ObjCAtTryStmt &S)
{
  CGM.getObjCRuntime().EmitTryOrSynchronizedStmt(*this, S);
}

void CodeGenFunction::EmitObjCAtThrowStmt(const ObjCAtThrowStmt &S)
{
  CGM.getObjCRuntime().EmitThrowStmt(*this, S);
}

void CodeGenFunction::EmitObjCAtSynchronizedStmt(
                                              const ObjCAtSynchronizedStmt &S)
{
  CGM.getObjCRuntime().EmitTryOrSynchronizedStmt(*this, S);
}

CGObjCRuntime::~CGObjCRuntime() {}
