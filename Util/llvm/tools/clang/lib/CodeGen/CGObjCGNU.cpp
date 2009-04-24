//===------- CGObjCGNU.cpp - Emit LLVM Code from ASTs for a Module --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This provides Objective-C code generation targetting the GNU runtime.  The
// class in this file generates structures used by the GNU Objective-C runtime
// library.  These structures are defined in objc/objc.h and objc/objc-api.h in
// the GNU runtime distribution.
//
//===----------------------------------------------------------------------===//

#include "CGObjCRuntime.h"
#include "CodeGenModule.h"
#include "CodeGenFunction.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
#include "llvm/Module.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Target/TargetData.h"
#include <map>


using namespace clang;
using namespace CodeGen;
using llvm::dyn_cast;

// The version of the runtime that this class targets.  Must match the version
// in the runtime.
static const int RuntimeVersion = 8;
static const int ProtocolVersion = 2;

namespace {
class CGObjCGNU : public CodeGen::CGObjCRuntime {
private:
  CodeGen::CodeGenModule &CGM;
  llvm::Module &TheModule;
  const llvm::PointerType *SelectorTy;
  const llvm::PointerType *PtrToInt8Ty;
  const llvm::Type *IMPTy;
  const llvm::PointerType *IdTy;
  const llvm::IntegerType *IntTy;
  const llvm::PointerType *PtrTy;
  const llvm::IntegerType *LongTy;
  const llvm::PointerType *PtrToIntTy;
  std::vector<llvm::Constant*> Classes;
  std::vector<llvm::Constant*> Categories;
  std::vector<llvm::Constant*> ConstantStrings;
  llvm::Function *LoadFunction;
  llvm::StringMap<llvm::Constant*> ExistingProtocols;
  typedef std::pair<std::string, std::string> TypedSelector;
  std::map<TypedSelector, llvm::GlobalAlias*> TypedSelectors;
  llvm::StringMap<llvm::GlobalAlias*> UntypedSelectors;
  // Some zeros used for GEPs in lots of places.
  llvm::Constant *Zeros[2];
  llvm::Constant *NULLPtr;
private:
  llvm::Constant *GenerateIvarList(
      const llvm::SmallVectorImpl<llvm::Constant *>  &IvarNames,
      const llvm::SmallVectorImpl<llvm::Constant *>  &IvarTypes,
      const llvm::SmallVectorImpl<llvm::Constant *>  &IvarOffsets);
  llvm::Constant *GenerateMethodList(const std::string &ClassName,
      const std::string &CategoryName,
      const llvm::SmallVectorImpl<Selector>  &MethodSels, 
      const llvm::SmallVectorImpl<llvm::Constant *>  &MethodTypes, 
      bool isClassMethodList);
  llvm::Constant *GenerateProtocolList(
      const llvm::SmallVectorImpl<std::string> &Protocols);
  llvm::Constant *GenerateClassStructure(
      llvm::Constant *MetaClass,
      llvm::Constant *SuperClass,
      unsigned info,
      const char *Name,
      llvm::Constant *Version,
      llvm::Constant *InstanceSize,
      llvm::Constant *IVars,
      llvm::Constant *Methods,
      llvm::Constant *Protocols);
  llvm::Constant *GenerateProtocolMethodList(
      const llvm::SmallVectorImpl<llvm::Constant *>  &MethodNames,
      const llvm::SmallVectorImpl<llvm::Constant *>  &MethodTypes);
  llvm::Constant *MakeConstantString(const std::string &Str, const std::string
      &Name="");
  llvm::Constant *MakeGlobal(const llvm::StructType *Ty,
      std::vector<llvm::Constant*> &V, const std::string &Name="");
  llvm::Constant *MakeGlobal(const llvm::ArrayType *Ty,
      std::vector<llvm::Constant*> &V, const std::string &Name="");
public:
  CGObjCGNU(CodeGen::CodeGenModule &cgm);
  virtual llvm::Constant *GenerateConstantString(const std::string &String);
  virtual CodeGen::RValue 
  GenerateMessageSend(CodeGen::CodeGenFunction &CGF,
                      QualType ResultType,
                      Selector Sel,
                      llvm::Value *Receiver,
                      bool IsClassMessage,
                      const CallArgList &CallArgs);
  virtual CodeGen::RValue 
  GenerateMessageSendSuper(CodeGen::CodeGenFunction &CGF,
                           QualType ResultType,
                           Selector Sel,
                           const ObjCInterfaceDecl *Class,
                           llvm::Value *Receiver,
                           bool IsClassMessage,
                           const CallArgList &CallArgs);
  virtual llvm::Value *GetClass(CGBuilderTy &Builder,
                                const ObjCInterfaceDecl *OID);
  virtual llvm::Value *GetSelector(CGBuilderTy &Builder, Selector Sel);
  
  virtual llvm::Function *GenerateMethod(const ObjCMethodDecl *OMD, 
                                         const ObjCContainerDecl *CD);
  virtual void GenerateCategory(const ObjCCategoryImplDecl *CMD);
  virtual void GenerateClass(const ObjCImplementationDecl *ClassDecl);
  virtual llvm::Value *GenerateProtocolRef(CGBuilderTy &Builder,
                                           const ObjCProtocolDecl *PD);
  virtual void GenerateProtocol(const ObjCProtocolDecl *PD);
  virtual llvm::Function *ModuleInitFunction();
  virtual llvm::Function *GetPropertyGetFunction();
  virtual llvm::Function *GetPropertySetFunction();
  virtual llvm::Function *EnumerationMutationFunction();
  
  virtual void EmitTryOrSynchronizedStmt(CodeGen::CodeGenFunction &CGF,
                                         const Stmt &S);
  virtual void EmitThrowStmt(CodeGen::CodeGenFunction &CGF,
                             const ObjCAtThrowStmt &S);
  virtual llvm::Value * EmitObjCWeakRead(CodeGen::CodeGenFunction &CGF,
                                         llvm::Value *AddrWeakObj);
  virtual void EmitObjCWeakAssign(CodeGen::CodeGenFunction &CGF,
                                  llvm::Value *src, llvm::Value *dst);
  virtual void EmitObjCGlobalAssign(CodeGen::CodeGenFunction &CGF,
                                    llvm::Value *src, llvm::Value *dest);
  virtual void EmitObjCIvarAssign(CodeGen::CodeGenFunction &CGF,
                                    llvm::Value *src, llvm::Value *dest);
  virtual void EmitObjCStrongCastAssign(CodeGen::CodeGenFunction &CGF,
                                        llvm::Value *src, llvm::Value *dest);
  virtual llvm::Value *EmitObjCValueForIvar(CodeGen::CodeGenFunction &CGF,
                                            QualType ObjectTy,
                                            llvm::Value *BaseValue,
                                            const ObjCIvarDecl *Ivar,
                                            const FieldDecl *Field,
                                            unsigned CVRQualifiers);
};
} // end anonymous namespace



static std::string SymbolNameForClass(const std::string &ClassName) {
  return ".objc_class_" + ClassName;
}

static std::string SymbolNameForMethod(const std::string &ClassName, const
  std::string &CategoryName, const std::string &MethodName, bool isClassMethod)
{
  return "._objc_method_" + ClassName +"("+CategoryName+")"+
            (isClassMethod ? "+" : "-") + MethodName;
}

CGObjCGNU::CGObjCGNU(CodeGen::CodeGenModule &cgm)
  : CGM(cgm), TheModule(CGM.getModule()) {
  IntTy = cast<llvm::IntegerType>(
      CGM.getTypes().ConvertType(CGM.getContext().IntTy));
  LongTy = cast<llvm::IntegerType>(
      CGM.getTypes().ConvertType(CGM.getContext().LongTy));
    
  Zeros[0] = llvm::ConstantInt::get(LongTy, 0);
  Zeros[1] = Zeros[0];
  NULLPtr = llvm::ConstantPointerNull::get(
    llvm::PointerType::getUnqual(llvm::Type::Int8Ty));
  // C string type.  Used in lots of places.
  PtrToInt8Ty = 
    llvm::PointerType::getUnqual(llvm::Type::Int8Ty);
  // Get the selector Type.
  SelectorTy = cast<llvm::PointerType>(
    CGM.getTypes().ConvertType(CGM.getContext().getObjCSelType()));

  PtrToIntTy = llvm::PointerType::getUnqual(IntTy);
  PtrTy = PtrToInt8Ty;
 
  // Object type
  llvm::PATypeHolder OpaqueObjTy = llvm::OpaqueType::get();
  llvm::Type *OpaqueIdTy = llvm::PointerType::getUnqual(OpaqueObjTy);
  llvm::Type *ObjectTy = llvm::StructType::get(OpaqueIdTy, NULL);
  llvm::cast<llvm::OpaqueType>(OpaqueObjTy.get())->refineAbstractTypeTo(
      ObjectTy);
  ObjectTy = llvm::cast<llvm::StructType>(OpaqueObjTy.get());
  IdTy = llvm::PointerType::getUnqual(ObjectTy);
 
  // IMP type
  std::vector<const llvm::Type*> IMPArgs;
  IMPArgs.push_back(IdTy);
  IMPArgs.push_back(SelectorTy);
  IMPTy = llvm::FunctionType::get(IdTy, IMPArgs, true);
}
// This has to perform the lookup every time, since posing and related
// techniques can modify the name -> class mapping.
llvm::Value *CGObjCGNU::GetClass(CGBuilderTy &Builder,
                                 const ObjCInterfaceDecl *OID) {
  llvm::Value *ClassName = CGM.GetAddrOfConstantCString(OID->getNameAsString());
  ClassName = Builder.CreateStructGEP(ClassName, 0);

  llvm::Constant *ClassLookupFn =
    TheModule.getOrInsertFunction("objc_lookup_class", IdTy, PtrToInt8Ty,
        NULL);
  return Builder.CreateCall(ClassLookupFn, ClassName);
}

/// GetSelector - Return the pointer to the unique'd string for this selector.
llvm::Value *CGObjCGNU::GetSelector(CGBuilderTy &Builder, Selector Sel) {
  // FIXME: uniquing on the string is wasteful, unique on Sel instead!
  llvm::GlobalAlias *&US = UntypedSelectors[Sel.getAsString()];
  if (US == 0)
    US = new llvm::GlobalAlias(llvm::PointerType::getUnqual(SelectorTy),
                               llvm::GlobalValue::InternalLinkage,
                               ".objc_untyped_selector_alias",
                               NULL, &TheModule);
  
  return Builder.CreateLoad(US);
  
}

llvm::Constant *CGObjCGNU::MakeConstantString(const std::string &Str,
                                              const std::string &Name) {
  llvm::Constant * ConstStr = llvm::ConstantArray::get(Str);
  ConstStr = new llvm::GlobalVariable(ConstStr->getType(), true, 
                               llvm::GlobalValue::InternalLinkage,
                               ConstStr, Name, &TheModule);
  return llvm::ConstantExpr::getGetElementPtr(ConstStr, Zeros, 2);
}
llvm::Constant *CGObjCGNU::MakeGlobal(const llvm::StructType *Ty,
    std::vector<llvm::Constant*> &V, const std::string &Name) {
  llvm::Constant *C = llvm::ConstantStruct::get(Ty, V);
  return new llvm::GlobalVariable(Ty, false,
      llvm::GlobalValue::InternalLinkage, C, Name, &TheModule);
}
llvm::Constant *CGObjCGNU::MakeGlobal(const llvm::ArrayType *Ty,
    std::vector<llvm::Constant*> &V, const std::string &Name) {
  llvm::Constant *C = llvm::ConstantArray::get(Ty, V);
  return new llvm::GlobalVariable(Ty, false,
      llvm::GlobalValue::InternalLinkage, C, Name, &TheModule);
}

/// Generate an NSConstantString object.
//TODO: In case there are any crazy people still using the GNU runtime without
//an OpenStep implementation, this should let them select their own class for
//constant strings.
llvm::Constant *CGObjCGNU::GenerateConstantString(const std::string &Str) {
  std::vector<llvm::Constant*> Ivars;
  Ivars.push_back(NULLPtr);
  Ivars.push_back(MakeConstantString(Str));
  Ivars.push_back(llvm::ConstantInt::get(IntTy, Str.size()));
  llvm::Constant *ObjCStr = MakeGlobal(
    llvm::StructType::get(PtrToInt8Ty, PtrToInt8Ty, IntTy, NULL),
    Ivars, ".objc_str");
  ConstantStrings.push_back(
      llvm::ConstantExpr::getBitCast(ObjCStr, PtrToInt8Ty));
  return ObjCStr;
}

///Generates a message send where the super is the receiver.  This is a message
///send to self with special delivery semantics indicating which class's method
///should be called.
CodeGen::RValue
CGObjCGNU::GenerateMessageSendSuper(CodeGen::CodeGenFunction &CGF,
                                    QualType ResultType,
                                    Selector Sel,
                                    const ObjCInterfaceDecl *Class,
                                    llvm::Value *Receiver,
                                    bool IsClassMessage,
                                    const CallArgList &CallArgs) {
  const ObjCInterfaceDecl *SuperClass = Class->getSuperClass();
  const llvm::Type *ReturnTy = CGM.getTypes().ConvertType(ResultType);
  // TODO: This should be cached, not looked up every time.
  llvm::Value *ReceiverClass = GetClass(CGF.Builder, SuperClass);
  llvm::Value *cmd = GetSelector(CGF.Builder, Sel);
  std::vector<const llvm::Type*> impArgTypes;
  impArgTypes.push_back(Receiver->getType());
  impArgTypes.push_back(SelectorTy);
  
  // Avoid an explicit cast on the IMP by getting a version that has the right
  // return type.
  llvm::FunctionType *impType = llvm::FunctionType::get(ReturnTy, impArgTypes,
                                                        true);
  // Construct the structure used to look up the IMP
  llvm::StructType *ObjCSuperTy = llvm::StructType::get(Receiver->getType(),
      IdTy, NULL);
  llvm::Value *ObjCSuper = CGF.Builder.CreateAlloca(ObjCSuperTy);
  // FIXME: volatility
  CGF.Builder.CreateStore(Receiver, CGF.Builder.CreateStructGEP(ObjCSuper, 0));
  CGF.Builder.CreateStore(ReceiverClass, CGF.Builder.CreateStructGEP(ObjCSuper, 1));

  // Get the IMP
  llvm::Constant *lookupFunction = 
     TheModule.getOrInsertFunction("objc_msg_lookup_super",
                                   llvm::PointerType::getUnqual(impType),
                                   llvm::PointerType::getUnqual(ObjCSuperTy),
                                   SelectorTy, NULL);
  llvm::Value *lookupArgs[] = {ObjCSuper, cmd};
  llvm::Value *imp = CGF.Builder.CreateCall(lookupFunction, lookupArgs,
      lookupArgs+2);

  // Call the method
  CallArgList ActualArgs;
  ActualArgs.push_back(std::make_pair(RValue::get(Receiver), 
                                      CGF.getContext().getObjCIdType()));
  ActualArgs.push_back(std::make_pair(RValue::get(cmd),
                                      CGF.getContext().getObjCSelType()));
  ActualArgs.insert(ActualArgs.end(), CallArgs.begin(), CallArgs.end());
  return CGF.EmitCall(CGM.getTypes().getFunctionInfo(ResultType, ActualArgs), 
                      imp, ActualArgs);
}

/// Generate code for a message send expression.  
CodeGen::RValue
CGObjCGNU::GenerateMessageSend(CodeGen::CodeGenFunction &CGF,
                               QualType ResultType,
                               Selector Sel,
                               llvm::Value *Receiver,
                               bool IsClassMessage,
                               const CallArgList &CallArgs) {
  const llvm::Type *ReturnTy = CGM.getTypes().ConvertType(ResultType);
  llvm::Value *cmd = GetSelector(CGF.Builder, Sel);

  // Look up the method implementation.
  std::vector<const llvm::Type*> impArgTypes;
  const llvm::Type *RetTy;
  //TODO: Revisit this when LLVM supports aggregate return types.
  if (ReturnTy->isSingleValueType() && ReturnTy != llvm::Type::VoidTy) {
    RetTy = ReturnTy;
  } else {
    // For struct returns allocate the space in the caller and pass it up to
    // the sender.
    RetTy = llvm::Type::VoidTy;
    impArgTypes.push_back(llvm::PointerType::getUnqual(ReturnTy));
  }
  impArgTypes.push_back(Receiver->getType());
  impArgTypes.push_back(SelectorTy);
  
  // Avoid an explicit cast on the IMP by getting a version that has the right
  // return type.
  llvm::FunctionType *impType = llvm::FunctionType::get(RetTy, impArgTypes,
                                                        true);
  
  llvm::Constant *lookupFunction = 
     TheModule.getOrInsertFunction("objc_msg_lookup",
                                   llvm::PointerType::getUnqual(impType),
                                   Receiver->getType(), SelectorTy, NULL);
  llvm::Value *imp = CGF.Builder.CreateCall2(lookupFunction, Receiver, cmd);

  // Call the method.
  CallArgList ActualArgs;
  ActualArgs.push_back(std::make_pair(RValue::get(Receiver), 
                                      CGF.getContext().getObjCIdType()));
  ActualArgs.push_back(std::make_pair(RValue::get(cmd),
                                      CGF.getContext().getObjCSelType()));
  ActualArgs.insert(ActualArgs.end(), CallArgs.begin(), CallArgs.end());
  return CGF.EmitCall(CGM.getTypes().getFunctionInfo(ResultType, ActualArgs), 
                      imp, ActualArgs);
}

/// Generates a MethodList.  Used in construction of a objc_class and 
/// objc_category structures.
llvm::Constant *CGObjCGNU::GenerateMethodList(const std::string &ClassName,
                                              const std::string &CategoryName, 
    const llvm::SmallVectorImpl<Selector> &MethodSels, 
    const llvm::SmallVectorImpl<llvm::Constant *> &MethodTypes, 
    bool isClassMethodList) {
  // Get the method structure type.  
  llvm::StructType *ObjCMethodTy = llvm::StructType::get(
    PtrToInt8Ty, // Really a selector, but the runtime creates it us.
    PtrToInt8Ty, // Method types
    llvm::PointerType::getUnqual(IMPTy), //Method pointer
    NULL);
  std::vector<llvm::Constant*> Methods;
  std::vector<llvm::Constant*> Elements;
  for (unsigned int i = 0, e = MethodTypes.size(); i < e; ++i) {
    Elements.clear();
    llvm::Constant *C = 
      CGM.GetAddrOfConstantCString(MethodSels[i].getAsString());
    Elements.push_back(llvm::ConstantExpr::getGetElementPtr(C, Zeros, 2));
    Elements.push_back(
          llvm::ConstantExpr::getGetElementPtr(MethodTypes[i], Zeros, 2));
    llvm::Constant *Method =
      TheModule.getFunction(SymbolNameForMethod(ClassName, CategoryName,
                                                MethodSels[i].getAsString(),
                                                isClassMethodList));
    Method = llvm::ConstantExpr::getBitCast(Method,
        llvm::PointerType::getUnqual(IMPTy));
    Elements.push_back(Method);
    Methods.push_back(llvm::ConstantStruct::get(ObjCMethodTy, Elements));
  }

  // Array of method structures
  llvm::ArrayType *ObjCMethodArrayTy = llvm::ArrayType::get(ObjCMethodTy,
                                                            MethodSels.size());
  llvm::Constant *MethodArray = llvm::ConstantArray::get(ObjCMethodArrayTy,
                                                         Methods);

  // Structure containing list pointer, array and array count
  llvm::SmallVector<const llvm::Type*, 16> ObjCMethodListFields;
  llvm::PATypeHolder OpaqueNextTy = llvm::OpaqueType::get();
  llvm::Type *NextPtrTy = llvm::PointerType::getUnqual(OpaqueNextTy);
  llvm::StructType *ObjCMethodListTy = llvm::StructType::get(NextPtrTy, 
      IntTy, 
      ObjCMethodArrayTy,
      NULL);
  // Refine next pointer type to concrete type
  llvm::cast<llvm::OpaqueType>(
      OpaqueNextTy.get())->refineAbstractTypeTo(ObjCMethodListTy);
  ObjCMethodListTy = llvm::cast<llvm::StructType>(OpaqueNextTy.get());

  Methods.clear();
  Methods.push_back(llvm::ConstantPointerNull::get(
        llvm::PointerType::getUnqual(ObjCMethodListTy)));
  Methods.push_back(llvm::ConstantInt::get(llvm::Type::Int32Ty,
        MethodTypes.size()));
  Methods.push_back(MethodArray);
  
  // Create an instance of the structure
  return MakeGlobal(ObjCMethodListTy, Methods, ".objc_method_list");
}

/// Generates an IvarList.  Used in construction of a objc_class.
llvm::Constant *CGObjCGNU::GenerateIvarList(
    const llvm::SmallVectorImpl<llvm::Constant *>  &IvarNames,
    const llvm::SmallVectorImpl<llvm::Constant *>  &IvarTypes,
    const llvm::SmallVectorImpl<llvm::Constant *>  &IvarOffsets) {
  // Get the method structure type.  
  llvm::StructType *ObjCIvarTy = llvm::StructType::get(
    PtrToInt8Ty,
    PtrToInt8Ty,
    IntTy,
    NULL);
  std::vector<llvm::Constant*> Ivars;
  std::vector<llvm::Constant*> Elements;
  for (unsigned int i = 0, e = IvarNames.size() ; i < e ; i++) {
    Elements.clear();
    Elements.push_back( llvm::ConstantExpr::getGetElementPtr(IvarNames[i],
          Zeros, 2));
    Elements.push_back( llvm::ConstantExpr::getGetElementPtr(IvarTypes[i],
          Zeros, 2));
    Elements.push_back(IvarOffsets[i]);
    Ivars.push_back(llvm::ConstantStruct::get(ObjCIvarTy, Elements));
  }

  // Array of method structures
  llvm::ArrayType *ObjCIvarArrayTy = llvm::ArrayType::get(ObjCIvarTy,
      IvarNames.size());

  
  Elements.clear();
  Elements.push_back(llvm::ConstantInt::get(IntTy, (int)IvarNames.size()));
  Elements.push_back(llvm::ConstantArray::get(ObjCIvarArrayTy, Ivars));
  // Structure containing array and array count
  llvm::StructType *ObjCIvarListTy = llvm::StructType::get(IntTy,
    ObjCIvarArrayTy,
    NULL);

  // Create an instance of the structure
  return MakeGlobal(ObjCIvarListTy, Elements, ".objc_ivar_list");
}

/// Generate a class structure
llvm::Constant *CGObjCGNU::GenerateClassStructure(
    llvm::Constant *MetaClass,
    llvm::Constant *SuperClass,
    unsigned info,
    const char *Name,
    llvm::Constant *Version,
    llvm::Constant *InstanceSize,
    llvm::Constant *IVars,
    llvm::Constant *Methods,
    llvm::Constant *Protocols) {
  // Set up the class structure
  // Note:  Several of these are char*s when they should be ids.  This is
  // because the runtime performs this translation on load.
  llvm::StructType *ClassTy = llvm::StructType::get(
      PtrToInt8Ty,        // class_pointer
      PtrToInt8Ty,        // super_class
      PtrToInt8Ty,        // name
      LongTy,             // version
      LongTy,             // info
      LongTy,             // instance_size
      IVars->getType(),   // ivars
      Methods->getType(), // methods
      // These are all filled in by the runtime, so we pretend 
      PtrTy,              // dtable
      PtrTy,              // subclass_list
      PtrTy,              // sibling_class
      PtrTy,              // protocols
      PtrTy,              // gc_object_type
      NULL);
  llvm::Constant *Zero = llvm::ConstantInt::get(LongTy, 0);
  llvm::Constant *NullP =
    llvm::ConstantPointerNull::get(PtrTy);
  // Fill in the structure
  std::vector<llvm::Constant*> Elements;
  Elements.push_back(llvm::ConstantExpr::getBitCast(MetaClass, PtrToInt8Ty));
  Elements.push_back(SuperClass);
  Elements.push_back(MakeConstantString(Name, ".class_name"));
  Elements.push_back(Zero);
  Elements.push_back(llvm::ConstantInt::get(LongTy, info));
  Elements.push_back(InstanceSize);
  Elements.push_back(IVars);
  Elements.push_back(Methods);
  Elements.push_back(NullP);
  Elements.push_back(NullP);
  Elements.push_back(NullP);
  Elements.push_back(llvm::ConstantExpr::getBitCast(Protocols, PtrTy));
  Elements.push_back(NullP);
  // Create an instance of the structure
  return MakeGlobal(ClassTy, Elements, SymbolNameForClass(Name));
}

llvm::Constant *CGObjCGNU::GenerateProtocolMethodList(
    const llvm::SmallVectorImpl<llvm::Constant *>  &MethodNames,
    const llvm::SmallVectorImpl<llvm::Constant *>  &MethodTypes) {
  // Get the method structure type.  
  llvm::StructType *ObjCMethodDescTy = llvm::StructType::get(
    PtrToInt8Ty, // Really a selector, but the runtime does the casting for us.
    PtrToInt8Ty,
    NULL);
  std::vector<llvm::Constant*> Methods;
  std::vector<llvm::Constant*> Elements;
  for (unsigned int i = 0, e = MethodTypes.size() ; i < e ; i++) {
    Elements.clear();
    Elements.push_back( llvm::ConstantExpr::getGetElementPtr(MethodNames[i],
          Zeros, 2)); 
    Elements.push_back(
          llvm::ConstantExpr::getGetElementPtr(MethodTypes[i], Zeros, 2));
    Methods.push_back(llvm::ConstantStruct::get(ObjCMethodDescTy, Elements));
  }
  llvm::ArrayType *ObjCMethodArrayTy = llvm::ArrayType::get(ObjCMethodDescTy,
      MethodNames.size());
  llvm::Constant *Array = llvm::ConstantArray::get(ObjCMethodArrayTy, Methods);
  llvm::StructType *ObjCMethodDescListTy = llvm::StructType::get(
      IntTy, ObjCMethodArrayTy, NULL);
  Methods.clear();
  Methods.push_back(llvm::ConstantInt::get(IntTy, MethodNames.size()));
  Methods.push_back(Array);
  return MakeGlobal(ObjCMethodDescListTy, Methods, ".objc_method_list");
}
// Create the protocol list structure used in classes, categories and so on
llvm::Constant *CGObjCGNU::GenerateProtocolList(
    const llvm::SmallVectorImpl<std::string> &Protocols) {
  llvm::ArrayType *ProtocolArrayTy = llvm::ArrayType::get(PtrToInt8Ty,
      Protocols.size());
  llvm::StructType *ProtocolListTy = llvm::StructType::get(
      PtrTy, //Should be a recurisve pointer, but it's always NULL here.
      LongTy,//FIXME: Should be size_t
      ProtocolArrayTy,
      NULL);
  std::vector<llvm::Constant*> Elements; 
  for (const std::string *iter = Protocols.begin(), *endIter = Protocols.end();
      iter != endIter ; iter++) {
    llvm::Constant *Ptr =
      llvm::ConstantExpr::getBitCast(ExistingProtocols[*iter], PtrToInt8Ty);
    Elements.push_back(Ptr);
  }
  llvm::Constant * ProtocolArray = llvm::ConstantArray::get(ProtocolArrayTy,
      Elements);
  Elements.clear();
  Elements.push_back(NULLPtr);
  Elements.push_back(llvm::ConstantInt::get(LongTy, Protocols.size()));
  Elements.push_back(ProtocolArray);
  return MakeGlobal(ProtocolListTy, Elements, ".objc_protocol_list");
}

llvm::Value *CGObjCGNU::GenerateProtocolRef(CGBuilderTy &Builder, 
                                            const ObjCProtocolDecl *PD) {
  return ExistingProtocols[PD->getNameAsString()];
}

void CGObjCGNU::GenerateProtocol(const ObjCProtocolDecl *PD) {
  ASTContext &Context = CGM.getContext();
  std::string ProtocolName = PD->getNameAsString();
  llvm::SmallVector<std::string, 16> Protocols;
  for (ObjCProtocolDecl::protocol_iterator PI = PD->protocol_begin(),
       E = PD->protocol_end(); PI != E; ++PI)
    Protocols.push_back((*PI)->getNameAsString());
  llvm::SmallVector<llvm::Constant*, 16> InstanceMethodNames;
  llvm::SmallVector<llvm::Constant*, 16> InstanceMethodTypes;
  for (ObjCProtocolDecl::instmeth_iterator iter = PD->instmeth_begin(),
       E = PD->instmeth_end(); iter != E; iter++) {
    std::string TypeStr;
    Context.getObjCEncodingForMethodDecl(*iter, TypeStr);
    InstanceMethodNames.push_back(
        CGM.GetAddrOfConstantCString((*iter)->getSelector().getAsString()));
    InstanceMethodTypes.push_back(CGM.GetAddrOfConstantCString(TypeStr));
  }
  // Collect information about class methods:
  llvm::SmallVector<llvm::Constant*, 16> ClassMethodNames;
  llvm::SmallVector<llvm::Constant*, 16> ClassMethodTypes;
  for (ObjCProtocolDecl::classmeth_iterator iter = PD->classmeth_begin(),
      endIter = PD->classmeth_end() ; iter != endIter ; iter++) {
    std::string TypeStr;
    Context.getObjCEncodingForMethodDecl((*iter),TypeStr);
    ClassMethodNames.push_back(
        CGM.GetAddrOfConstantCString((*iter)->getSelector().getAsString()));
    ClassMethodTypes.push_back(CGM.GetAddrOfConstantCString(TypeStr));
  }

  llvm::Constant *ProtocolList = GenerateProtocolList(Protocols);
  llvm::Constant *InstanceMethodList =
    GenerateProtocolMethodList(InstanceMethodNames, InstanceMethodTypes);
  llvm::Constant *ClassMethodList =
    GenerateProtocolMethodList(ClassMethodNames, ClassMethodTypes);
  // Protocols are objects containing lists of the methods implemented and
  // protocols adopted.
  llvm::StructType *ProtocolTy = llvm::StructType::get(IdTy,
      PtrToInt8Ty,
      ProtocolList->getType(),
      InstanceMethodList->getType(),
      ClassMethodList->getType(),
      NULL);
  std::vector<llvm::Constant*> Elements; 
  // The isa pointer must be set to a magic number so the runtime knows it's
  // the correct layout.
  Elements.push_back(llvm::ConstantExpr::getIntToPtr(
        llvm::ConstantInt::get(llvm::Type::Int32Ty, ProtocolVersion), IdTy));
  Elements.push_back(MakeConstantString(ProtocolName, ".objc_protocol_name"));
  Elements.push_back(ProtocolList);
  Elements.push_back(InstanceMethodList);
  Elements.push_back(ClassMethodList);
  ExistingProtocols[ProtocolName] = 
    llvm::ConstantExpr::getBitCast(MakeGlobal(ProtocolTy, Elements,
          ".objc_protocol"), IdTy);
}

void CGObjCGNU::GenerateCategory(const ObjCCategoryImplDecl *OCD) {
  std::string ClassName = OCD->getClassInterface()->getNameAsString();
  std::string CategoryName = OCD->getNameAsString();
  // Collect information about instance methods
  llvm::SmallVector<Selector, 16> InstanceMethodSels;
  llvm::SmallVector<llvm::Constant*, 16> InstanceMethodTypes;
  for (ObjCCategoryImplDecl::instmeth_iterator iter = OCD->instmeth_begin(),
      endIter = OCD->instmeth_end() ; iter != endIter ; iter++) {
    InstanceMethodSels.push_back((*iter)->getSelector());
    std::string TypeStr;
    CGM.getContext().getObjCEncodingForMethodDecl(*iter,TypeStr);
    InstanceMethodTypes.push_back(CGM.GetAddrOfConstantCString(TypeStr));
  }

  // Collect information about class methods
  llvm::SmallVector<Selector, 16> ClassMethodSels;
  llvm::SmallVector<llvm::Constant*, 16> ClassMethodTypes;
  for (ObjCCategoryImplDecl::classmeth_iterator iter = OCD->classmeth_begin(),
      endIter = OCD->classmeth_end() ; iter != endIter ; iter++) {
    ClassMethodSels.push_back((*iter)->getSelector());
    std::string TypeStr;
    CGM.getContext().getObjCEncodingForMethodDecl(*iter,TypeStr);
    ClassMethodTypes.push_back(CGM.GetAddrOfConstantCString(TypeStr));
  }

  // Collect the names of referenced protocols
  llvm::SmallVector<std::string, 16> Protocols;
  const ObjCInterfaceDecl *ClassDecl = OCD->getClassInterface();
  const ObjCList<ObjCProtocolDecl> &Protos =ClassDecl->getReferencedProtocols();
  for (ObjCList<ObjCProtocolDecl>::iterator I = Protos.begin(),
       E = Protos.end(); I != E; ++I)
    Protocols.push_back((*I)->getNameAsString());

  std::vector<llvm::Constant*> Elements;
  Elements.push_back(MakeConstantString(CategoryName));
  Elements.push_back(MakeConstantString(ClassName));
  // Instance method list 
  Elements.push_back(llvm::ConstantExpr::getBitCast(GenerateMethodList(
          ClassName, CategoryName, InstanceMethodSels, InstanceMethodTypes,
          false), PtrTy));
  // Class method list
  Elements.push_back(llvm::ConstantExpr::getBitCast(GenerateMethodList(
          ClassName, CategoryName, ClassMethodSels, ClassMethodTypes, true),
        PtrTy));
  // Protocol list
  Elements.push_back(llvm::ConstantExpr::getBitCast(
        GenerateProtocolList(Protocols), PtrTy));
  Categories.push_back(llvm::ConstantExpr::getBitCast(
        MakeGlobal(llvm::StructType::get(PtrToInt8Ty, PtrToInt8Ty, PtrTy,
            PtrTy, PtrTy, NULL), Elements), PtrTy));
}

void CGObjCGNU::GenerateClass(const ObjCImplementationDecl *OID) {
  ASTContext &Context = CGM.getContext();

  // Get the superclass name.
  const ObjCInterfaceDecl * SuperClassDecl = 
    OID->getClassInterface()->getSuperClass();
  std::string SuperClassName;
  if (SuperClassDecl)
    SuperClassName = SuperClassDecl->getNameAsString();

  // Get the class name
  ObjCInterfaceDecl * ClassDecl = (ObjCInterfaceDecl*)OID->getClassInterface();
  std::string ClassName = ClassDecl->getNameAsString();

  // Get the size of instances.  For runtimes that support late-bound instances
  // this should probably be something different (size just of instance
  // varaibles in this class, not superclasses?).
  int instanceSize = 0;
  const llvm::Type *ObjTy = 0;
  if (!LateBoundIVars()) {
    ObjTy = CGM.getTypes().ConvertType(Context.getObjCInterfaceType(ClassDecl));
    instanceSize = CGM.getTargetData().getTypePaddedSize(ObjTy);
  } else {
    // This is required by newer ObjC runtimes.
    assert(0 && "Late-bound instance variables not yet supported");
  }

  // Collect information about instance variables.
  llvm::SmallVector<llvm::Constant*, 16> IvarNames;
  llvm::SmallVector<llvm::Constant*, 16> IvarTypes;
  llvm::SmallVector<llvm::Constant*, 16> IvarOffsets;
  const llvm::StructLayout *Layout =
    CGM.getTargetData().getStructLayout(cast<llvm::StructType>(ObjTy));
  ObjTy = llvm::PointerType::getUnqual(ObjTy);
  for (ObjCInterfaceDecl::ivar_iterator iter = ClassDecl->ivar_begin(),
      endIter = ClassDecl->ivar_end() ; iter != endIter ; iter++) {
      // Store the name
      IvarNames.push_back(CGM.GetAddrOfConstantCString((*iter)
                                                         ->getNameAsString()));
      // Get the type encoding for this ivar
      std::string TypeStr;
      Context.getObjCEncodingForType((*iter)->getType(), TypeStr);
      IvarTypes.push_back(CGM.GetAddrOfConstantCString(TypeStr));
      // Get the offset
      FieldDecl *Field = ClassDecl->lookupFieldDeclForIvar(Context, (*iter));
      int offset =
        (int)Layout->getElementOffset(CGM.getTypes().getLLVMFieldNo(Field));
      IvarOffsets.push_back(
          llvm::ConstantInt::get(llvm::Type::Int32Ty, offset));
  }

  // Collect information about instance methods
  llvm::SmallVector<Selector, 16> InstanceMethodSels;
  llvm::SmallVector<llvm::Constant*, 16> InstanceMethodTypes;
  for (ObjCImplementationDecl::instmeth_iterator iter = OID->instmeth_begin(),
      endIter = OID->instmeth_end() ; iter != endIter ; iter++) {
    InstanceMethodSels.push_back((*iter)->getSelector());
    std::string TypeStr;
    Context.getObjCEncodingForMethodDecl((*iter),TypeStr);
    InstanceMethodTypes.push_back(CGM.GetAddrOfConstantCString(TypeStr));
  }

  // Collect information about class methods
  llvm::SmallVector<Selector, 16> ClassMethodSels;
  llvm::SmallVector<llvm::Constant*, 16> ClassMethodTypes;
  for (ObjCImplementationDecl::classmeth_iterator iter = OID->classmeth_begin(),
      endIter = OID->classmeth_end() ; iter != endIter ; iter++) {
    ClassMethodSels.push_back((*iter)->getSelector());
    std::string TypeStr;
    Context.getObjCEncodingForMethodDecl((*iter),TypeStr);
    ClassMethodTypes.push_back(CGM.GetAddrOfConstantCString(TypeStr));
  }
  // Collect the names of referenced protocols
  llvm::SmallVector<std::string, 16> Protocols;
  const ObjCList<ObjCProtocolDecl> &Protos =ClassDecl->getReferencedProtocols();
  for (ObjCList<ObjCProtocolDecl>::iterator I = Protos.begin(),
       E = Protos.end(); I != E; ++I)
    Protocols.push_back((*I)->getNameAsString());



  // Get the superclass pointer.
  llvm::Constant *SuperClass;
  if (!SuperClassName.empty()) {
    SuperClass = MakeConstantString(SuperClassName, ".super_class_name");
  } else {
    SuperClass = llvm::ConstantPointerNull::get(PtrToInt8Ty);
  }
  // Empty vector used to construct empty method lists
  llvm::SmallVector<llvm::Constant*, 1>  empty;
  // Generate the method and instance variable lists
  llvm::Constant *MethodList = GenerateMethodList(ClassName, "",
      InstanceMethodSels, InstanceMethodTypes, false);
  llvm::Constant *ClassMethodList = GenerateMethodList(ClassName, "",
      ClassMethodSels, ClassMethodTypes, true);
  llvm::Constant *IvarList = GenerateIvarList(IvarNames, IvarTypes,
      IvarOffsets);
  //Generate metaclass for class methods
  llvm::Constant *MetaClassStruct = GenerateClassStructure(NULLPtr,
      NULLPtr, 0x2L, /*name*/"", 0, Zeros[0], GenerateIvarList(
        empty, empty, empty), ClassMethodList, NULLPtr);
  // Generate the class structure
  llvm::Constant *ClassStruct =
    GenerateClassStructure(MetaClassStruct, SuperClass, 0x1L,
                           ClassName.c_str(), 0,
      llvm::ConstantInt::get(LongTy, instanceSize), IvarList,
      MethodList, GenerateProtocolList(Protocols));
  // Add class structure to list to be added to the symtab later
  ClassStruct = llvm::ConstantExpr::getBitCast(ClassStruct, PtrToInt8Ty);
  Classes.push_back(ClassStruct);
}

llvm::Function *CGObjCGNU::ModuleInitFunction() { 
  // Only emit an ObjC load function if no Objective-C stuff has been called
  if (Classes.empty() && Categories.empty() && ConstantStrings.empty() &&
      ExistingProtocols.empty() && TypedSelectors.empty() &&
      UntypedSelectors.empty())
    return NULL;

  const llvm::StructType *SelStructTy = dyn_cast<llvm::StructType>(
          SelectorTy->getElementType());
  const llvm::Type *SelStructPtrTy = SelectorTy;
  bool isSelOpaque = false;
  if (SelStructTy == 0) {
    SelStructTy = llvm::StructType::get(PtrToInt8Ty, PtrToInt8Ty, NULL);
    SelStructPtrTy = llvm::PointerType::getUnqual(SelStructTy);
    isSelOpaque = true;
  }

  // Name the ObjC types to make the IR a bit easier to read
  TheModule.addTypeName(".objc_selector", SelStructPtrTy);
  TheModule.addTypeName(".objc_id", IdTy);
  TheModule.addTypeName(".objc_imp", IMPTy);

  std::vector<llvm::Constant*> Elements;
  // Generate statics list:
  llvm::ArrayType *StaticsArrayTy = llvm::ArrayType::get(PtrToInt8Ty,
      ConstantStrings.size() + 1);
  ConstantStrings.push_back(NULLPtr);
  Elements.push_back(MakeConstantString("NSConstantString",
        ".objc_static_class_name"));
  Elements.push_back(llvm::ConstantArray::get(StaticsArrayTy, ConstantStrings));
  llvm::StructType *StaticsListTy = 
    llvm::StructType::get(PtrToInt8Ty, StaticsArrayTy, NULL);
  llvm::Type *StaticsListPtrTy = llvm::PointerType::getUnqual(StaticsListTy);
  llvm::Constant *Statics = 
    MakeGlobal(StaticsListTy, Elements, ".objc_statics");
  llvm::ArrayType *StaticsListArrayTy =
    llvm::ArrayType::get(StaticsListPtrTy, 2);
  Elements.clear();
  Elements.push_back(Statics);
  Elements.push_back(llvm::Constant::getNullValue(StaticsListPtrTy));
  Statics = MakeGlobal(StaticsListArrayTy, Elements, ".objc_statics_ptr");
  Statics = llvm::ConstantExpr::getBitCast(Statics, PtrTy);
  // Array of classes, categories, and constant objects
  llvm::ArrayType *ClassListTy = llvm::ArrayType::get(PtrToInt8Ty,
      Classes.size() + Categories.size()  + 2);
  llvm::StructType *SymTabTy = llvm::StructType::get(LongTy, SelStructPtrTy,
                                                     llvm::Type::Int16Ty,
                                                     llvm::Type::Int16Ty,
                                                     ClassListTy, NULL);

  Elements.clear();
  // Pointer to an array of selectors used in this module.
  std::vector<llvm::Constant*> Selectors;
  for (std::map<TypedSelector, llvm::GlobalAlias*>::iterator
     iter = TypedSelectors.begin(), iterEnd = TypedSelectors.end();
     iter != iterEnd ; ++iter) {
    Elements.push_back(MakeConstantString(iter->first.first, ".objc_sel_name"));
    Elements.push_back(MakeConstantString(iter->first.second,
                                          ".objc_sel_types"));
    Selectors.push_back(llvm::ConstantStruct::get(SelStructTy, Elements));
    Elements.clear();
  }
  for (llvm::StringMap<llvm::GlobalAlias*>::iterator
      iter = UntypedSelectors.begin(), iterEnd = UntypedSelectors.end();
      iter != iterEnd; ++iter) {
    Elements.push_back(
        MakeConstantString(iter->getKeyData(), ".objc_sel_name"));
    Elements.push_back(NULLPtr);
    Selectors.push_back(llvm::ConstantStruct::get(SelStructTy, Elements));
    Elements.clear();
  }
  Elements.push_back(NULLPtr);
  Elements.push_back(NULLPtr);
  Selectors.push_back(llvm::ConstantStruct::get(SelStructTy, Elements));
  Elements.clear();
  // Number of static selectors
  Elements.push_back(llvm::ConstantInt::get(LongTy, Selectors.size() ));
  llvm::Constant *SelectorList = MakeGlobal(
          llvm::ArrayType::get(SelStructTy, Selectors.size()), Selectors,
          ".objc_selector_list");
  Elements.push_back(llvm::ConstantExpr::getBitCast(SelectorList, 
    SelStructPtrTy));

  // Now that all of the static selectors exist, create pointers to them.
  int index = 0;
  for (std::map<TypedSelector, llvm::GlobalAlias*>::iterator
     iter=TypedSelectors.begin(), iterEnd =TypedSelectors.end();
     iter != iterEnd; ++iter) {
    llvm::Constant *Idxs[] = {Zeros[0],
      llvm::ConstantInt::get(llvm::Type::Int32Ty, index++), Zeros[0]};
    llvm::Constant *SelPtr = new llvm::GlobalVariable(SelStructPtrTy,
        true, llvm::GlobalValue::InternalLinkage,
        llvm::ConstantExpr::getGetElementPtr(SelectorList, Idxs, 2),
        ".objc_sel_ptr", &TheModule);
    // If selectors are defined as an opaque type, cast the pointer to this
    // type.
    if (isSelOpaque) {
      SelPtr = llvm::ConstantExpr::getBitCast(SelPtr,
        llvm::PointerType::getUnqual(SelectorTy));
    }
    (*iter).second->setAliasee(SelPtr);
  }
  for (llvm::StringMap<llvm::GlobalAlias*>::iterator
      iter=UntypedSelectors.begin(), iterEnd = UntypedSelectors.end();
      iter != iterEnd; iter++) {
    llvm::Constant *Idxs[] = {Zeros[0],
      llvm::ConstantInt::get(llvm::Type::Int32Ty, index++), Zeros[0]};
    llvm::Constant *SelPtr = new llvm::GlobalVariable(SelStructPtrTy, true,
        llvm::GlobalValue::InternalLinkage,
        llvm::ConstantExpr::getGetElementPtr(SelectorList, Idxs, 2),
        ".objc_sel_ptr", &TheModule);
    // If selectors are defined as an opaque type, cast the pointer to this
    // type.
    if (isSelOpaque) {
      SelPtr = llvm::ConstantExpr::getBitCast(SelPtr,
        llvm::PointerType::getUnqual(SelectorTy));
    }
    (*iter).second->setAliasee(SelPtr);
  }
  // Number of classes defined.
  Elements.push_back(llvm::ConstantInt::get(llvm::Type::Int16Ty, 
        Classes.size()));
  // Number of categories defined
  Elements.push_back(llvm::ConstantInt::get(llvm::Type::Int16Ty, 
        Categories.size()));
  // Create an array of classes, then categories, then static object instances
  Classes.insert(Classes.end(), Categories.begin(), Categories.end());
  //  NULL-terminated list of static object instances (mainly constant strings)
  Classes.push_back(Statics);
  Classes.push_back(NULLPtr);
  llvm::Constant *ClassList = llvm::ConstantArray::get(ClassListTy, Classes);
  Elements.push_back(ClassList);
  // Construct the symbol table 
  llvm::Constant *SymTab= MakeGlobal(SymTabTy, Elements);

  // The symbol table is contained in a module which has some version-checking
  // constants
  llvm::StructType * ModuleTy = llvm::StructType::get(LongTy, LongTy,
      PtrToInt8Ty, llvm::PointerType::getUnqual(SymTabTy), NULL);
  Elements.clear();
  // Runtime version used for compatibility checking.
  Elements.push_back(llvm::ConstantInt::get(LongTy, RuntimeVersion));
  //FIXME: Should be sizeof(ModuleTy)
  Elements.push_back(llvm::ConstantInt::get(LongTy, 16));
  //FIXME: Should be the path to the file where this module was declared
  Elements.push_back(NULLPtr);
  Elements.push_back(SymTab);
  llvm::Value *Module = MakeGlobal(ModuleTy, Elements);

  // Create the load function calling the runtime entry point with the module
  // structure
  std::vector<const llvm::Type*> VoidArgs;
  llvm::Function * LoadFunction = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::VoidTy, VoidArgs, false),
      llvm::GlobalValue::InternalLinkage, ".objc_load_function",
      &TheModule);
  llvm::BasicBlock *EntryBB = llvm::BasicBlock::Create("entry", LoadFunction);
  CGBuilderTy Builder;
  Builder.SetInsertPoint(EntryBB);
  llvm::Value *Register = TheModule.getOrInsertFunction("__objc_exec_class",
      llvm::Type::VoidTy, llvm::PointerType::getUnqual(ModuleTy), NULL);
  Builder.CreateCall(Register, Module);
  Builder.CreateRetVoid();
  return LoadFunction;
}

llvm::Function *CGObjCGNU::GenerateMethod(const ObjCMethodDecl *OMD,
                                          const ObjCContainerDecl *CD) {  
  const ObjCCategoryImplDecl *OCD = 
    dyn_cast<ObjCCategoryImplDecl>(OMD->getDeclContext());
  std::string CategoryName = OCD ? OCD->getNameAsString() : "";
  std::string ClassName = OMD->getClassInterface()->getNameAsString();
  std::string MethodName = OMD->getSelector().getAsString();
  bool isClassMethod = !OMD->isInstanceMethod();

  CodeGenTypes &Types = CGM.getTypes();
  const llvm::FunctionType *MethodTy = 
    Types.GetFunctionType(Types.getFunctionInfo(OMD), OMD->isVariadic());
  std::string FunctionName = SymbolNameForMethod(ClassName, CategoryName,
      MethodName, isClassMethod);

  llvm::Function *Method = llvm::Function::Create(MethodTy,
      llvm::GlobalValue::InternalLinkage,
      FunctionName,
      &TheModule);
  return Method;
}

llvm::Function *CGObjCGNU::GetPropertyGetFunction() {
  return 0;
}

llvm::Function *CGObjCGNU::GetPropertySetFunction() {
  return 0;
}

llvm::Function *CGObjCGNU::EnumerationMutationFunction() {
  return 0;
}

void CGObjCGNU::EmitTryOrSynchronizedStmt(CodeGen::CodeGenFunction &CGF,
                                          const Stmt &S) {
  CGF.ErrorUnsupported(&S, "@try/@synchronized statement");
}

void CGObjCGNU::EmitThrowStmt(CodeGen::CodeGenFunction &CGF,
                              const ObjCAtThrowStmt &S) {
  CGF.ErrorUnsupported(&S, "@throw statement");
}

llvm::Value * CGObjCGNU::EmitObjCWeakRead(CodeGen::CodeGenFunction &CGF,
                                          llvm::Value *AddrWeakObj)
{
  return 0;
}

void CGObjCGNU::EmitObjCWeakAssign(CodeGen::CodeGenFunction &CGF,
                                   llvm::Value *src, llvm::Value *dst)
{
  return;
}

void CGObjCGNU::EmitObjCGlobalAssign(CodeGen::CodeGenFunction &CGF,
                                     llvm::Value *src, llvm::Value *dst)
{
  return;
}

void CGObjCGNU::EmitObjCIvarAssign(CodeGen::CodeGenFunction &CGF,
                                   llvm::Value *src, llvm::Value *dst)
{
  return;
}

void CGObjCGNU::EmitObjCStrongCastAssign(CodeGen::CodeGenFunction &CGF,
                                         llvm::Value *src, llvm::Value *dst)
{
  return;
}

llvm::Value *CGObjCGNU::EmitObjCValueForIvar(CodeGen::CodeGenFunction &CGF,
                                             QualType ObjectTy,
                                             llvm::Value *BaseValue,
                                             const ObjCIvarDecl *Ivar,
                                             const FieldDecl *Field,
                                             unsigned CVRQualifiers) {
  // TODO:  Add a special case for isa (index 0)
  unsigned Index = CGM.getTypes().getLLVMFieldNo(Field);
  llvm::Value *V = CGF.Builder.CreateStructGEP(BaseValue, Index, "tmp");
  return V;
}

CodeGen::CGObjCRuntime *CodeGen::CreateGNUObjCRuntime(CodeGen::CodeGenModule &CGM){
  return new CGObjCGNU(CGM);
}
