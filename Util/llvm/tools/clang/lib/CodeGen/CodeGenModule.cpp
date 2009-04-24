//===--- CodeGenModule.cpp - Emit LLVM Code from ASTs for a Module --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This coordinates the per-module state used while generating code.
//
//===----------------------------------------------------------------------===//

#include "CGDebugInfo.h"
#include "CodeGenModule.h"
#include "CodeGenFunction.h"
#include "CGCall.h"
#include "CGObjCRuntime.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclCXX.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/CallingConv.h"
#include "llvm/Module.h"
#include "llvm/Intrinsics.h"
#include "llvm/Target/TargetData.h"
using namespace clang;
using namespace CodeGen;


CodeGenModule::CodeGenModule(ASTContext &C, const LangOptions &LO,
                             llvm::Module &M, const llvm::TargetData &TD,
                             Diagnostic &diags, bool GenerateDebugInfo)
  : Context(C), Features(LO), TheModule(M), TheTargetData(TD), Diags(diags),
    Types(C, M, TD), Runtime(0), MemCpyFn(0), MemMoveFn(0), MemSetFn(0),
    CFConstantStringClassRef(0) {

  if (Features.ObjC1) {
    if (Features.NeXTRuntime) {
      Runtime = Features.ObjCNonFragileABI ? CreateMacNonFragileABIObjCRuntime(*this) 
                                       : CreateMacObjCRuntime(*this);
    } else {
      Runtime = CreateGNUObjCRuntime(*this);
    }
  }

  // If debug info generation is enabled, create the CGDebugInfo object.
  DebugInfo = GenerateDebugInfo ? new CGDebugInfo(this) : 0;      
}

CodeGenModule::~CodeGenModule() {
  delete Runtime;
  delete DebugInfo;
}

void CodeGenModule::Release() {
  EmitStatics();
  EmitAliases();
  if (Runtime)
    if (llvm::Function *ObjCInitFunction = Runtime->ModuleInitFunction())
      AddGlobalCtor(ObjCInitFunction);
  EmitCtorList(GlobalCtors, "llvm.global_ctors");
  EmitCtorList(GlobalDtors, "llvm.global_dtors");
  EmitAnnotations();
  BindRuntimeFunctions();
}

void CodeGenModule::BindRuntimeFunctions() {
  // Deal with protecting runtime function names.
  for (unsigned i = 0, e = RuntimeFunctions.size(); i < e; ++i) {
    llvm::Function *Fn = RuntimeFunctions[i].first;
    const std::string &Name = RuntimeFunctions[i].second;
    
    // Discard unused runtime functions.
    if (Fn->use_empty()) {
      Fn->eraseFromParent();
      continue;
    }
      
    // See if there is a conflict against a function.
    llvm::Function *Conflict = TheModule.getFunction(Name);
    if (Conflict) {
      // Decide which version to take. If the conflict is a definition
      // we are forced to take that, otherwise assume the runtime
      // knows best.
      if (!Conflict->isDeclaration()) {
        llvm::Value *Casted = 
          llvm::ConstantExpr::getBitCast(Conflict, Fn->getType());
        Fn->replaceAllUsesWith(Casted);
        Fn->eraseFromParent();
      } else {
        Fn->takeName(Conflict);
        llvm::Value *Casted = 
          llvm::ConstantExpr::getBitCast(Fn, Conflict->getType());
        Conflict->replaceAllUsesWith(Casted);
        Conflict->eraseFromParent();
      }
    } else {
      // FIXME: There still may be conflicts with aliases and
      // variables. 
      Fn->setName(Name);
    }
  }
}

/// ErrorUnsupported - Print out an error that codegen doesn't support the
/// specified stmt yet.
void CodeGenModule::ErrorUnsupported(const Stmt *S, const char *Type,
                                     bool OmitOnError) {
  if (OmitOnError && getDiags().hasErrorOccurred())
    return;
  unsigned DiagID = getDiags().getCustomDiagID(Diagnostic::Error, 
                                               "cannot codegen this %0 yet");
  std::string Msg = Type;
  getDiags().Report(Context.getFullLoc(S->getLocStart()), DiagID)
    << Msg << S->getSourceRange();
}

/// ErrorUnsupported - Print out an error that codegen doesn't support the
/// specified decl yet.
void CodeGenModule::ErrorUnsupported(const Decl *D, const char *Type,
                                     bool OmitOnError) {
  if (OmitOnError && getDiags().hasErrorOccurred())
    return;
  unsigned DiagID = getDiags().getCustomDiagID(Diagnostic::Error, 
                                               "cannot codegen this %0 yet");
  std::string Msg = Type;
  getDiags().Report(Context.getFullLoc(D->getLocation()), DiagID) << Msg;
}

/// setGlobalVisibility - Set the visibility for the given LLVM
/// GlobalValue according to the given clang AST visibility value.
static void setGlobalVisibility(llvm::GlobalValue *GV,
                                VisibilityAttr::VisibilityTypes Vis) {
  switch (Vis) {
  default: assert(0 && "Unknown visibility!");
  case VisibilityAttr::DefaultVisibility:
    GV->setVisibility(llvm::GlobalValue::DefaultVisibility);
    break;
  case VisibilityAttr::HiddenVisibility:
    GV->setVisibility(llvm::GlobalValue::HiddenVisibility);
    break;
  case VisibilityAttr::ProtectedVisibility:
    GV->setVisibility(llvm::GlobalValue::ProtectedVisibility);
    break;
  }
}

/// AddGlobalCtor - Add a function to the list that will be called before
/// main() runs.
void CodeGenModule::AddGlobalCtor(llvm::Function * Ctor, int Priority) {
  // FIXME: Type coercion of void()* types.
  GlobalCtors.push_back(std::make_pair(Ctor, Priority));
}

/// AddGlobalDtor - Add a function to the list that will be called
/// when the module is unloaded.
void CodeGenModule::AddGlobalDtor(llvm::Function * Dtor, int Priority) {
  // FIXME: Type coercion of void()* types.
  GlobalDtors.push_back(std::make_pair(Dtor, Priority));
}

void CodeGenModule::EmitCtorList(const CtorList &Fns, const char *GlobalName) {
  // Ctor function type is void()*.
  llvm::FunctionType* CtorFTy =
    llvm::FunctionType::get(llvm::Type::VoidTy, 
                            std::vector<const llvm::Type*>(),
                            false);
  llvm::Type *CtorPFTy = llvm::PointerType::getUnqual(CtorFTy);

  // Get the type of a ctor entry, { i32, void ()* }.
  llvm::StructType* CtorStructTy = 
    llvm::StructType::get(llvm::Type::Int32Ty, 
                          llvm::PointerType::getUnqual(CtorFTy), NULL);

  // Construct the constructor and destructor arrays.
  std::vector<llvm::Constant*> Ctors;
  for (CtorList::const_iterator I = Fns.begin(), E = Fns.end(); I != E; ++I) {
    std::vector<llvm::Constant*> S;
    S.push_back(llvm::ConstantInt::get(llvm::Type::Int32Ty, I->second, false));
    S.push_back(llvm::ConstantExpr::getBitCast(I->first, CtorPFTy));
    Ctors.push_back(llvm::ConstantStruct::get(CtorStructTy, S));
  }

  if (!Ctors.empty()) {
    llvm::ArrayType *AT = llvm::ArrayType::get(CtorStructTy, Ctors.size());
    new llvm::GlobalVariable(AT, false,
                             llvm::GlobalValue::AppendingLinkage,
                             llvm::ConstantArray::get(AT, Ctors),
                             GlobalName, 
                             &TheModule);
  }
}

void CodeGenModule::EmitAnnotations() {
  if (Annotations.empty())
    return;

  // Create a new global variable for the ConstantStruct in the Module.
  llvm::Constant *Array =
  llvm::ConstantArray::get(llvm::ArrayType::get(Annotations[0]->getType(),
                                                Annotations.size()),
                           Annotations);
  llvm::GlobalValue *gv = 
  new llvm::GlobalVariable(Array->getType(), false,  
                           llvm::GlobalValue::AppendingLinkage, Array, 
                           "llvm.global.annotations", &TheModule);
  gv->setSection("llvm.metadata");
}

static void SetGlobalValueAttributes(const Decl *D, 
                                     bool IsInternal,
                                     bool IsInline,
                                     llvm::GlobalValue *GV,
                                     bool ForDefinition) {
  // FIXME: Set up linkage and many other things.  Note, this is a simple 
  // approximation of what we really want.
  if (!ForDefinition) {
    // Only a few attributes are set on declarations.
    if (D->getAttr<DLLImportAttr>()) {
      // The dllimport attribute is overridden by a subsequent declaration as
      // dllexport.
      if (!D->getAttr<DLLExportAttr>()) {
        // dllimport attribute can be applied only to function decls, not to
        // definitions.
        if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
          if (!FD->getBody())
            GV->setLinkage(llvm::Function::DLLImportLinkage);
        } else
          GV->setLinkage(llvm::Function::DLLImportLinkage);
      }
    }
  } else {
    if (IsInternal) {
      GV->setLinkage(llvm::Function::InternalLinkage);
    } else {
      if (D->getAttr<DLLExportAttr>()) {
        if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
          // The dllexport attribute is ignored for undefined symbols.
          if (FD->getBody())
            GV->setLinkage(llvm::Function::DLLExportLinkage);
        } else
          GV->setLinkage(llvm::Function::DLLExportLinkage);
      } else if (D->getAttr<WeakAttr>() || IsInline)
        GV->setLinkage(llvm::Function::WeakLinkage);
    }
  }

  // FIXME: Figure out the relative priority of the attribute,
  // -fvisibility, and private_extern.
  if (const VisibilityAttr *attr = D->getAttr<VisibilityAttr>())
    setGlobalVisibility(GV, attr->getVisibility());
  // FIXME: else handle -fvisibility

  if (const AsmLabelAttr *ALA = D->getAttr<AsmLabelAttr>()) {
    // Prefaced with special LLVM marker to indicate that the name
    // should not be munged.
    GV->setName("\01" + ALA->getLabel());
  }
}

void CodeGenModule::SetFunctionAttributes(const Decl *D,
                                          const CGFunctionInfo &Info, 
                                          llvm::Function *F) {
  AttributeListType AttributeList;
  ConstructAttributeList(Info, D, AttributeList);

  F->setAttributes(llvm::AttrListPtr::get(AttributeList.begin(),
                                        AttributeList.size()));

  // Set the appropriate calling convention for the Function.
  if (D->getAttr<FastCallAttr>())
    F->setCallingConv(llvm::CallingConv::X86_FastCall);

  if (D->getAttr<StdCallAttr>())
    F->setCallingConv(llvm::CallingConv::X86_StdCall);
}

/// SetFunctionAttributesForDefinition - Set function attributes
/// specific to a function definition.
void CodeGenModule::SetFunctionAttributesForDefinition(const Decl *D,
                                                       llvm::Function *F) {
  if (isa<ObjCMethodDecl>(D)) {
    SetGlobalValueAttributes(D, true, false, F, true);
  } else {
    const FunctionDecl *FD = cast<FunctionDecl>(D);
    SetGlobalValueAttributes(FD, FD->getStorageClass() == FunctionDecl::Static,
                             FD->isInline(), F, true);
  }
                             
  if (!Features.Exceptions)
    F->addFnAttr(llvm::Attribute::NoUnwind);  

  if (D->getAttr<AlwaysInlineAttr>())
    F->addFnAttr(llvm::Attribute::AlwaysInline);
}

void CodeGenModule::SetMethodAttributes(const ObjCMethodDecl *MD,
                                        llvm::Function *F) {
  SetFunctionAttributes(MD, getTypes().getFunctionInfo(MD), F);
  
  SetFunctionAttributesForDefinition(MD, F);
}

void CodeGenModule::SetFunctionAttributes(const FunctionDecl *FD,
                                          llvm::Function *F) {
  SetFunctionAttributes(FD, getTypes().getFunctionInfo(FD), F);
  
  SetGlobalValueAttributes(FD, FD->getStorageClass() == FunctionDecl::Static,
                           FD->isInline(), F, false);
}


void CodeGenModule::EmitAliases() {
  for (unsigned i = 0, e = Aliases.size(); i != e; ++i) {
    const FunctionDecl *D = Aliases[i];
    const AliasAttr *AA = D->getAttr<AliasAttr>();

    // This is something of a hack, if the FunctionDecl got overridden
    // then its attributes will be moved to the new declaration. In
    // this case the current decl has no alias attribute, but we will
    // eventually see it.
    if (!AA)
      continue;

    const std::string& aliaseeName = AA->getAliasee();
    llvm::Function *aliasee = getModule().getFunction(aliaseeName);
    if (!aliasee) {
      // FIXME: This isn't unsupported, this is just an error, which
      // sema should catch, but...
      ErrorUnsupported(D, "alias referencing a missing function");
      continue;
    }

    llvm::GlobalValue *GA = 
      new llvm::GlobalAlias(aliasee->getType(),
                            llvm::Function::ExternalLinkage,
                            D->getNameAsString(), aliasee, &getModule());
    
    llvm::GlobalValue *&Entry = GlobalDeclMap[D->getIdentifier()];
    if (Entry) {
      // If we created a dummy function for this then replace it.
      GA->takeName(Entry);
            
      llvm::Value *Casted = 
        llvm::ConstantExpr::getBitCast(GA, Entry->getType());
      Entry->replaceAllUsesWith(Casted);
      Entry->eraseFromParent();

      Entry = GA;
    }

    // Alias should never be internal or inline.
    SetGlobalValueAttributes(D, false, false, GA, true);
  }
}

void CodeGenModule::EmitStatics() {
  // Emit code for each used static decl encountered.  Since a previously unused
  // static decl may become used during the generation of code for a static
  // function, iterate until no changes are made.
  bool Changed;
  do {
    Changed = false;
    
    for (std::list<const ValueDecl*>::iterator i = StaticDecls.begin(),
         e = StaticDecls.end(); i != e; ) {
      const ValueDecl *D = *i;
      
      // Check if we have used a decl with the same name
      // FIXME: The AST should have some sort of aggregate decls or
      // global symbol map.
      // FIXME: This is missing some important cases. For example, we
      // need to check for uses in an alias and in a constructor.
      if (!GlobalDeclMap.count(D->getIdentifier())) {
        i++;
        continue;
      }
      
      // Emit the definition.
      EmitGlobalDefinition(D);

      // Erase the used decl from the list.
      i = StaticDecls.erase(i);

      // Remember that we made a change.
      Changed = true;
    }
  } while (Changed);
}

/// EmitAnnotateAttr - Generate the llvm::ConstantStruct which contains the 
/// annotation information for a given GlobalValue.  The annotation struct is
/// {i8 *, i8 *, i8 *, i32}.  The first field is a constant expression, the 
/// GlobalValue being annotated.  The second field is the constant string 
/// created from the AnnotateAttr's annotation.  The third field is a constant 
/// string containing the name of the translation unit.  The fourth field is
/// the line number in the file of the annotated value declaration.
///
/// FIXME: this does not unique the annotation string constants, as llvm-gcc
///        appears to.
///
llvm::Constant *CodeGenModule::EmitAnnotateAttr(llvm::GlobalValue *GV, 
                                                const AnnotateAttr *AA,
                                                unsigned LineNo) {
  llvm::Module *M = &getModule();

  // get [N x i8] constants for the annotation string, and the filename string
  // which are the 2nd and 3rd elements of the global annotation structure.
  const llvm::Type *SBP = llvm::PointerType::getUnqual(llvm::Type::Int8Ty);
  llvm::Constant *anno = llvm::ConstantArray::get(AA->getAnnotation(), true);
  llvm::Constant *unit = llvm::ConstantArray::get(M->getModuleIdentifier(),
                                                  true);

  // Get the two global values corresponding to the ConstantArrays we just
  // created to hold the bytes of the strings.
  llvm::GlobalValue *annoGV = 
  new llvm::GlobalVariable(anno->getType(), false,
                           llvm::GlobalValue::InternalLinkage, anno,
                           GV->getName() + ".str", M);
  // translation unit name string, emitted into the llvm.metadata section.
  llvm::GlobalValue *unitGV =
  new llvm::GlobalVariable(unit->getType(), false,
                           llvm::GlobalValue::InternalLinkage, unit, ".str", M);

  // Create the ConstantStruct that is the global annotion.
  llvm::Constant *Fields[4] = {
    llvm::ConstantExpr::getBitCast(GV, SBP),
    llvm::ConstantExpr::getBitCast(annoGV, SBP),
    llvm::ConstantExpr::getBitCast(unitGV, SBP),
    llvm::ConstantInt::get(llvm::Type::Int32Ty, LineNo)
  };
  return llvm::ConstantStruct::get(Fields, 4, false);
}

void CodeGenModule::EmitGlobal(const ValueDecl *Global) {
  bool isDef, isStatic;

  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(Global)) {
    // Aliases are deferred until code for everything else has been
    // emitted.
    if (FD->getAttr<AliasAttr>()) {
      assert(!FD->isThisDeclarationADefinition() && 
             "Function alias cannot have a definition!");
      Aliases.push_back(FD);
      return;
    }

    isDef = FD->isThisDeclarationADefinition();
    isStatic = FD->getStorageClass() == FunctionDecl::Static;
  } else if (const VarDecl *VD = cast<VarDecl>(Global)) {
    assert(VD->isFileVarDecl() && "Cannot emit local var decl as global.");

    isDef = !((VD->getStorageClass() == VarDecl::Extern ||
               VD->getStorageClass() == VarDecl::PrivateExtern) && 
              VD->getInit() == 0);
    isStatic = VD->getStorageClass() == VarDecl::Static;
  } else {
    assert(0 && "Invalid argument to EmitGlobal");
    return;
  }

  // Forward declarations are emitted lazily on first use.
  if (!isDef)
    return;

  // If the global is a static, defer code generation until later so
  // we can easily omit unused statics.
  if (isStatic) {
    StaticDecls.push_back(Global);
    return;
  }

  // Otherwise emit the definition.
  EmitGlobalDefinition(Global);
}

void CodeGenModule::EmitGlobalDefinition(const ValueDecl *D) {
  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    EmitGlobalFunctionDefinition(FD);
  } else if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
    EmitGlobalVarDefinition(VD);
  } else {
    assert(0 && "Invalid argument to EmitGlobalDefinition()");
  }
}

 llvm::Constant *CodeGenModule::GetAddrOfGlobalVar(const VarDecl *D) {
  assert(D->hasGlobalStorage() && "Not a global variable");

  QualType ASTTy = D->getType();
  const llvm::Type *Ty = getTypes().ConvertTypeForMem(ASTTy);
  const llvm::Type *PTy = llvm::PointerType::get(Ty, ASTTy.getAddressSpace());

  // Lookup the entry, lazily creating it if necessary.
  llvm::GlobalValue *&Entry = GlobalDeclMap[D->getIdentifier()];
  if (!Entry) {
    llvm::GlobalVariable *GV = 
      new llvm::GlobalVariable(Ty, false, 
                               llvm::GlobalValue::ExternalLinkage,
                               0, D->getNameAsString(), &getModule(), 0,
                               ASTTy.getAddressSpace());
    Entry = GV;

    // Handle things which are present even on external declarations.

    // FIXME: This code is overly simple and should be merged with
    // other global handling.

    GV->setConstant(D->getType().isConstant(Context));

    if (D->getStorageClass() == VarDecl::PrivateExtern)
      setGlobalVisibility(GV, VisibilityAttr::HiddenVisibility);
  }
  
  // Make sure the result is of the correct type.
  return llvm::ConstantExpr::getBitCast(Entry, PTy);
}

void CodeGenModule::EmitGlobalVarDefinition(const VarDecl *D) {
  llvm::Constant *Init = 0;
  QualType ASTTy = D->getType();
  const llvm::Type *VarTy = getTypes().ConvertTypeForMem(ASTTy);

  if (D->getInit() == 0) {
    // This is a tentative definition; tentative definitions are
    // implicitly initialized with { 0 }
    const llvm::Type* InitTy;
    if (ASTTy->isIncompleteArrayType()) {
      // An incomplete array is normally [ TYPE x 0 ], but we need
      // to fix it to [ TYPE x 1 ].
      const llvm::ArrayType* ATy = cast<llvm::ArrayType>(VarTy);
      InitTy = llvm::ArrayType::get(ATy->getElementType(), 1);
    } else {
      InitTy = VarTy;
    }
    Init = llvm::Constant::getNullValue(InitTy);
  } else {
    Init = EmitConstantExpr(D->getInit());
  }
  const llvm::Type* InitType = Init->getType();

  llvm::GlobalValue *&Entry = GlobalDeclMap[D->getIdentifier()];
  llvm::GlobalVariable *GV = cast_or_null<llvm::GlobalVariable>(Entry);
  
  if (!GV) {
    GV = new llvm::GlobalVariable(InitType, false, 
                                  llvm::GlobalValue::ExternalLinkage,
                                  0, D->getNameAsString(), &getModule(), 0,
                                  ASTTy.getAddressSpace());
  } else if (GV->getType() != 
             llvm::PointerType::get(InitType, ASTTy.getAddressSpace())) {
    // We have a definition after a prototype with the wrong type.
    // We must make a new GlobalVariable* and update everything that used OldGV
    // (a declaration or tentative definition) with the new GlobalVariable*
    // (which will be a definition).
    //
    // This happens if there is a prototype for a global (e.g. "extern int x[];")
    // and then a definition of a different type (e.g. "int x[10];"). This also
    // happens when an initializer has a different type from the type of the
    // global (this happens with unions).
    //
    // FIXME: This also ends up happening if there's a definition followed by
    // a tentative definition!  (Although Sema rejects that construct
    // at the moment.)

    // Save the old global
    llvm::GlobalVariable *OldGV = GV;

    // Make a new global with the correct type
    GV = new llvm::GlobalVariable(InitType, false, 
                                  llvm::GlobalValue::ExternalLinkage,
                                  0, D->getNameAsString(), &getModule(), 0,
                                  ASTTy.getAddressSpace());
    // Steal the name of the old global
    GV->takeName(OldGV);

    // Replace all uses of the old global with the new global
    llvm::Constant *NewPtrForOldDecl = 
        llvm::ConstantExpr::getBitCast(GV, OldGV->getType());
    OldGV->replaceAllUsesWith(NewPtrForOldDecl);

    // Erase the old global, since it is no longer used.
    OldGV->eraseFromParent();
  }

  Entry = GV;

  if (const AnnotateAttr *AA = D->getAttr<AnnotateAttr>()) {
    SourceManager &SM = Context.getSourceManager();
    AddAnnotation(EmitAnnotateAttr(GV, AA,
                              SM.getInstantiationLineNumber(D->getLocation())));
  }

  GV->setInitializer(Init);
  GV->setConstant(D->getType().isConstant(Context));

  // FIXME: This is silly; getTypeAlign should just work for incomplete arrays
  unsigned Align;
  if (const IncompleteArrayType* IAT =
        Context.getAsIncompleteArrayType(D->getType()))
    Align = Context.getTypeAlign(IAT->getElementType());
  else
    Align = Context.getTypeAlign(D->getType());
  if (const AlignedAttr* AA = D->getAttr<AlignedAttr>()) {
    Align = std::max(Align, AA->getAlignment());
  }
  GV->setAlignment(Align / 8);

  if (const VisibilityAttr *attr = D->getAttr<VisibilityAttr>())
    setGlobalVisibility(GV, attr->getVisibility());
  // FIXME: else handle -fvisibility

  if (const AsmLabelAttr *ALA = D->getAttr<AsmLabelAttr>()) {
    // Prefaced with special LLVM marker to indicate that the name
    // should not be munged.
    GV->setName("\01" + ALA->getLabel());
  }
  
  // Set the llvm linkage type as appropriate.
  if (D->getStorageClass() == VarDecl::Static)
    GV->setLinkage(llvm::Function::InternalLinkage);
  else if (D->getAttr<DLLImportAttr>())
    GV->setLinkage(llvm::Function::DLLImportLinkage);
  else if (D->getAttr<DLLExportAttr>())
    GV->setLinkage(llvm::Function::DLLExportLinkage);
  else if (D->getAttr<WeakAttr>())
    GV->setLinkage(llvm::GlobalVariable::WeakLinkage);
  else {
    // FIXME: This isn't right.  This should handle common linkage and other
    // stuff.
    switch (D->getStorageClass()) {
    case VarDecl::Static: assert(0 && "This case handled above");
    case VarDecl::Auto:
    case VarDecl::Register:
      assert(0 && "Can't have auto or register globals");
    case VarDecl::None:
      if (!D->getInit())
        GV->setLinkage(llvm::GlobalVariable::CommonLinkage);
      else
        GV->setLinkage(llvm::GlobalVariable::ExternalLinkage);
      break;
    case VarDecl::Extern:
      // FIXME: common
      break;
      
    case VarDecl::PrivateExtern:
      GV->setVisibility(llvm::GlobalValue::HiddenVisibility);
      // FIXME: common
      break;
    }
  }

  // Emit global variable debug information.
  CGDebugInfo *DI = getDebugInfo();
  if(DI) {
    DI->setLocation(D->getLocation());
    DI->EmitGlobalVariable(GV, D);
  }
}

llvm::GlobalValue *
CodeGenModule::EmitForwardFunctionDefinition(const FunctionDecl *D) {
  const llvm::Type *Ty = getTypes().ConvertType(D->getType());
  llvm::Function *F = llvm::Function::Create(cast<llvm::FunctionType>(Ty), 
                                             llvm::Function::ExternalLinkage,
                                             D->getNameAsString(),&getModule());
  SetFunctionAttributes(D, F);
  return F;
}

llvm::Constant *CodeGenModule::GetAddrOfFunction(const FunctionDecl *D) {
  QualType ASTTy = D->getType();
  const llvm::Type *Ty = getTypes().ConvertTypeForMem(ASTTy);
  const llvm::Type *PTy = llvm::PointerType::get(Ty, ASTTy.getAddressSpace());
  
  // Lookup the entry, lazily creating it if necessary.
  llvm::GlobalValue *&Entry = GlobalDeclMap[D->getIdentifier()];
  if (!Entry)
    Entry = EmitForwardFunctionDefinition(D);

  return llvm::ConstantExpr::getBitCast(Entry, PTy);
}

void CodeGenModule::EmitGlobalFunctionDefinition(const FunctionDecl *D) {
  llvm::GlobalValue *&Entry = GlobalDeclMap[D->getIdentifier()];
  if (!Entry) {
    Entry = EmitForwardFunctionDefinition(D);
  } else {
    // If the types mismatch then we have to rewrite the definition.
    const llvm::Type *Ty = getTypes().ConvertType(D->getType());
    if (Entry->getType() != llvm::PointerType::getUnqual(Ty)) {
      // Otherwise, we have a definition after a prototype with the wrong type.
      // F is the Function* for the one with the wrong type, we must make a new
      // Function* and update everything that used F (a declaration) with the new
      // Function* (which will be a definition).
      //
      // This happens if there is a prototype for a function (e.g. "int f()") and
      // then a definition of a different type (e.g. "int f(int x)").  Start by
      // making a new function of the correct type, RAUW, then steal the name.
      llvm::GlobalValue *NewFn = EmitForwardFunctionDefinition(D);
      NewFn->takeName(Entry);
      
      // Replace uses of F with the Function we will endow with a body.
      llvm::Constant *NewPtrForOldDecl = 
        llvm::ConstantExpr::getBitCast(NewFn, Entry->getType());
      Entry->replaceAllUsesWith(NewPtrForOldDecl);
            
      // Ok, delete the old function now, which is dead.
      assert(Entry->isDeclaration() && "Shouldn't replace non-declaration");
      Entry->eraseFromParent();
      
      Entry = NewFn;
    }
  }

  llvm::Function *Fn = cast<llvm::Function>(Entry);    
  CodeGenFunction(*this).GenerateCode(D, Fn);

  SetFunctionAttributesForDefinition(D, Fn);
  
  if (const ConstructorAttr *CA = D->getAttr<ConstructorAttr>()) {
    AddGlobalCtor(Fn, CA->getPriority());
  } else if (const DestructorAttr *DA = D->getAttr<DestructorAttr>()) {
    AddGlobalDtor(Fn, DA->getPriority());
  }
}

llvm::Function *
CodeGenModule::CreateRuntimeFunction(const llvm::FunctionType *FTy,
                                     const std::string &Name) {
  llvm::Function *Fn = llvm::Function::Create(FTy, 
                                              llvm::Function::ExternalLinkage,
                                              "", &TheModule);
  RuntimeFunctions.push_back(std::make_pair(Fn, Name));
  return Fn;
}

void CodeGenModule::UpdateCompletedType(const TagDecl *TD) {
  // Make sure that this type is translated.
  Types.UpdateCompletedType(TD);
}


/// getBuiltinLibFunction
llvm::Function *CodeGenModule::getBuiltinLibFunction(unsigned BuiltinID) {
  if (BuiltinID > BuiltinFunctions.size())
    BuiltinFunctions.resize(BuiltinID);
  
  // Cache looked up functions.  Since builtin id #0 is invalid we don't reserve
  // a slot for it.
  assert(BuiltinID && "Invalid Builtin ID");
  llvm::Function *&FunctionSlot = BuiltinFunctions[BuiltinID-1];
  if (FunctionSlot)
    return FunctionSlot;
  
  assert(Context.BuiltinInfo.isLibFunction(BuiltinID) && "isn't a lib fn");
  
  // Get the name, skip over the __builtin_ prefix.
  const char *Name = Context.BuiltinInfo.GetName(BuiltinID)+10;
  
  // Get the type for the builtin.
  QualType Type = Context.BuiltinInfo.GetBuiltinType(BuiltinID, Context);
  const llvm::FunctionType *Ty = 
    cast<llvm::FunctionType>(getTypes().ConvertType(Type));

  // FIXME: This has a serious problem with code like this:
  //  void abs() {}
  //    ... __builtin_abs(x);
  // The two versions of abs will collide.  The fix is for the builtin to win,
  // and for the existing one to be turned into a constantexpr cast of the
  // builtin.  In the case where the existing one is a static function, it
  // should just be renamed.
  if (llvm::Function *Existing = getModule().getFunction(Name)) {
    if (Existing->getFunctionType() == Ty && Existing->hasExternalLinkage())
      return FunctionSlot = Existing;
    assert(Existing == 0 && "FIXME: Name collision");
  }

  // FIXME: param attributes for sext/zext etc.
  return FunctionSlot = 
    llvm::Function::Create(Ty, llvm::Function::ExternalLinkage, Name,
                           &getModule());
}

llvm::Function *CodeGenModule::getIntrinsic(unsigned IID,const llvm::Type **Tys,
                                            unsigned NumTys) {
  return llvm::Intrinsic::getDeclaration(&getModule(),
                                         (llvm::Intrinsic::ID)IID, Tys, NumTys);
}

llvm::Function *CodeGenModule::getMemCpyFn() {
  if (MemCpyFn) return MemCpyFn;
  const llvm::Type *IntPtr = TheTargetData.getIntPtrType();
  return MemCpyFn = getIntrinsic(llvm::Intrinsic::memcpy, &IntPtr, 1);
}

llvm::Function *CodeGenModule::getMemMoveFn() {
  if (MemMoveFn) return MemMoveFn;
  const llvm::Type *IntPtr = TheTargetData.getIntPtrType();
  return MemMoveFn = getIntrinsic(llvm::Intrinsic::memmove, &IntPtr, 1);
}

llvm::Function *CodeGenModule::getMemSetFn() {
  if (MemSetFn) return MemSetFn;
  const llvm::Type *IntPtr = TheTargetData.getIntPtrType();
  return MemSetFn = getIntrinsic(llvm::Intrinsic::memset, &IntPtr, 1);
}

static void appendFieldAndPadding(CodeGenModule &CGM,
                                  std::vector<llvm::Constant*>& Fields,
                                  FieldDecl *FieldD, FieldDecl *NextFieldD,
                                  llvm::Constant* Field,
                                  RecordDecl* RD, const llvm::StructType *STy)
{
  // Append the field.
  Fields.push_back(Field);
  
  int StructFieldNo = CGM.getTypes().getLLVMFieldNo(FieldD);
  
  int NextStructFieldNo;
  if (!NextFieldD) {
    NextStructFieldNo = STy->getNumElements();
  } else {
    NextStructFieldNo = CGM.getTypes().getLLVMFieldNo(NextFieldD);
  }
  
  // Append padding
  for (int i = StructFieldNo + 1; i < NextStructFieldNo; i++) {
    llvm::Constant *C = 
      llvm::Constant::getNullValue(STy->getElementType(StructFieldNo + 1));
    
    Fields.push_back(C);
  }
}

// We still need to work out the details of handling UTF-16. 
// See: <rdr://2996215>
llvm::Constant *CodeGenModule::
GetAddrOfConstantCFString(const std::string &str) {
  llvm::StringMapEntry<llvm::Constant *> &Entry = 
    CFConstantStringMap.GetOrCreateValue(&str[0], &str[str.length()]);
  
  if (Entry.getValue())
    return Entry.getValue();
  
  llvm::Constant *Zero = llvm::Constant::getNullValue(llvm::Type::Int32Ty);
  llvm::Constant *Zeros[] = { Zero, Zero };
  
  if (!CFConstantStringClassRef) {
    const llvm::Type *Ty = getTypes().ConvertType(getContext().IntTy);
    Ty = llvm::ArrayType::get(Ty, 0);

    // FIXME: This is fairly broken if
    // __CFConstantStringClassReference is already defined, in that it
    // will get renamed and the user will most likely see an opaque
    // error message. This is a general issue with relying on
    // particular names.
    llvm::GlobalVariable *GV = 
      new llvm::GlobalVariable(Ty, false,
                               llvm::GlobalVariable::ExternalLinkage, 0, 
                               "__CFConstantStringClassReference", 
                               &getModule());
    
    // Decay array -> ptr
    CFConstantStringClassRef =
      llvm::ConstantExpr::getGetElementPtr(GV, Zeros, 2);
  }
  
  QualType CFTy = getContext().getCFConstantStringType();
  RecordDecl *CFRD = CFTy->getAsRecordType()->getDecl();

  const llvm::StructType *STy = 
    cast<llvm::StructType>(getTypes().ConvertType(CFTy));

  std::vector<llvm::Constant*> Fields;
  RecordDecl::field_iterator Field = CFRD->field_begin();

  // Class pointer.
  FieldDecl *CurField = *Field++;
  FieldDecl *NextField = *Field++;
  appendFieldAndPadding(*this, Fields, CurField, NextField,
                        CFConstantStringClassRef, CFRD, STy);
  
  // Flags.
  CurField = NextField;
  NextField = *Field++;
  const llvm::Type *Ty = getTypes().ConvertType(getContext().UnsignedIntTy);
  appendFieldAndPadding(*this, Fields, CurField, NextField,
                        llvm::ConstantInt::get(Ty, 0x07C8), CFRD, STy);
    
  // String pointer.
  CurField = NextField;
  NextField = *Field++;
  llvm::Constant *C = llvm::ConstantArray::get(str);
  C = new llvm::GlobalVariable(C->getType(), true, 
                               llvm::GlobalValue::InternalLinkage,
                               C, ".str", &getModule());
  appendFieldAndPadding(*this, Fields, CurField, NextField,
                        llvm::ConstantExpr::getGetElementPtr(C, Zeros, 2),
                        CFRD, STy);
  
  // String length.
  CurField = NextField;
  NextField = 0;
  Ty = getTypes().ConvertType(getContext().LongTy);
  appendFieldAndPadding(*this, Fields, CurField, NextField,
                        llvm::ConstantInt::get(Ty, str.length()), CFRD, STy);
  
  // The struct.
  C = llvm::ConstantStruct::get(STy, Fields);
  llvm::GlobalVariable *GV = 
    new llvm::GlobalVariable(C->getType(), true, 
                             llvm::GlobalVariable::InternalLinkage, 
                             C, "", &getModule());
  
  GV->setSection("__DATA,__cfstring");
  Entry.setValue(GV);
  
  return GV;
}

/// GetStringForStringLiteral - Return the appropriate bytes for a
/// string literal, properly padded to match the literal type.
std::string CodeGenModule::GetStringForStringLiteral(const StringLiteral *E) {
  if (E->isWide()) {
    ErrorUnsupported(E, "wide string");
    return "FIXME";
  }

  const char *StrData = E->getStrData();
  unsigned Len = E->getByteLength();

  const ConstantArrayType *CAT =
    getContext().getAsConstantArrayType(E->getType());
  assert(CAT && "String isn't pointer or array!");
  
  // Resize the string to the right size
  // FIXME: What about wchar_t strings?
  std::string Str(StrData, StrData+Len);
  uint64_t RealLen = CAT->getSize().getZExtValue();
  Str.resize(RealLen, '\0');
  
  return Str;
}

/// GetAddrOfConstantStringFromLiteral - Return a pointer to a
/// constant array for the given string literal.
llvm::Constant *
CodeGenModule::GetAddrOfConstantStringFromLiteral(const StringLiteral *S) {
  // FIXME: This can be more efficient.
  return GetAddrOfConstantString(GetStringForStringLiteral(S));
}

/// GenerateWritableString -- Creates storage for a string literal.
static llvm::Constant *GenerateStringLiteral(const std::string &str, 
                                             bool constant,
                                             CodeGenModule &CGM,
                                             const char *GlobalName) {
  // Create Constant for this string literal. Don't add a '\0'.
  llvm::Constant *C = llvm::ConstantArray::get(str, false);
  
  // Create a global variable for this string
  C = new llvm::GlobalVariable(C->getType(), constant, 
                               llvm::GlobalValue::InternalLinkage,
                               C, 
                               GlobalName ? GlobalName : ".str", 
                               &CGM.getModule());

  return C;
}

/// GetAddrOfConstantString - Returns a pointer to a character array
/// containing the literal. This contents are exactly that of the
/// given string, i.e. it will not be null terminated automatically;
/// see GetAddrOfConstantCString. Note that whether the result is
/// actually a pointer to an LLVM constant depends on
/// Feature.WriteableStrings.
///
/// The result has pointer to array type.
llvm::Constant *CodeGenModule::GetAddrOfConstantString(const std::string &str,
                                                       const char *GlobalName) {
  // Don't share any string literals if writable-strings is turned on.
  if (Features.WritableStrings)
    return GenerateStringLiteral(str, false, *this, GlobalName);
  
  llvm::StringMapEntry<llvm::Constant *> &Entry = 
  ConstantStringMap.GetOrCreateValue(&str[0], &str[str.length()]);

  if (Entry.getValue())
      return Entry.getValue();

  // Create a global variable for this.
  llvm::Constant *C = GenerateStringLiteral(str, true, *this, GlobalName);
  Entry.setValue(C);
  return C;
}

/// GetAddrOfConstantCString - Returns a pointer to a character
/// array containing the literal and a terminating '\-'
/// character. The result has pointer to array type.
llvm::Constant *CodeGenModule::GetAddrOfConstantCString(const std::string &str,
                                                        const char *GlobalName){
  return GetAddrOfConstantString(str + '\0', GlobalName);
}

/// EmitObjCPropertyImplementations - Emit information for synthesized
/// properties for an implementation.
void CodeGenModule::EmitObjCPropertyImplementations(const 
                                                    ObjCImplementationDecl *D) {
  for (ObjCImplementationDecl::propimpl_iterator i = D->propimpl_begin(),
         e = D->propimpl_end(); i != e; ++i) {
    ObjCPropertyImplDecl *PID = *i;
    
    // Dynamic is just for type-checking.
    if (PID->getPropertyImplementation() == ObjCPropertyImplDecl::Synthesize) {
      ObjCPropertyDecl *PD = PID->getPropertyDecl();

      // Determine which methods need to be implemented, some may have
      // been overridden. Note that ::isSynthesized is not the method
      // we want, that just indicates if the decl came from a
      // property. What we want to know is if the method is defined in
      // this implementation.
      if (!D->getInstanceMethod(PD->getGetterName()))
        CodeGenFunction(*this).GenerateObjCGetter(
                                 const_cast<ObjCImplementationDecl *>(D), PID);
      if (!PD->isReadOnly() &&
          !D->getInstanceMethod(PD->getSetterName()))
        CodeGenFunction(*this).GenerateObjCSetter(
                                 const_cast<ObjCImplementationDecl *>(D), PID);
    }
  }
}

/// EmitTopLevelDecl - Emit code for a single top level declaration.
void CodeGenModule::EmitTopLevelDecl(Decl *D) {
  // If an error has occurred, stop code generation, but continue
  // parsing and semantic analysis (to ensure all warnings and errors
  // are emitted).
  if (Diags.hasErrorOccurred())
    return;

  switch (D->getKind()) {
  case Decl::Function:
  case Decl::Var:
    EmitGlobal(cast<ValueDecl>(D));
    break;

  case Decl::Namespace:
    ErrorUnsupported(D, "namespace");
    break;

    // Objective-C Decls
    
    // Forward declarations, no (immediate) code generation.
  case Decl::ObjCClass:
  case Decl::ObjCCategory:
  case Decl::ObjCForwardProtocol:
  case Decl::ObjCInterface:
    break;

  case Decl::ObjCProtocol:
    Runtime->GenerateProtocol(cast<ObjCProtocolDecl>(D));
    break;

  case Decl::ObjCCategoryImpl:
    // Categories have properties but don't support synthesize so we
    // can ignore them here.

    Runtime->GenerateCategory(cast<ObjCCategoryImplDecl>(D));
    break;

  case Decl::ObjCImplementation: {
    ObjCImplementationDecl *OMD = cast<ObjCImplementationDecl>(D);
    EmitObjCPropertyImplementations(OMD);
    Runtime->GenerateClass(OMD);
    break;
  } 
  case Decl::ObjCMethod: {
    ObjCMethodDecl *OMD = cast<ObjCMethodDecl>(D);
    // If this is not a prototype, emit the body.
    if (OMD->getBody())
      CodeGenFunction(*this).GenerateObjCMethod(OMD);
    break;
  }
  case Decl::ObjCCompatibleAlias: 
    // compatibility-alias is a directive and has no code gen.
    break;

  case Decl::LinkageSpec: {
    LinkageSpecDecl *LSD = cast<LinkageSpecDecl>(D);
    if (LSD->getLanguage() == LinkageSpecDecl::lang_cxx)
      ErrorUnsupported(LSD, "linkage spec");
    // FIXME: implement C++ linkage, C linkage works mostly by C
    // language reuse already.
    break;
  }

  case Decl::FileScopeAsm: {
    FileScopeAsmDecl *AD = cast<FileScopeAsmDecl>(D);
    std::string AsmString(AD->getAsmString()->getStrData(),
                          AD->getAsmString()->getByteLength());
    
    const std::string &S = getModule().getModuleInlineAsm();
    if (S.empty())
      getModule().setModuleInlineAsm(AsmString);
    else
      getModule().setModuleInlineAsm(S + '\n' + AsmString);
    break;
  }
   
  default: 
    // Make sure we handled everything we should, every other kind is
    // a non-top-level decl.  FIXME: Would be nice to have an
    // isTopLevelDeclKind function. Need to recode Decl::Kind to do
    // that easily.
    assert(isa<TypeDecl>(D) && "Unsupported decl kind");
  }
}
  
