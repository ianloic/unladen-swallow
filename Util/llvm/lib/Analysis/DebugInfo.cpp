//===--- DebugInfo.cpp - Debug Information Helper Classes -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the helper classes used to build and interpret debug
// information in LLVM IR form.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/DebugInfo.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Intrinsics.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Instructions.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/Dwarf.h"
#include "llvm/Support/DebugLoc.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;
using namespace llvm::dwarf;

//===----------------------------------------------------------------------===//
// DIDescriptor
//===----------------------------------------------------------------------===//

/// ValidDebugInfo - Return true if V represents valid debug info value.
/// FIXME : Add DIDescriptor.isValid()
bool DIDescriptor::ValidDebugInfo(MDNode *N, CodeGenOpt::Level OptLevel) {
  if (!N)
    return false;

  DIDescriptor DI(N);

  // Check current version. Allow Version6 for now.
  unsigned Version = DI.getVersion();
  if (Version != LLVMDebugVersion && Version != LLVMDebugVersion6)
    return false;

  unsigned Tag = DI.getTag();
  switch (Tag) {
  case DW_TAG_variable:
    assert(DIVariable(N).Verify() && "Invalid DebugInfo value");
    break;
  case DW_TAG_compile_unit:
    assert(DICompileUnit(N).Verify() && "Invalid DebugInfo value");
    break;
  case DW_TAG_subprogram:
    assert(DISubprogram(N).Verify() && "Invalid DebugInfo value");
    break;
  case DW_TAG_lexical_block:
    // FIXME: This interfers with the quality of generated code during
    // optimization.
    if (OptLevel != CodeGenOpt::None)
      return false;
    // FALLTHROUGH
  default:
    break;
  }

  return true;
}

DIDescriptor::DIDescriptor(MDNode *N, unsigned RequiredTag) {
  DbgNode = N;
  
  // If this is non-null, check to see if the Tag matches. If not, set to null.
  if (N && getTag() != RequiredTag) {
    DbgNode = 0;
  }
}

const std::string &
DIDescriptor::getStringField(unsigned Elt, std::string &Result) const {
  Result.clear();
  if (DbgNode == 0)
    return Result;

  if (Elt < DbgNode->getNumElements()) 
    if (MDString *MDS = dyn_cast_or_null<MDString>(DbgNode->getElement(Elt))) {
      Result.assign(MDS->begin(), MDS->begin() + MDS->length());
      return Result;
    }
  
  return Result;
}

uint64_t DIDescriptor::getUInt64Field(unsigned Elt) const {
  if (DbgNode == 0) 
    return 0;

  if (Elt < DbgNode->getNumElements())
    if (ConstantInt *CI = dyn_cast<ConstantInt>(DbgNode->getElement(Elt)))
      return CI->getZExtValue();
  
  return 0;
}

DIDescriptor DIDescriptor::getDescriptorField(unsigned Elt) const {
  if (DbgNode == 0) 
    return DIDescriptor();

  if (Elt < DbgNode->getNumElements() && DbgNode->getElement(Elt))
    return DIDescriptor(dyn_cast<MDNode>(DbgNode->getElement(Elt)));

  return DIDescriptor();
}

GlobalVariable *DIDescriptor::getGlobalVariableField(unsigned Elt) const {
  if (DbgNode == 0) 
    return 0;

  if (Elt < DbgNode->getNumElements())
      return dyn_cast<GlobalVariable>(DbgNode->getElement(Elt));
  return 0;
}

//===----------------------------------------------------------------------===//
// Simple Descriptor Constructors and other Methods
//===----------------------------------------------------------------------===//

// Needed by DIVariable::getType().
DIType::DIType(MDNode *N) : DIDescriptor(N) {
  if (!N) return;
  unsigned tag = getTag();
  if (tag != dwarf::DW_TAG_base_type && !DIDerivedType::isDerivedType(tag) &&
      !DICompositeType::isCompositeType(tag)) {
    DbgNode = 0;
  }
}

/// isDerivedType - Return true if the specified tag is legal for
/// DIDerivedType.
bool DIType::isDerivedType(unsigned Tag) {
  switch (Tag) {
  case dwarf::DW_TAG_typedef:
  case dwarf::DW_TAG_pointer_type:
  case dwarf::DW_TAG_reference_type:
  case dwarf::DW_TAG_const_type:
  case dwarf::DW_TAG_volatile_type:
  case dwarf::DW_TAG_restrict_type:
  case dwarf::DW_TAG_member:
  case dwarf::DW_TAG_inheritance:
    return true;
  default:
    // CompositeTypes are currently modelled as DerivedTypes.
    return isCompositeType(Tag);
  }
}

/// isCompositeType - Return true if the specified tag is legal for
/// DICompositeType.
bool DIType::isCompositeType(unsigned TAG) {
  switch (TAG) {
  case dwarf::DW_TAG_array_type:
  case dwarf::DW_TAG_structure_type:
  case dwarf::DW_TAG_union_type:
  case dwarf::DW_TAG_enumeration_type:
  case dwarf::DW_TAG_vector_type:
  case dwarf::DW_TAG_subroutine_type:
  case dwarf::DW_TAG_class_type:
    return true;
  default:
    return false;
  }
}

/// isVariable - Return true if the specified tag is legal for DIVariable.
bool DIVariable::isVariable(unsigned Tag) {
  switch (Tag) {
  case dwarf::DW_TAG_auto_variable:
  case dwarf::DW_TAG_arg_variable:
  case dwarf::DW_TAG_return_variable:
    return true;
  default:
    return false;
  }
}

unsigned DIArray::getNumElements() const {
  assert (DbgNode && "Invalid DIArray");
  return DbgNode->getNumElements();
}

/// replaceAllUsesWith - Replace all uses of debug info referenced by
/// this descriptor. After this completes, the current debug info value
/// is erased.
void DIDerivedType::replaceAllUsesWith(DIDescriptor &D) {
  if (isNull())
    return;

  assert (!D.isNull() && "Can not replace with null");
  DbgNode->replaceAllUsesWith(D.getNode());
  delete DbgNode;
}

/// Verify - Verify that a compile unit is well formed.
bool DICompileUnit::Verify() const {
  if (isNull()) 
    return false;
  std::string Res;
  if (getFilename(Res).empty()) 
    return false;
  // It is possible that directory and produce string is empty.
  return true;
}

/// Verify - Verify that a type descriptor is well formed.
bool DIType::Verify() const {
  if (isNull()) 
    return false;
  if (getContext().isNull()) 
    return false;

  DICompileUnit CU = getCompileUnit();
  if (!CU.isNull() && !CU.Verify()) 
    return false;
  return true;
}

/// Verify - Verify that a composite type descriptor is well formed.
bool DICompositeType::Verify() const {
  if (isNull()) 
    return false;
  if (getContext().isNull()) 
    return false;

  DICompileUnit CU = getCompileUnit();
  if (!CU.isNull() && !CU.Verify()) 
    return false;
  return true;
}

/// Verify - Verify that a subprogram descriptor is well formed.
bool DISubprogram::Verify() const {
  if (isNull())
    return false;
  
  if (getContext().isNull())
    return false;

  DICompileUnit CU = getCompileUnit();
  if (!CU.Verify()) 
    return false;

  DICompositeType Ty = getType();
  if (!Ty.isNull() && !Ty.Verify())
    return false;
  return true;
}

/// Verify - Verify that a global variable descriptor is well formed.
bool DIGlobalVariable::Verify() const {
  if (isNull())
    return false;
  
  if (getContext().isNull())
    return false;

  DICompileUnit CU = getCompileUnit();
  if (!CU.isNull() && !CU.Verify()) 
    return false;

  DIType Ty = getType();
  if (!Ty.Verify())
    return false;

  if (!getGlobal())
    return false;

  return true;
}

/// Verify - Verify that a variable descriptor is well formed.
bool DIVariable::Verify() const {
  if (isNull())
    return false;
  
  if (getContext().isNull())
    return false;

  DIType Ty = getType();
  if (!Ty.Verify())
    return false;

  return true;
}

/// getOriginalTypeSize - If this type is derived from a base type then
/// return base type size.
uint64_t DIDerivedType::getOriginalTypeSize() const {
  if (getTag() != dwarf::DW_TAG_member)
    return getSizeInBits();
  DIType BT = getTypeDerivedFrom();
  if (BT.getTag() != dwarf::DW_TAG_base_type)
    return getSizeInBits();
  return BT.getSizeInBits();
}

/// describes - Return true if this subprogram provides debugging
/// information for the function F.
bool DISubprogram::describes(const Function *F) {
  assert (F && "Invalid function");
  std::string Name;
  getLinkageName(Name);
  if (Name.empty())
    getName(Name);
  if (F->getName() == Name)
    return true;
  return false;
}

//===----------------------------------------------------------------------===//
// DIDescriptor: dump routines for all descriptors.
//===----------------------------------------------------------------------===//


/// dump - Print descriptor.
void DIDescriptor::dump() const {
  errs() << "[" << dwarf::TagString(getTag()) << "] ";
  errs().write_hex((intptr_t)DbgNode) << ']';
}

/// dump - Print compile unit.
void DICompileUnit::dump() const {
  if (getLanguage())
    errs() << " [" << dwarf::LanguageString(getLanguage()) << "] ";

  std::string Res1, Res2;
  errs() << " [" << getDirectory(Res1) << "/" << getFilename(Res2) << " ]";
}

/// dump - Print type.
void DIType::dump() const {
  if (isNull()) return;

  std::string Res;
  if (!getName(Res).empty())
    errs() << " [" << Res << "] ";

  unsigned Tag = getTag();
  errs() << " [" << dwarf::TagString(Tag) << "] ";

  // TODO : Print context
  getCompileUnit().dump();
  errs() << " [" 
         << getLineNumber() << ", " 
         << getSizeInBits() << ", "
         << getAlignInBits() << ", "
         << getOffsetInBits() 
         << "] ";

  if (isPrivate()) 
    errs() << " [private] ";
  else if (isProtected())
    errs() << " [protected] ";

  if (isForwardDecl())
    errs() << " [fwd] ";

  if (isBasicType(Tag))
    DIBasicType(DbgNode).dump();
  else if (isDerivedType(Tag))
    DIDerivedType(DbgNode).dump();
  else if (isCompositeType(Tag))
    DICompositeType(DbgNode).dump();
  else {
    errs() << "Invalid DIType\n";
    return;
  }

  errs() << "\n";
}

/// dump - Print basic type.
void DIBasicType::dump() const {
  errs() << " [" << dwarf::AttributeEncodingString(getEncoding()) << "] ";
}

/// dump - Print derived type.
void DIDerivedType::dump() const {
  errs() << "\n\t Derived From: "; getTypeDerivedFrom().dump();
}

/// dump - Print composite type.
void DICompositeType::dump() const {
  DIArray A = getTypeArray();
  if (A.isNull())
    return;
  errs() << " [" << A.getNumElements() << " elements]";
}

/// dump - Print global.
void DIGlobal::dump() const {
  std::string Res;
  if (!getName(Res).empty())
    errs() << " [" << Res << "] ";

  unsigned Tag = getTag();
  errs() << " [" << dwarf::TagString(Tag) << "] ";

  // TODO : Print context
  getCompileUnit().dump();
  errs() << " [" << getLineNumber() << "] ";

  if (isLocalToUnit())
    errs() << " [local] ";

  if (isDefinition())
    errs() << " [def] ";

  if (isGlobalVariable(Tag))
    DIGlobalVariable(DbgNode).dump();

  errs() << "\n";
}

/// dump - Print subprogram.
void DISubprogram::dump() const {
  DIGlobal::dump();
}

/// dump - Print global variable.
void DIGlobalVariable::dump() const {
  errs() << " [";
  getGlobal()->dump();
  errs() << "] ";
}

/// dump - Print variable.
void DIVariable::dump() const {
  std::string Res;
  if (!getName(Res).empty())
    errs() << " [" << Res << "] ";

  getCompileUnit().dump();
  errs() << " [" << getLineNumber() << "] ";
  getType().dump();
  errs() << "\n";
}

//===----------------------------------------------------------------------===//
// DIFactory: Basic Helpers
//===----------------------------------------------------------------------===//

DIFactory::DIFactory(Module &m)
  : M(m), VMContext(M.getContext()), StopPointFn(0), FuncStartFn(0), 
    RegionStartFn(0), RegionEndFn(0),
    DeclareFn(0) {
  EmptyStructPtr = PointerType::getUnqual(StructType::get(VMContext));
}

Constant *DIFactory::GetTagConstant(unsigned TAG) {
  assert((TAG & LLVMDebugVersionMask) == 0 &&
         "Tag too large for debug encoding!");
  return ConstantInt::get(Type::getInt32Ty(VMContext), TAG | LLVMDebugVersion);
}

//===----------------------------------------------------------------------===//
// DIFactory: Primary Constructors
//===----------------------------------------------------------------------===//

/// GetOrCreateArray - Create an descriptor for an array of descriptors. 
/// This implicitly uniques the arrays created.
DIArray DIFactory::GetOrCreateArray(DIDescriptor *Tys, unsigned NumTys) {
  SmallVector<Value*, 16> Elts;
  
  if (NumTys == 0)
    Elts.push_back(llvm::Constant::getNullValue(Type::getInt32Ty(VMContext)));
  else
    for (unsigned i = 0; i != NumTys; ++i)
      Elts.push_back(Tys[i].getNode());

  return DIArray(MDNode::get(VMContext,Elts.data(), Elts.size()));
}

/// GetOrCreateSubrange - Create a descriptor for a value range.  This
/// implicitly uniques the values returned.
DISubrange DIFactory::GetOrCreateSubrange(int64_t Lo, int64_t Hi) {
  Value *Elts[] = {
    GetTagConstant(dwarf::DW_TAG_subrange_type),
    ConstantInt::get(Type::getInt64Ty(VMContext), Lo),
    ConstantInt::get(Type::getInt64Ty(VMContext), Hi)
  };
  
  return DISubrange(MDNode::get(VMContext, &Elts[0], 3));
}



/// CreateCompileUnit - Create a new descriptor for the specified compile
/// unit.  Note that this does not unique compile units within the module.
DICompileUnit DIFactory::CreateCompileUnit(unsigned LangID,
                                           const std::string &Filename,
                                           const std::string &Directory,
                                           const std::string &Producer,
                                           bool isMain,
                                           bool isOptimized,
                                           const char *Flags,
                                           unsigned RunTimeVer) {
  Value *Elts[] = {
    GetTagConstant(dwarf::DW_TAG_compile_unit),
    llvm::Constant::getNullValue(Type::getInt32Ty(VMContext)),
    ConstantInt::get(Type::getInt32Ty(VMContext), LangID),
    MDString::get(VMContext, Filename),
    MDString::get(VMContext, Directory),
    MDString::get(VMContext, Producer),
    ConstantInt::get(Type::getInt1Ty(VMContext), isMain),
    ConstantInt::get(Type::getInt1Ty(VMContext), isOptimized),
    MDString::get(VMContext, Flags),
    ConstantInt::get(Type::getInt32Ty(VMContext), RunTimeVer)
  };

  return DICompileUnit(MDNode::get(VMContext, &Elts[0], 10));
}

/// CreateEnumerator - Create a single enumerator value.
DIEnumerator DIFactory::CreateEnumerator(const std::string &Name, uint64_t Val){
  Value *Elts[] = {
    GetTagConstant(dwarf::DW_TAG_enumerator),
    MDString::get(VMContext, Name),
    ConstantInt::get(Type::getInt64Ty(VMContext), Val)
  };
  return DIEnumerator(MDNode::get(VMContext, &Elts[0], 3));
}


/// CreateBasicType - Create a basic type like int, float, etc.
DIBasicType DIFactory::CreateBasicType(DIDescriptor Context,
                                      const std::string &Name,
                                       DICompileUnit CompileUnit,
                                       unsigned LineNumber,
                                       uint64_t SizeInBits,
                                       uint64_t AlignInBits,
                                       uint64_t OffsetInBits, unsigned Flags,
                                       unsigned Encoding) {
  Value *Elts[] = {
    GetTagConstant(dwarf::DW_TAG_base_type),
    Context.getNode(),
    MDString::get(VMContext, Name),
    CompileUnit.getNode(),
    ConstantInt::get(Type::getInt32Ty(VMContext), LineNumber),
    ConstantInt::get(Type::getInt64Ty(VMContext), SizeInBits),
    ConstantInt::get(Type::getInt64Ty(VMContext), AlignInBits),
    ConstantInt::get(Type::getInt64Ty(VMContext), OffsetInBits),
    ConstantInt::get(Type::getInt32Ty(VMContext), Flags),
    ConstantInt::get(Type::getInt32Ty(VMContext), Encoding)
  };
  return DIBasicType(MDNode::get(VMContext, &Elts[0], 10));
}

/// CreateDerivedType - Create a derived type like const qualified type,
/// pointer, typedef, etc.
DIDerivedType DIFactory::CreateDerivedType(unsigned Tag,
                                           DIDescriptor Context,
                                           const std::string &Name,
                                           DICompileUnit CompileUnit,
                                           unsigned LineNumber,
                                           uint64_t SizeInBits,
                                           uint64_t AlignInBits,
                                           uint64_t OffsetInBits,
                                           unsigned Flags,
                                           DIType DerivedFrom) {
  Value *Elts[] = {
    GetTagConstant(Tag),
    Context.getNode(),
    MDString::get(VMContext, Name),
    CompileUnit.getNode(),
    ConstantInt::get(Type::getInt32Ty(VMContext), LineNumber),
    ConstantInt::get(Type::getInt64Ty(VMContext), SizeInBits),
    ConstantInt::get(Type::getInt64Ty(VMContext), AlignInBits),
    ConstantInt::get(Type::getInt64Ty(VMContext), OffsetInBits),
    ConstantInt::get(Type::getInt32Ty(VMContext), Flags),
    DerivedFrom.getNode(),
  };
  return DIDerivedType(MDNode::get(VMContext, &Elts[0], 10));
}

/// CreateCompositeType - Create a composite type like array, struct, etc.
DICompositeType DIFactory::CreateCompositeType(unsigned Tag,
                                               DIDescriptor Context,
                                               const std::string &Name,
                                               DICompileUnit CompileUnit,
                                               unsigned LineNumber,
                                               uint64_t SizeInBits,
                                               uint64_t AlignInBits,
                                               uint64_t OffsetInBits,
                                               unsigned Flags,
                                               DIType DerivedFrom,
                                               DIArray Elements,
                                               unsigned RuntimeLang) {

  Value *Elts[] = {
    GetTagConstant(Tag),
    Context.getNode(),
    MDString::get(VMContext, Name),
    CompileUnit.getNode(),
    ConstantInt::get(Type::getInt32Ty(VMContext), LineNumber),
    ConstantInt::get(Type::getInt64Ty(VMContext), SizeInBits),
    ConstantInt::get(Type::getInt64Ty(VMContext), AlignInBits),
    ConstantInt::get(Type::getInt64Ty(VMContext), OffsetInBits),
    ConstantInt::get(Type::getInt32Ty(VMContext), Flags),
    DerivedFrom.getNode(),
    Elements.getNode(),
    ConstantInt::get(Type::getInt32Ty(VMContext), RuntimeLang)
  };
  return DICompositeType(MDNode::get(VMContext, &Elts[0], 12));
}


/// CreateSubprogram - Create a new descriptor for the specified subprogram.
/// See comments in DISubprogram for descriptions of these fields.  This
/// method does not unique the generated descriptors.
DISubprogram DIFactory::CreateSubprogram(DIDescriptor Context, 
                                         const std::string &Name,
                                         const std::string &DisplayName,
                                         const std::string &LinkageName,
                                         DICompileUnit CompileUnit,
                                         unsigned LineNo, DIType Type,
                                         bool isLocalToUnit,
                                         bool isDefinition) {

  Value *Elts[] = {
    GetTagConstant(dwarf::DW_TAG_subprogram),
    llvm::Constant::getNullValue(Type::getInt32Ty(VMContext)),
    Context.getNode(),
    MDString::get(VMContext, Name),
    MDString::get(VMContext, DisplayName),
    MDString::get(VMContext, LinkageName),
    CompileUnit.getNode(),
    ConstantInt::get(Type::getInt32Ty(VMContext), LineNo),
    Type.getNode(),
    ConstantInt::get(Type::getInt1Ty(VMContext), isLocalToUnit),
    ConstantInt::get(Type::getInt1Ty(VMContext), isDefinition)
  };
  return DISubprogram(MDNode::get(VMContext, &Elts[0], 11));
}

/// CreateGlobalVariable - Create a new descriptor for the specified global.
DIGlobalVariable
DIFactory::CreateGlobalVariable(DIDescriptor Context, const std::string &Name,
                                const std::string &DisplayName,
                                const std::string &LinkageName,
                                DICompileUnit CompileUnit,
                                unsigned LineNo, DIType Type,bool isLocalToUnit,
                                bool isDefinition, llvm::GlobalVariable *Val) {
  Value *Elts[] = { 
    GetTagConstant(dwarf::DW_TAG_variable),
    llvm::Constant::getNullValue(Type::getInt32Ty(VMContext)),
    Context.getNode(),
    MDString::get(VMContext, Name),
    MDString::get(VMContext, DisplayName),
    MDString::get(VMContext, LinkageName),
    CompileUnit.getNode(),
    ConstantInt::get(Type::getInt32Ty(VMContext), LineNo),
    Type.getNode(),
    ConstantInt::get(Type::getInt1Ty(VMContext), isLocalToUnit),
    ConstantInt::get(Type::getInt1Ty(VMContext), isDefinition),
    Val
  };

  Value *const *Vs = &Elts[0];
  MDNode *Node = MDNode::get(VMContext,Vs, 12);

  // Create a named metadata so that we do not lose this mdnode.
  NamedMDNode *NMD = M.getOrInsertNamedMetadata("llvm.dbg.gv");
  NMD->addElement(Node);

  return DIGlobalVariable(Node);
}


/// CreateVariable - Create a new descriptor for the specified variable.
DIVariable DIFactory::CreateVariable(unsigned Tag, DIDescriptor Context,
                                     const std::string &Name,
                                     DICompileUnit CompileUnit, unsigned LineNo,
                                     DIType Type) {
  Value *Elts[] = {
    GetTagConstant(Tag),
    Context.getNode(),
    MDString::get(VMContext, Name),
    CompileUnit.getNode(),
    ConstantInt::get(Type::getInt32Ty(VMContext), LineNo),
    Type.getNode(),
  };
  return DIVariable(MDNode::get(VMContext, &Elts[0], 6));
}


/// CreateBlock - This creates a descriptor for a lexical block with the
/// specified parent VMContext.
DIBlock DIFactory::CreateBlock(DIDescriptor Context) {
  Value *Elts[] = {
    GetTagConstant(dwarf::DW_TAG_lexical_block),
    Context.getNode()
  };
  return DIBlock(MDNode::get(VMContext, &Elts[0], 2));
}


//===----------------------------------------------------------------------===//
// DIFactory: Routines for inserting code into a function
//===----------------------------------------------------------------------===//

/// InsertStopPoint - Create a new llvm.dbg.stoppoint intrinsic invocation,
/// inserting it at the end of the specified basic block.
void DIFactory::InsertStopPoint(DICompileUnit CU, unsigned LineNo,
                                unsigned ColNo, BasicBlock *BB) {
  
  // Lazily construct llvm.dbg.stoppoint function.
  if (!StopPointFn)
    StopPointFn = llvm::Intrinsic::getDeclaration(&M, 
                                              llvm::Intrinsic::dbg_stoppoint);
  
  // Invoke llvm.dbg.stoppoint
  Value *Args[] = {
    ConstantInt::get(llvm::Type::getInt32Ty(VMContext), LineNo),
    ConstantInt::get(llvm::Type::getInt32Ty(VMContext), ColNo),
    CU.getNode()
  };
  CallInst::Create(StopPointFn, Args, Args+3, "", BB);
}

/// InsertSubprogramStart - Create a new llvm.dbg.func.start intrinsic to
/// mark the start of the specified subprogram.
void DIFactory::InsertSubprogramStart(DISubprogram SP, BasicBlock *BB) {
  // Lazily construct llvm.dbg.func.start.
  if (!FuncStartFn)
    FuncStartFn = Intrinsic::getDeclaration(&M, Intrinsic::dbg_func_start);
  
  // Call llvm.dbg.func.start which also implicitly sets a stoppoint.
  CallInst::Create(FuncStartFn, SP.getNode(), "", BB);
}

/// InsertRegionStart - Insert a new llvm.dbg.region.start intrinsic call to
/// mark the start of a region for the specified scoping descriptor.
void DIFactory::InsertRegionStart(DIDescriptor D, BasicBlock *BB) {
  // Lazily construct llvm.dbg.region.start function.
  if (!RegionStartFn)
    RegionStartFn = Intrinsic::getDeclaration(&M, Intrinsic::dbg_region_start);

  // Call llvm.dbg.func.start.
  CallInst::Create(RegionStartFn, D.getNode(), "", BB);
}

/// InsertRegionEnd - Insert a new llvm.dbg.region.end intrinsic call to
/// mark the end of a region for the specified scoping descriptor.
void DIFactory::InsertRegionEnd(DIDescriptor D, BasicBlock *BB) {
  // Lazily construct llvm.dbg.region.end function.
  if (!RegionEndFn)
    RegionEndFn = Intrinsic::getDeclaration(&M, Intrinsic::dbg_region_end);

  // Call llvm.dbg.region.end.
  CallInst::Create(RegionEndFn, D.getNode(), "", BB);
}

/// InsertDeclare - Insert a new llvm.dbg.declare intrinsic call.
void DIFactory::InsertDeclare(Value *Storage, DIVariable D, BasicBlock *BB) {
  // Cast the storage to a {}* for the call to llvm.dbg.declare.
  Storage = new BitCastInst(Storage, EmptyStructPtr, "", BB);
  
  if (!DeclareFn)
    DeclareFn = Intrinsic::getDeclaration(&M, Intrinsic::dbg_declare);

  Value *Args[] = { Storage, D.getNode() };
  CallInst::Create(DeclareFn, Args, Args+2, "", BB);
}


//===----------------------------------------------------------------------===//
// DebugInfoFinder implementations.
//===----------------------------------------------------------------------===//

/// processModule - Process entire module and collect debug info.
void DebugInfoFinder::processModule(Module &M) {


  for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I)
    for (Function::iterator FI = (*I).begin(), FE = (*I).end(); FI != FE; ++FI)
      for (BasicBlock::iterator BI = (*FI).begin(), BE = (*FI).end(); BI != BE;
           ++BI) {
        if (DbgStopPointInst *SPI = dyn_cast<DbgStopPointInst>(BI))
          processStopPoint(SPI);
        else if (DbgFuncStartInst *FSI = dyn_cast<DbgFuncStartInst>(BI))
          processFuncStart(FSI);
        else if (DbgRegionStartInst *DRS = dyn_cast<DbgRegionStartInst>(BI))
          processRegionStart(DRS);
        else if (DbgRegionEndInst *DRE = dyn_cast<DbgRegionEndInst>(BI))
          processRegionEnd(DRE);
        else if (DbgDeclareInst *DDI = dyn_cast<DbgDeclareInst>(BI))
          processDeclare(DDI);
      }

  NamedMDNode *NMD = M.getNamedMetadata("llvm.dbg.gv");
  if (!NMD)
    return;

  for (unsigned i = 0, e = NMD->getNumElements(); i != e; ++i) {
    DIGlobalVariable DIG(cast<MDNode>(NMD->getElement(i)));
    if (addGlobalVariable(DIG)) {
      addCompileUnit(DIG.getCompileUnit());
      processType(DIG.getType());
    }
  }
}
    
/// processType - Process DIType.
void DebugInfoFinder::processType(DIType DT) {
  if (!addType(DT))
    return;

  addCompileUnit(DT.getCompileUnit());
  if (DT.isCompositeType(DT.getTag())) {
    DICompositeType DCT(DT.getNode());
    processType(DCT.getTypeDerivedFrom());
    DIArray DA = DCT.getTypeArray();
    if (!DA.isNull())
      for (unsigned i = 0, e = DA.getNumElements(); i != e; ++i) {
        DIDescriptor D = DA.getElement(i);
        DIType TypeE = DIType(D.getNode());
        if (!TypeE.isNull())
          processType(TypeE);
        else 
          processSubprogram(DISubprogram(D.getNode()));
      }
  } else if (DT.isDerivedType(DT.getTag())) {
    DIDerivedType DDT(DT.getNode());
    if (!DDT.isNull()) 
      processType(DDT.getTypeDerivedFrom());
  }
}

/// processSubprogram - Process DISubprogram.
void DebugInfoFinder::processSubprogram(DISubprogram SP) {
  if (SP.isNull())
    return;
  if (!addSubprogram(SP))
    return;
  addCompileUnit(SP.getCompileUnit());
  processType(SP.getType());
}

/// processStopPoint - Process DbgStopPointInst.
void DebugInfoFinder::processStopPoint(DbgStopPointInst *SPI) {
  MDNode *Context = dyn_cast<MDNode>(SPI->getContext());
  addCompileUnit(DICompileUnit(Context));
}

/// processFuncStart - Process DbgFuncStartInst.
void DebugInfoFinder::processFuncStart(DbgFuncStartInst *FSI) {
  MDNode *SP = dyn_cast<MDNode>(FSI->getSubprogram());
  processSubprogram(DISubprogram(SP));
}

/// processRegionStart - Process DbgRegionStart.
void DebugInfoFinder::processRegionStart(DbgRegionStartInst *DRS) {
  MDNode *SP = dyn_cast<MDNode>(DRS->getContext());
  processSubprogram(DISubprogram(SP));
}

/// processRegionEnd - Process DbgRegionEnd.
void DebugInfoFinder::processRegionEnd(DbgRegionEndInst *DRE) {
  MDNode *SP = dyn_cast<MDNode>(DRE->getContext());
  processSubprogram(DISubprogram(SP));
}

/// processDeclare - Process DbgDeclareInst.
void DebugInfoFinder::processDeclare(DbgDeclareInst *DDI) {
  DIVariable DV(cast<MDNode>(DDI->getVariable()));
  if (DV.isNull())
    return;

  if (!NodesSeen.insert(DV.getNode()))
    return;

  addCompileUnit(DV.getCompileUnit());
  processType(DV.getType());
}

/// addType - Add type into Tys.
bool DebugInfoFinder::addType(DIType DT) {
  if (DT.isNull())
    return false;

  if (!NodesSeen.insert(DT.getNode()))
    return false;

  TYs.push_back(DT.getNode());
  return true;
}

/// addCompileUnit - Add compile unit into CUs.
bool DebugInfoFinder::addCompileUnit(DICompileUnit CU) {
  if (CU.isNull())
    return false;

  if (!NodesSeen.insert(CU.getNode()))
    return false;

  CUs.push_back(CU.getNode());
  return true;
}
    
/// addGlobalVariable - Add global variable into GVs.
bool DebugInfoFinder::addGlobalVariable(DIGlobalVariable DIG) {
  if (DIG.isNull())
    return false;

  if (!NodesSeen.insert(DIG.getNode()))
    return false;

  GVs.push_back(DIG.getNode());
  return true;
}

// addSubprogram - Add subprgoram into SPs.
bool DebugInfoFinder::addSubprogram(DISubprogram SP) {
  if (SP.isNull())
    return false;
  
  if (!NodesSeen.insert(SP.getNode()))
    return false;

  SPs.push_back(SP.getNode());
  return true;
}

namespace llvm {
  /// findStopPoint - Find the stoppoint coressponding to this instruction, that
  /// is the stoppoint that dominates this instruction.
  const DbgStopPointInst *findStopPoint(const Instruction *Inst) {
    if (const DbgStopPointInst *DSI = dyn_cast<DbgStopPointInst>(Inst))
      return DSI;

    const BasicBlock *BB = Inst->getParent();
    BasicBlock::const_iterator I = Inst, B;
    while (BB) {
      B = BB->begin();

      // A BB consisting only of a terminator can't have a stoppoint.
      while (I != B) {
        --I;
        if (const DbgStopPointInst *DSI = dyn_cast<DbgStopPointInst>(I))
          return DSI;
      }

      // This BB didn't have a stoppoint: if there is only one predecessor, look
      // for a stoppoint there. We could use getIDom(), but that would require
      // dominator info.
      BB = I->getParent()->getUniquePredecessor();
      if (BB)
        I = BB->getTerminator();
    }

    return 0;
  }

  /// findBBStopPoint - Find the stoppoint corresponding to first real
  /// (non-debug intrinsic) instruction in this Basic Block, and return the
  /// stoppoint for it.
  const DbgStopPointInst *findBBStopPoint(const BasicBlock *BB) {
    for(BasicBlock::const_iterator I = BB->begin(), E = BB->end(); I != E; ++I)
      if (const DbgStopPointInst *DSI = dyn_cast<DbgStopPointInst>(I))
        return DSI;

    // Fallback to looking for stoppoint of unique predecessor. Useful if this
    // BB contains no stoppoints, but unique predecessor does.
    BB = BB->getUniquePredecessor();
    if (BB)
      return findStopPoint(BB->getTerminator());

    return 0;
  }

  Value *findDbgGlobalDeclare(GlobalVariable *V) {
    const Module *M = V->getParent();
    NamedMDNode *NMD = M->getNamedMetadata("llvm.dbg.gv");
    if (!NMD)
      return 0;
    
    for (unsigned i = 0, e = NMD->getNumElements(); i != e; ++i) {
      DIGlobalVariable DIG(cast_or_null<MDNode>(NMD->getElement(i)));
      if (DIG.isNull())
        continue;
      if (DIG.getGlobal() == V)
        return DIG.getNode();
    }
    return 0;
  }

  /// Finds the llvm.dbg.declare intrinsic corresponding to this value if any.
  /// It looks through pointer casts too.
  const DbgDeclareInst *findDbgDeclare(const Value *V, bool stripCasts) {
    if (stripCasts) {
      V = V->stripPointerCasts();

      // Look for the bitcast.
      for (Value::use_const_iterator I = V->use_begin(), E =V->use_end();
            I != E; ++I)
        if (isa<BitCastInst>(I))
          return findDbgDeclare(*I, false);

      return 0;
    }

    // Find llvm.dbg.declare among uses of the instruction.
    for (Value::use_const_iterator I = V->use_begin(), E =V->use_end();
          I != E; ++I)
      if (const DbgDeclareInst *DDI = dyn_cast<DbgDeclareInst>(I))
        return DDI;

    return 0;
  }

  bool getLocationInfo(const Value *V, std::string &DisplayName,
                       std::string &Type, unsigned &LineNo, std::string &File,
                       std::string &Dir) {
    DICompileUnit Unit;
    DIType TypeD;

    if (GlobalVariable *GV = dyn_cast<GlobalVariable>(const_cast<Value*>(V))) {
      Value *DIGV = findDbgGlobalDeclare(GV);
      if (!DIGV) return false;
      DIGlobalVariable Var(cast<MDNode>(DIGV));

      Var.getDisplayName(DisplayName);
      LineNo = Var.getLineNumber();
      Unit = Var.getCompileUnit();
      TypeD = Var.getType();
    } else {
      const DbgDeclareInst *DDI = findDbgDeclare(V);
      if (!DDI) return false;
      DIVariable Var(cast<MDNode>(DDI->getVariable()));

      Var.getName(DisplayName);
      LineNo = Var.getLineNumber();
      Unit = Var.getCompileUnit();
      TypeD = Var.getType();
    }

    TypeD.getName(Type);
    Unit.getFilename(File);
    Unit.getDirectory(Dir);
    return true;
  }

  /// isValidDebugInfoIntrinsic - Return true if SPI is a valid debug 
  /// info intrinsic.
  bool isValidDebugInfoIntrinsic(DbgStopPointInst &SPI, 
                                 CodeGenOpt::Level OptLev) {
    return DIDescriptor::ValidDebugInfo(SPI.getContext(), OptLev);
  }

  /// isValidDebugInfoIntrinsic - Return true if FSI is a valid debug 
  /// info intrinsic.
  bool isValidDebugInfoIntrinsic(DbgFuncStartInst &FSI,
                                 CodeGenOpt::Level OptLev) {
    return DIDescriptor::ValidDebugInfo(FSI.getSubprogram(), OptLev);
  }

  /// isValidDebugInfoIntrinsic - Return true if RSI is a valid debug 
  /// info intrinsic.
  bool isValidDebugInfoIntrinsic(DbgRegionStartInst &RSI,
                                 CodeGenOpt::Level OptLev) {
    return DIDescriptor::ValidDebugInfo(RSI.getContext(), OptLev);
  }

  /// isValidDebugInfoIntrinsic - Return true if REI is a valid debug 
  /// info intrinsic.
  bool isValidDebugInfoIntrinsic(DbgRegionEndInst &REI,
                                 CodeGenOpt::Level OptLev) {
    return DIDescriptor::ValidDebugInfo(REI.getContext(), OptLev);
  }


  /// isValidDebugInfoIntrinsic - Return true if DI is a valid debug 
  /// info intrinsic.
  bool isValidDebugInfoIntrinsic(DbgDeclareInst &DI,
                                 CodeGenOpt::Level OptLev) {
    return DIDescriptor::ValidDebugInfo(DI.getVariable(), OptLev);
  }

  /// ExtractDebugLocation - Extract debug location information 
  /// from llvm.dbg.stoppoint intrinsic.
  DebugLoc ExtractDebugLocation(DbgStopPointInst &SPI,
                                DebugLocTracker &DebugLocInfo) {
    DebugLoc DL;
    Value *Context = SPI.getContext();

    // If this location is already tracked then use it.
    DebugLocTuple Tuple(cast<MDNode>(Context), SPI.getLine(), 
                        SPI.getColumn());
    DenseMap<DebugLocTuple, unsigned>::iterator II
      = DebugLocInfo.DebugIdMap.find(Tuple);
    if (II != DebugLocInfo.DebugIdMap.end())
      return DebugLoc::get(II->second);

    // Add a new location entry.
    unsigned Id = DebugLocInfo.DebugLocations.size();
    DebugLocInfo.DebugLocations.push_back(Tuple);
    DebugLocInfo.DebugIdMap[Tuple] = Id;
    
    return DebugLoc::get(Id);
  }

  /// ExtractDebugLocation - Extract debug location information 
  /// from llvm.dbg.func_start intrinsic.
  DebugLoc ExtractDebugLocation(DbgFuncStartInst &FSI,
                                DebugLocTracker &DebugLocInfo) {
    DebugLoc DL;
    Value *SP = FSI.getSubprogram();

    DISubprogram Subprogram(cast<MDNode>(SP));
    unsigned Line = Subprogram.getLineNumber();
    DICompileUnit CU(Subprogram.getCompileUnit());

    // If this location is already tracked then use it.
    DebugLocTuple Tuple(CU.getNode(), Line, /* Column */ 0);
    DenseMap<DebugLocTuple, unsigned>::iterator II
      = DebugLocInfo.DebugIdMap.find(Tuple);
    if (II != DebugLocInfo.DebugIdMap.end())
      return DebugLoc::get(II->second);

    // Add a new location entry.
    unsigned Id = DebugLocInfo.DebugLocations.size();
    DebugLocInfo.DebugLocations.push_back(Tuple);
    DebugLocInfo.DebugIdMap[Tuple] = Id;
    
    return DebugLoc::get(Id);
  }

  /// isInlinedFnStart - Return true if FSI is starting an inlined function.
  bool isInlinedFnStart(DbgFuncStartInst &FSI, const Function *CurrentFn) {
    DISubprogram Subprogram(cast<MDNode>(FSI.getSubprogram()));
    if (Subprogram.describes(CurrentFn))
      return false;

    return true;
  }

  /// isInlinedFnEnd - Return true if REI is ending an inlined function.
  bool isInlinedFnEnd(DbgRegionEndInst &REI, const Function *CurrentFn) {
    DISubprogram Subprogram(cast<MDNode>(REI.getContext()));
    if (Subprogram.isNull() || Subprogram.describes(CurrentFn))
      return false;

    return true;
  }
}
