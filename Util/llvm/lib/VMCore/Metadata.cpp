//===-- Metadata.cpp - Implement Metadata classes -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Metadata classes.
//
//===----------------------------------------------------------------------===//

#include "LLVMContextImpl.h"
#include "llvm/Metadata.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/Instruction.h"
#include "SymbolTableListTraitsImpl.h"
using namespace llvm;

//===----------------------------------------------------------------------===//
//MetadataBase implementation
//

/// resizeOperands - Metadata keeps track of other metadata uses using 
/// OperandList. Resize this list to hold anticipated number of metadata
/// operands.
void MetadataBase::resizeOperands(unsigned NumOps) {
  unsigned e = getNumOperands();
  if (NumOps == 0) {
    NumOps = e*2;
    if (NumOps < 2) NumOps = 2;  
  } else if (NumOps > NumOperands) {
    // No resize needed.
    if (ReservedSpace >= NumOps) return;
  } else if (NumOps == NumOperands) {
    if (ReservedSpace == NumOps) return;
  } else {
    return;
  }

  ReservedSpace = NumOps;
  Use *OldOps = OperandList;
  Use *NewOps = allocHungoffUses(NumOps);
  std::copy(OldOps, OldOps + e, NewOps);
  OperandList = NewOps;
  if (OldOps) Use::zap(OldOps, OldOps + e, true);
}
//===----------------------------------------------------------------------===//
//MDString implementation
//
MDString *MDString::get(LLVMContext &Context, const StringRef &Str) {
  LLVMContextImpl *pImpl = Context.pImpl;
  sys::SmartScopedWriter<true> Writer(pImpl->ConstantsLock);
  StringMapEntry<MDString *> &Entry = 
    pImpl->MDStringCache.GetOrCreateValue(Str);
  MDString *&S = Entry.getValue();
  if (!S) S = new MDString(Context, Entry.getKeyData(),
                           Entry.getKeyLength());

  return S;
}

//===----------------------------------------------------------------------===//
//MDNode implementation
//
MDNode::MDNode(LLVMContext &C, Value*const* Vals, unsigned NumVals)
  : MetadataBase(Type::getMetadataTy(C), Value::MDNodeVal) {
  NumOperands = 0;
  resizeOperands(NumVals);
  for (unsigned i = 0; i != NumVals; ++i) {
    // Only record metadata uses.
    if (MetadataBase *MB = dyn_cast_or_null<MetadataBase>(Vals[i]))
      OperandList[NumOperands++] = MB;
    else if(Vals[i] && 
            Vals[i]->getType()->getTypeID() == Type::MetadataTyID)
      OperandList[NumOperands++] = Vals[i];
    Node.push_back(ElementVH(Vals[i], this));
  }
}

void MDNode::Profile(FoldingSetNodeID &ID) const {
  for (const_elem_iterator I = elem_begin(), E = elem_end(); I != E; ++I)
    ID.AddPointer(*I);
}

MDNode *MDNode::get(LLVMContext &Context, Value*const* Vals, unsigned NumVals) {
  LLVMContextImpl *pImpl = Context.pImpl;
  FoldingSetNodeID ID;
  for (unsigned i = 0; i != NumVals; ++i)
    ID.AddPointer(Vals[i]);

  pImpl->ConstantsLock.reader_acquire();
  void *InsertPoint;
  MDNode *N = pImpl->MDNodeSet.FindNodeOrInsertPos(ID, InsertPoint);
  pImpl->ConstantsLock.reader_release();
  
  if (!N) {
    sys::SmartScopedWriter<true> Writer(pImpl->ConstantsLock);
    N = pImpl->MDNodeSet.FindNodeOrInsertPos(ID, InsertPoint);
    if (!N) {
      // InsertPoint will have been set by the FindNodeOrInsertPos call.
      N = new MDNode(Context, Vals, NumVals);
      pImpl->MDNodeSet.InsertNode(N, InsertPoint);
    }
  }

  return N;
}

/// dropAllReferences - Remove all uses and clear node vector.
void MDNode::dropAllReferences() {
  User::dropAllReferences();
  Node.clear();
}

MDNode::~MDNode() {
  {
    LLVMContextImpl *pImpl = getType()->getContext().pImpl;
    sys::SmartScopedWriter<true> Writer(pImpl->ConstantsLock);
    pImpl->MDNodeSet.RemoveNode(this);
  }
  dropAllReferences();
}

// Replace value from this node's element list.
void MDNode::replaceElement(Value *From, Value *To) {
  if (From == To || !getType())
    return;
  LLVMContext &Context = getType()->getContext();
  LLVMContextImpl *pImpl = Context.pImpl;

  // Find value. This is a linear search, do something if it consumes 
  // lot of time. It is possible that to have multiple instances of
  // From in this MDNode's element list.
  SmallVector<unsigned, 4> Indexes;
  unsigned Index = 0;
  for (SmallVector<ElementVH, 4>::iterator I = Node.begin(),
         E = Node.end(); I != E; ++I, ++Index) {
    Value *V = *I;
    if (V && V == From) 
      Indexes.push_back(Index);
  }

  if (Indexes.empty())
    return;

  // Remove "this" from the context map. 
  {
    sys::SmartScopedWriter<true> Writer(pImpl->ConstantsLock);
    pImpl->MDNodeSet.RemoveNode(this);
  }

  // MDNode only lists metadata elements in operand list, because MDNode
  // used by MDNode is considered a valid use. However on the side, MDNode
  // using a non-metadata value is not considered a "use" of non-metadata
  // value.
  SmallVector<unsigned, 4> OpIndexes;
  unsigned OpIndex = 0;
  for (User::op_iterator OI = op_begin(), OE = op_end();
       OI != OE; ++OI, OpIndex++) {
    if (*OI == From)
      OpIndexes.push_back(OpIndex);
  }
  if (MetadataBase *MDTo = dyn_cast_or_null<MetadataBase>(To)) {
    for (SmallVector<unsigned, 4>::iterator OI = OpIndexes.begin(),
           OE = OpIndexes.end(); OI != OE; ++OI)
      setOperand(*OI, MDTo);
  } else {
    for (SmallVector<unsigned, 4>::iterator OI = OpIndexes.begin(),
           OE = OpIndexes.end(); OI != OE; ++OI)
      setOperand(*OI, 0);
  }

  // Replace From element(s) in place.
  for (SmallVector<unsigned, 4>::iterator I = Indexes.begin(), E = Indexes.end(); 
       I != E; ++I) {
    unsigned Index = *I;
    Node[Index] = ElementVH(To, this);
  }

  // Insert updated "this" into the context's folding node set.
  // If a node with same element list already exist then before inserting 
  // updated "this" into the folding node set, replace all uses of existing 
  // node with updated "this" node.
  FoldingSetNodeID ID;
  Profile(ID);
  pImpl->ConstantsLock.reader_acquire();
  void *InsertPoint;
  MDNode *N = pImpl->MDNodeSet.FindNodeOrInsertPos(ID, InsertPoint);
  pImpl->ConstantsLock.reader_release();

  if (N) {
    N->replaceAllUsesWith(this);
    delete N;
    N = 0;
  }

  {
    sys::SmartScopedWriter<true> Writer(pImpl->ConstantsLock);
    N = pImpl->MDNodeSet.FindNodeOrInsertPos(ID, InsertPoint);
    if (!N) {
      // InsertPoint will have been set by the FindNodeOrInsertPos call.
      N = this;
      pImpl->MDNodeSet.InsertNode(N, InsertPoint);
    }
  }
}

//===----------------------------------------------------------------------===//
//NamedMDNode implementation
//
NamedMDNode::NamedMDNode(LLVMContext &C, const Twine &N,
                         MetadataBase*const* MDs, 
                         unsigned NumMDs, Module *ParentModule)
  : MetadataBase(Type::getMetadataTy(C), Value::NamedMDNodeVal), Parent(0) {
  setName(N);
  NumOperands = 0;
  resizeOperands(NumMDs);

  for (unsigned i = 0; i != NumMDs; ++i) {
    if (MDs[i])
      OperandList[NumOperands++] = MDs[i];
    Node.push_back(WeakMetadataVH(MDs[i]));
  }
  if (ParentModule)
    ParentModule->getNamedMDList().push_back(this);
}

NamedMDNode *NamedMDNode::Create(const NamedMDNode *NMD, Module *M) {
  assert (NMD && "Invalid source NamedMDNode!");
  SmallVector<MetadataBase *, 4> Elems;
  for (unsigned i = 0, e = NMD->getNumElements(); i != e; ++i)
    Elems.push_back(NMD->getElement(i));
  return new NamedMDNode(NMD->getContext(), NMD->getName().data(),
                         Elems.data(), Elems.size(), M);
}

/// eraseFromParent - Drop all references and remove the node from parent
/// module.
void NamedMDNode::eraseFromParent() {
  getParent()->getNamedMDList().erase(this);
}

/// dropAllReferences - Remove all uses and clear node vector.
void NamedMDNode::dropAllReferences() {
  User::dropAllReferences();
  Node.clear();
}

NamedMDNode::~NamedMDNode() {
  dropAllReferences();
}

//===----------------------------------------------------------------------===//
//Metadata implementation
//

/// RegisterMDKind - Register a new metadata kind and return its ID.
/// A metadata kind can be registered only once. 
unsigned MetadataContext::RegisterMDKind(const char *Name) {
  assert (validName(Name) && "Invalid custome metadata name!");
  unsigned Count = MDHandlerNames.size();
  assert(MDHandlerNames.find(Name) == MDHandlerNames.end() 
         && "Already registered MDKind!");
  MDHandlerNames[Name] = Count + 1;
  return Count + 1;
}

/// validName - Return true if Name is a valid custom metadata handler name.
bool MetadataContext::validName(const char *Name) {
  if (!Name)
    return false;

  if (!isalpha(*Name))
    return false;

  unsigned Length = strlen(Name);  
  unsigned Count = 1;
  ++Name;
  while (Name &&
         (isalnum(*Name) || *Name == '_' || *Name == '-' || *Name == '.')) {
    ++Name;
    ++Count;
  }
  if (Length != Count)
    return false;
  return true;
}

/// getMDKind - Return metadata kind. If the requested metadata kind
/// is not registered then return 0.
unsigned MetadataContext::getMDKind(const char *Name) {
  assert (validName(Name) && "Invalid custome metadata name!");
  StringMap<unsigned>::iterator I = MDHandlerNames.find(Name);
  if (I == MDHandlerNames.end())
    return 0;

  return I->getValue();
}

/// addMD - Attach the metadata of given kind with an Instruction.
void MetadataContext::addMD(unsigned MDKind, MDNode *Node, Instruction *Inst) {
  assert (Node && "Unable to add custome metadata");
  Inst->HasMetadata = true;
  MDStoreTy::iterator I = MetadataStore.find(Inst);
  if (I == MetadataStore.end()) {
    MDMapTy Info;
    Info.push_back(std::make_pair(MDKind, Node));
    MetadataStore.insert(std::make_pair(Inst, Info));
    return;
  }

  MDMapTy &Info = I->second;
  // If there is an entry for this MDKind then replace it.
  for (unsigned i = 0, e = Info.size(); i != e; ++i) {
    MDPairTy &P = Info[i];
    if (P.first == MDKind) {
      Info[i] = std::make_pair(MDKind, Node);
      return;
    }
  }

  // Otherwise add a new entry.
  Info.push_back(std::make_pair(MDKind, Node));
  return;
}

/// removeMD - Remove metadata of given kind attached with an instuction.
void MetadataContext::removeMD(unsigned Kind, Instruction *Inst) {
  MDStoreTy::iterator I = MetadataStore.find(Inst);
  if (I == MetadataStore.end())
    return;

  MDMapTy &Info = I->second;
  for (MDMapTy::iterator MI = Info.begin(), ME = Info.end(); MI != ME; ++MI) {
    MDPairTy &P = *MI;
    if (P.first == Kind) {
      Info.erase(MI);
      return;
    }
  }

  return;
}
  
/// removeMDs - Remove all metadata attached with an instruction.
void MetadataContext::removeMDs(const Instruction *Inst) {
  // Find Metadata handles for this instruction.
  MDStoreTy::iterator I = MetadataStore.find(Inst);
  assert (I != MetadataStore.end() && "Invalid custom metadata info!");
  MDMapTy &Info = I->second;
  
  // FIXME : Give all metadata handlers a chance to adjust.
  
  // Remove the entries for this instruction.
  Info.clear();
  MetadataStore.erase(I);
}


/// getMD - Get the metadata of given kind attached with an Instruction.
/// If the metadata is not found then return 0.
MDNode *MetadataContext::getMD(unsigned MDKind, const Instruction *Inst) {
  MDStoreTy::iterator I = MetadataStore.find(Inst);
  if (I == MetadataStore.end())
    return NULL;
  
  MDMapTy &Info = I->second;
  for (MDMapTy::iterator I = Info.begin(), E = Info.end(); I != E; ++I)
    if (I->first == MDKind)
      return dyn_cast_or_null<MDNode>(I->second);
  return NULL;
}

/// getMDs - Get the metadata attached with an Instruction.
const MetadataContext::MDMapTy *MetadataContext::getMDs(const Instruction *Inst) {
  MDStoreTy::iterator I = MetadataStore.find(Inst);
  if (I == MetadataStore.end())
    return NULL;
  
  return &(I->second);
}

/// getHandlerNames - Get handler names. This is used by bitcode
/// writer.
const StringMap<unsigned> *MetadataContext::getHandlerNames() {
  return &MDHandlerNames;
}

/// ValueIsCloned - This handler is used to update metadata store
/// when In1 is cloned to create In2.
void MetadataContext::ValueIsCloned(const Instruction *In1, Instruction *In2) {
  // Find Metadata handles for In1.
  MDStoreTy::iterator I = MetadataStore.find(In1);
  assert (I != MetadataStore.end() && "Invalid custom metadata info!");

  // FIXME : Give all metadata handlers a chance to adjust.

  MDMapTy &In1Info = I->second;
  MDMapTy In2Info;
  for (MDMapTy::iterator I = In1Info.begin(), E = In1Info.end(); I != E; ++I)
    if (MDNode *MD = dyn_cast_or_null<MDNode>(I->second))
      addMD(I->first, MD, In2);
}
