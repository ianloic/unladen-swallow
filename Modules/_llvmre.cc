/* _llvmre.cpp */

#include "Python.h"
#include "_llvmfunctionobject.h"
#include "llvm_compile.h"
#include "Python/global_llvm_data.h"

#include "llvm/Support/Debug.h"

#include <string>

#include "llvm/Module.h"
#include "llvm/Function.h"
#include "llvm/DerivedTypes.h"
#include "llvm/LLVMContext.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/CallingConv.h"
#include "llvm/Attributes.h"

// for SRE_FLAG_*
#include "sre_constants.h"

using llvm::Module;
using llvm::Function;
using llvm::BasicBlock;

using llvm::Value;
using llvm::Type;
using llvm::IntegerType;
using llvm::PointerType;
using llvm::FunctionType;
using llvm::ConstantInt;

using llvm::BinaryOperator;
using llvm::CallInst;
using llvm::ReturnInst;
using llvm::BranchInst;
using llvm::SwitchInst;
using llvm::GetElementPtrInst;
using llvm::LoadInst;
using llvm::StoreInst;
using llvm::ICmpInst;

using llvm::ExecutionEngine;

// helper to produce better error messages
#define _PyErr_SetString(T,S) \
  PyErr_Format(T, S " (in %s at %s:%d)",__PRETTY_FUNCTION__ , __FILE__, __LINE__)

// FIXME: assert sizeof(unicode) == 2
#define CHAR_TYPE IntegerType::get(16)
#define CHAR_POINTER_TYPE PointerType::get(CHAR_TYPE, 0)
#define BOOL_TYPE IntegerType::get(1)
#define OFFSET_TYPE IntegerType::get(32)
#define OFFSET_POINTER_TYPE PointerType::get(OFFSET_TYPE, 0)

typedef int32_t ReOffset;
typedef ReOffset (*MatchFunction)(Py_UNICODE*, ReOffset, ReOffset, ReOffset*);
typedef ReOffset (*FindFunction)(Py_UNICODE*, ReOffset, ReOffset, ReOffset*, ReOffset*);

class CompiledRegEx;

// Wrapper functions for character class tests. To make things simpler these
// all accept a Py_UNICODE character and return bool
extern "C"
bool wrap_Py_UNICODE_ISDIGIT(Py_UNICODE c) {
  return Py_UNICODE_ISDIGIT(c) == 1;
}

extern "C"
bool wrap_isalnum(Py_UNICODE c) {
  return isalnum(c) != 0;
}

extern "C"
bool wrap_Py_UNICODE_ISALNUM(Py_UNICODE c) {
  return Py_UNICODE_ISALNUM(c) == 1;
}

extern "C"
bool wrap_isspace(Py_UNICODE c) {
  return isspace(c) != 0;
}

extern "C"
bool wrap_Py_UNICODE_ISSPACE(Py_UNICODE c) {
  return Py_UNICODE_ISSPACE(c) == 1;
}

// make sure there's a function in the RegEx object for the associated
// wrapper. for some reason the mapping isn't being picked up
// automatically so we have to use engine->addGlobalMapping. I don't think
// that we should, in theory...
#define ENSURE_TEST_FUNCTION(name) \
  if (regex.name == NULL) { \
    std::vector<const Type*> func_args; \
    func_args.push_back(CHAR_TYPE); \
    const FunctionType* func_type = \
      FunctionType::get(BOOL_TYPE, func_args, false); \
    regex.name = Function::Create(func_type, \
        Function::ExternalLinkage, "wrap_" #name, regex.module); \
    PyGlobalLlvmData *global_llvm_data = \
      PyThreadState_GET()->interp->global_llvm_data; \
    ExecutionEngine *engine = global_llvm_data->getExecutionEngine(); \
    engine->addGlobalMapping(regex.name, (void*)wrap_##name); \
  } \


typedef struct {
  PyObject_HEAD
  /* Type-specific fields go here. */
  Module* module;

  /* the compiled regular expression */
  CompiledRegEx* compiled;

  /* a function to find the first instance of that compiled regular 
   * expression */
  Function* find;

  int flags;
  int groups;

  // useful value to reuse
  Value* not_found;

  // external functions that get lazily created
  Function* Py_UNICODE_ISDIGIT;
  Function* isalnum;
  Function* Py_UNICODE_ISALNUM;
  Function* isspace;
  Function* Py_UNICODE_ISSPACE;
} RegEx;


class CompiledRegEx {
  public:
    CompiledRegEx(RegEx& regex, bool first=false);
    ~CompiledRegEx();

    // compile the result of sre_parse.parse
    bool Compile(PyObject* seq, Py_ssize_t index);

    // this holds useful regex-wide state
    RegEx& regex;
    // the function we're compiling
    Function* function;

  private:
    // this is the start of the pattern
    bool first;

    // arg values
    Value* string;
    Value* offset;
    Value* end_offset;
    Value* groups;

    // function local variables
    Value* offset_ptr;
    // the loaded character
    Value* character;

    // important BasicBlocks
    BasicBlock* entry;
    BasicBlock* return_offset;
    BasicBlock* return_not_found;

    // the last basic block in the program flow
    BasicBlock* last;

    // methods to create code for particular operations
    BasicBlock* literal(BasicBlock* block, PyObject* arg, bool not_literal);
    BasicBlock* any(BasicBlock* block);
    BasicBlock* in(BasicBlock* block, PyObject* arg);
    BasicBlock* repeat(BasicBlock* block, PyObject* arg, PyObject* seq, 
        Py_ssize_t index, bool is_greedy);
    BasicBlock* subpattern_begin(BasicBlock* block, PyObject* arg);
    BasicBlock* subpattern_end(BasicBlock* block, PyObject* arg);
    BasicBlock* branch(BasicBlock* block, PyObject* arg);
    BasicBlock* groupref_exists(BasicBlock* block, PyObject* arg);
    BasicBlock* at_end(BasicBlock* block);
    BasicBlock* at_beginning(BasicBlock* block);
    BasicBlock* at_boundary(BasicBlock* block, bool non_boundary);

    // helpers to generate commonly used code
    Value* loadOffset(BasicBlock* block);
    void storeOffset(BasicBlock* block, Value* value);
    BasicBlock* loadCharacter(BasicBlock* block);
    Function* greedy(Function* repeat, Function* after);
    Function* nongreedy(Function* repeat, Function* after);
    void testRange(BasicBlock* block, Value* c, int from, int to, BasicBlock* member, BasicBlock* nonmember);
    bool testCategory(BasicBlock* block, Value* c, const char* category, BasicBlock* member, BasicBlock* nonmember);

    // call unladen-swallow's optimizer
    bool optimize(Function* f);
};

CompiledRegEx::CompiledRegEx(RegEx& regex, bool first) 
  : regex(regex), first(first) {
}

CompiledRegEx::~CompiledRegEx() {
}

bool CompiledRegEx::optimize(Function* f) {
  if (regex.flags & 128) {
    // don't optimize if DEBUG is set
    return true;
  }
  // use fastcc if this isn't the first function we're calling
  if (f != function || !first) {
    // unfortunately this screws everything up
    //f->setCallingConv(llvm::CallingConv::Fast);
  }
  // optimize the function
	struct PyGlobalLlvmData *global_llvm_data = PyGlobalLlvmData::Get();
	if (global_llvm_data->Optimize(*f, 3) < 0) {
		PyErr_Format(PyExc_SystemError, "Failed to optimize to level %d", 3);
		return false;
  }
  return true;
}

Value* 
CompiledRegEx::loadOffset(BasicBlock* block) {
  return new LoadInst(offset_ptr, "offset", block);
}

void
CompiledRegEx::storeOffset(BasicBlock* block, Value* value) {
  new StoreInst(value, offset_ptr, block);
}

BasicBlock* 
CompiledRegEx::loadCharacter(BasicBlock* block) {
  // get the current offset
  Value* offset = loadOffset(block);
  // make sure it's not past the end of the string
  Value* ended = new ICmpInst(ICmpInst::ICMP_UGT, offset, end_offset, 
      "ended", block);
  // if it's ended, return not_found, otherwise, continue
  // I'm scared this will fuck things up
  BasicBlock* new_block = BasicBlock::Create("block", function);
  BranchInst::Create(return_not_found, new_block, ended, block);

  block = new_block;

  // load the character at the right offset
  Value* c_ptr = GetElementPtrInst::Create(string, offset, "c_ptr", block);
  character = new LoadInst(c_ptr, "c", block);
  // increment the offset
  offset = BinaryOperator::CreateAdd(offset,
      ConstantInt::get(OFFSET_TYPE, 1), "increment", block);
  storeOffset(block, offset);
  return block;
}

Function*
CompiledRegEx::greedy(Function* repeat, Function* after) 
{
  // create the function
  std::vector<const Type*> args_type;
  args_type.push_back(CHAR_POINTER_TYPE); // string
  args_type.push_back(OFFSET_TYPE); // offset
  args_type.push_back(OFFSET_TYPE); // end_offset
  args_type.push_back(OFFSET_POINTER_TYPE); // groups
  args_type.push_back(OFFSET_TYPE); // counter

  FunctionType *func_type = FunctionType::get(OFFSET_TYPE, args_type, false);

  // FIXME: choose better function flags
  Function* function = Function::Create(func_type, Function::ExternalLinkage,
      "recurse", regex.module);

  Function::arg_iterator args = function->arg_begin();
  Value* string = args++;
  string->setName("string");
  Value* offset = args++;
  offset->setName("offset");
  Value* end_offset = args++;
  end_offset->setName("end_offset");
  Value* groups = args++;
  groups->setName("groups");
  Value* countdown = args++;
  countdown->setName("countdown");

  // create BasicBlocks
  BasicBlock* call_repeat = BasicBlock::Create("call_repeat", function);
  BasicBlock* count = BasicBlock::Create("count", function);
  BasicBlock* recurse = BasicBlock::Create("recurse", function);
  BasicBlock* call_after = BasicBlock::Create("call_after", function);
  BasicBlock* return_offset = BasicBlock::Create("return_offset", function);

  // set up BasicBlock to return not_found
  BasicBlock* return_not_found = 
    BasicBlock::Create("return_not_found", function);
  ReturnInst::Create(regex.not_found, return_not_found);

  // call repeat
  std::vector<Value*> call_args;
  call_args.push_back(string);
  call_args.push_back(offset);
  call_args.push_back(end_offset);
  call_args.push_back(groups);
  Value* repeat_result = CallInst::Create(repeat, call_args.begin(),
      call_args.end(), "repeat_result", call_repeat);
  Value* repeat_result_not_found = new ICmpInst(ICmpInst::ICMP_EQ,
      repeat_result, regex.not_found, "repeat_result_not_found", call_repeat);
  BranchInst::Create(return_not_found, count, repeat_result_not_found, 
      call_repeat);

  // count
  Value* remaining = BinaryOperator::CreateSub(countdown,
      ConstantInt::get(OFFSET_TYPE, 1), "remaining", count);
  Value* stop_recursion = new ICmpInst(ICmpInst::ICMP_EQ, remaining,
      ConstantInt::get(OFFSET_TYPE, 0), "stop_recursion", count);
  BranchInst::Create(call_after, recurse, stop_recursion, count);

  // recurse
  call_args[1] = repeat_result;
  call_args.push_back(remaining);
  Value* recurse_result = CallInst::Create(function, call_args.begin(),
      call_args.end(), "recurse_result", recurse);
  Value* recurse_result_not_found = new ICmpInst(ICmpInst::ICMP_EQ,
      recurse_result, regex.not_found, "recurse_result_not_found", recurse);
  BranchInst::Create(call_after, return_offset, recurse_result_not_found, 
      recurse);

  // after
  call_args.resize(4);
  Value* after_result = CallInst::Create(after, call_args.begin(), 
      call_args.end(), "after_result", call_after);
  ReturnInst::Create(after_result, call_after);

  // return_offset
  ReturnInst::Create(recurse_result, return_offset);

  optimize(function);

  return function; 
}

Function*
CompiledRegEx::nongreedy(Function* repeat, Function* after) 
{
  // create the function
  std::vector<const Type*> args_type;
  args_type.push_back(CHAR_POINTER_TYPE); // string
  args_type.push_back(OFFSET_TYPE); // offset
  args_type.push_back(OFFSET_TYPE); // end_offset
  args_type.push_back(OFFSET_POINTER_TYPE); // groups
  args_type.push_back(OFFSET_TYPE); // counter

  FunctionType *func_type = FunctionType::get(OFFSET_TYPE, args_type, false);

  // FIXME: choose better function flags
  Function* function = Function::Create(func_type, Function::ExternalLinkage,
      "recurse", regex.module);

  Function::arg_iterator args = function->arg_begin();
  Value* string = args++;
  string->setName("string");
  Value* offset = args++;
  offset->setName("offset");
  Value* end_offset = args++;
  end_offset->setName("end_offset");
  Value* groups = args++;
  groups->setName("groups");
  Value* countdown = args++;
  countdown->setName("countdown");

  // create BasicBlocks
  BasicBlock* call_after = BasicBlock::Create("call_after", function);
  BasicBlock* call_repeat = BasicBlock::Create("call_repeat", function);
  BasicBlock* count = BasicBlock::Create("count", function);
  BasicBlock* recurse = BasicBlock::Create("recurse", function);
  BasicBlock* return_after_result = BasicBlock::Create("return_after_result", function);

  // set up BasicBlock to return not_found
  BasicBlock* return_not_found = 
    BasicBlock::Create("return_not_found", function);
  ReturnInst::Create(regex.not_found, return_not_found);

  // call after
  std::vector<Value*> call_args;
  call_args.push_back(string);
  call_args.push_back(offset);
  call_args.push_back(end_offset);
  call_args.push_back(groups);
  Value* after_result = CallInst::Create(after, call_args.begin(),
      call_args.end(), "after_result", call_after);
  Value* after_result_not_found = new ICmpInst(ICmpInst::ICMP_EQ,
      after_result, regex.not_found, "after_result_not_found", call_after);
  BranchInst::Create(call_repeat, return_after_result, after_result_not_found,
      call_after);

  // call repeat
  Value* repeat_result = CallInst::Create(repeat, call_args.begin(),
      call_args.end(), "repeat_result", call_repeat);
  Value* repeat_result_not_found = new ICmpInst(ICmpInst::ICMP_EQ,
      repeat_result, regex.not_found, "repeat_result_not_found", call_repeat);
  BranchInst::Create(return_not_found, count, repeat_result_not_found, 
      call_repeat);

  // count
  Value* remaining = BinaryOperator::CreateSub(countdown,
      ConstantInt::get(OFFSET_TYPE, 1), "remaining", count);
  Value* stop_recursion = new ICmpInst(ICmpInst::ICMP_EQ, remaining,
      ConstantInt::get(OFFSET_TYPE, 0), "stop_recursion", count);
  BranchInst::Create(return_not_found, recurse, stop_recursion, count);

  // recurse
  call_args[1] = repeat_result;
  call_args.push_back(remaining);
  Value* recurse_result = CallInst::Create(function, call_args.begin(),
      call_args.end(), "recurse_result", recurse);
  ReturnInst::Create(recurse_result, recurse);

  // return_after_result
  ReturnInst::Create(after_result, return_after_result);

  optimize(function);

  return function; 
}

void
CompiledRegEx::testRange(BasicBlock* block,
                         Value*      c,
                         int         from,
                         int         to,
                         BasicBlock* member, 
                         BasicBlock* nonmember) 
{
  /** in @block, test if the current character (@c) is in the range
   * @from to @to inclusive. If so jump to @member, else jump to @nonmember */

  // create a couple of new basic blocks for the range test
  BasicBlock* greater_equal = BasicBlock::Create("greater_equal", function);
  // test the character >= from
  Value* is_ge = new llvm::ICmpInst(llvm::ICmpInst::ICMP_UGE, c,
      ConstantInt::get(CHAR_TYPE, from), "is_ge", block);
  BranchInst::Create(greater_equal, nonmember, is_ge, block);
  // test the character <= to
  Value* is_le = new llvm::ICmpInst(llvm::ICmpInst::ICMP_ULE, c,
      ConstantInt::get(CHAR_TYPE, to), "is_le", greater_equal);
  BranchInst::Create(member, nonmember, is_le, greater_equal);
}

bool
CompiledRegEx::testCategory(BasicBlock* block,
                            Value* c,
                            const char* category, 
                            BasicBlock* member, 
                            BasicBlock* nonmember) 
{
  /** in @block, test if the character (@c) is a member 
   *  of @category and branch to @member or @nonmember as appropriate */
  if (!strcmp(category, "category_digit")) {
    if (regex.flags & SRE_FLAG_UNICODE) {
      // for unicode we call Py_UNICODE_ISDIGIT
      // except that function doesn't really exist. It's a macro that calls
      // _PyUnicode_IsDigit
      ENSURE_TEST_FUNCTION(Py_UNICODE_ISDIGIT)

      // call the function
      std::vector<Value*> args;
      args.push_back(c);
      Value* IsDigit_result = CallInst::Create(regex.Py_UNICODE_ISDIGIT,
          args.begin(), args.end(), "IsDigit_result", block);
      // go to the right successor block
      BranchInst::Create(member, nonmember, IsDigit_result, block);
    } else {
      // for non-unicode, test if it's in the range '0' - '9'
      testRange(block, c, '0', '9', member, nonmember);
    }
  } else if (!strcmp(category, "category_not_digit")) {
    // the opposite of the digit category
    testCategory(block, c, "category_digit", nonmember, member);
  } else if (!strcmp(category, "category_word")) {
    if (regex.flags & SRE_FLAG_LOCALE) {
      // match [0-9_] and whatever system isalnum matches
      BasicBlock* tmp1 = BasicBlock::Create("category_word_1", function);
      BasicBlock* tmp2 = BasicBlock::Create("category_word_2", function);
      testRange(block, c, '0', '9', member, tmp1);
      Value* is_underscore = new ICmpInst(ICmpInst::ICMP_EQ, c,
        ConstantInt::get(CHAR_TYPE, '_'), "is_underscore", tmp1);
      BranchInst::Create(member, tmp2, is_underscore, tmp1);
      // make sure isalnum is available to the JIT
      ENSURE_TEST_FUNCTION(isalnum);
      // call the function
      std::vector<Value*> args;
      args.push_back(c);
      Value* result = CallInst::Create(regex.isalnum,
          args.begin(), args.end(), "result", tmp2);
      // go to the right successor block
      BranchInst::Create(member, nonmember, result, tmp2);
    } else if (regex.flags & SRE_FLAG_UNICODE) {
      // match [0-9_] and whatever Py_UNICODE_ISALNUM matches
      BasicBlock* tmp1 = BasicBlock::Create("category_word_1", function);
      BasicBlock* tmp2 = BasicBlock::Create("category_word_2", function);
      testRange(block, c, '0', '9', member, tmp1);
      Value* is_underscore = new ICmpInst(ICmpInst::ICMP_EQ, c,
        ConstantInt::get(CHAR_TYPE, '_'), "is_underscore", tmp1);
      BranchInst::Create(member, tmp2, is_underscore, tmp1);
      // make sure Py_UNICODE_ISALNUM is available to the JIT
      ENSURE_TEST_FUNCTION(Py_UNICODE_ISALNUM);
      // call the function
      std::vector<Value*> args;
      args.push_back(c);
      Value* result = CallInst::Create(regex.Py_UNICODE_ISALNUM,
          args.begin(), args.end(), "result", tmp2);
      // go to the right successor block
      BranchInst::Create(member, nonmember, result, tmp2);
    } else {
      // match [a-zA-Z0-9_]
      BasicBlock* tmp1 = BasicBlock::Create("category_word_1", function);
      BasicBlock* tmp2 = BasicBlock::Create("category_word_2", function);
      BasicBlock* tmp3 = BasicBlock::Create("category_word_3", function);
      testRange(block, c, 'a', 'z', member, tmp1);
      testRange(tmp1, c, 'A', 'Z', member, tmp2);
      testRange(tmp2, c, '0', '9', member, tmp3);
      Value* is_underscore = new ICmpInst(ICmpInst::ICMP_EQ, c,
        ConstantInt::get(CHAR_TYPE, '_'), "is_underscore", tmp3);
      BranchInst::Create(member, nonmember, is_underscore, tmp3);
    }
  } else if (!strcmp(category, "category_not_word")) {
    testCategory(block, c, "category_word", nonmember, member);
  } else if (!strcmp(category, "category_space")) {
    // match [ \t\n\r\f\v]
    BasicBlock* unmatched = BasicBlock::Create("", function);
    // create a switch instruction
    SwitchInst* switch_ = SwitchInst::Create(c, unmatched, 6, block);
    switch_->addCase(ConstantInt::get(CHAR_TYPE, ' '), member);
    switch_->addCase(ConstantInt::get(CHAR_TYPE, '\t'), member);
    switch_->addCase(ConstantInt::get(CHAR_TYPE, '\n'), member);
    switch_->addCase(ConstantInt::get(CHAR_TYPE, '\r'), member);
    switch_->addCase(ConstantInt::get(CHAR_TYPE, '\f'), member);
    switch_->addCase(ConstantInt::get(CHAR_TYPE, '\v'), member);
    if (regex.flags & SRE_FLAG_LOCALE) {
      // also match isspace
      // make sure isspace is available to the JIT
      ENSURE_TEST_FUNCTION(isspace);
      // call the function
      std::vector<Value*> args;
      args.push_back(c);
      Value* result = CallInst::Create(regex.isspace,
          args.begin(), args.end(), "result", unmatched);
      // go to the right successor block
      BranchInst::Create(member, nonmember, result, unmatched);
    } else if (regex.flags & SRE_FLAG_UNICODE) {
      // also match Py_UNICODE_ISSPACE
      // make sure Py_UNICODE_ISSPACE is available to the JIT
      ENSURE_TEST_FUNCTION(Py_UNICODE_ISSPACE);
      // call the function
      std::vector<Value*> args;
      args.push_back(c);
      Value* result = CallInst::Create(regex.Py_UNICODE_ISSPACE,
          args.begin(), args.end(), "result", unmatched);
      // go to the right successor block
      BranchInst::Create(member, nonmember, result, unmatched);
    } else {
      // didn't match
      BranchInst::Create(nonmember, unmatched);
    }
  } else if (!strcmp(category, "category_not_space")) {
    testCategory(block, c, "category_space", nonmember, member);
  } else {
    PyErr_Format(PyExc_ValueError, "Unsupported SRE category '%s'", category);
    return false;
  }
  return true;
}

bool 
CompiledRegEx::Compile(PyObject* seq, Py_ssize_t index)
{
  // make sure we got a sequence
  if (!PySequence_Check(seq)) {
    _PyErr_SetString(PyExc_TypeError, "Expected a sequence");
    return false;
  }

  std::vector<const Type*> args_type;
  args_type.push_back(CHAR_POINTER_TYPE); // string
  args_type.push_back(OFFSET_TYPE); // offset
  args_type.push_back(OFFSET_TYPE); // end_offset
  args_type.push_back(OFFSET_POINTER_TYPE); // groups
  FunctionType *func_type = FunctionType::get(OFFSET_TYPE, args_type, false);

  // FIXME: choose better function flags
  function = Function::Create(func_type, Function::ExternalLinkage, 
      "pattern", regex.module);

  // get and name the arguments
  Function::arg_iterator args = function->arg_begin();
  string = args++;
  string->setName("string");
  offset = args++;
  offset->setName("offset");
  end_offset = args++;
  end_offset->setName("end_offset");
  groups = args++;
  groups->setName("groups");

  // create the entry BasicBlock
  entry = BasicBlock::Create("entry", function);
  offset_ptr = new llvm::AllocaInst(OFFSET_TYPE, "offset_ptr", entry);
  new StoreInst(offset, offset_ptr, entry);

  // create a block that returns the offset
  return_offset = BasicBlock::Create("return_offset", function);
  Value* _offset = new LoadInst(offset_ptr, "offset", return_offset);
  ReturnInst::Create(_offset, return_offset);

  // create a block that returns "not found"
  return_not_found = BasicBlock::Create("return_not_found", function);
  ReturnInst::Create(regex.not_found, return_not_found);

  last = entry;

  Py_ssize_t seq_length = PySequence_Size(seq);
  while (index < seq_length) {
    // get the index-th item in the sequence
    PyObject* element = PySequence_GetItem(seq, index);
    // make sure that that item is a sequence
    if (!PySequence_Check(element)) {
      _PyErr_SetString(PyExc_TypeError, "Expected a sequence");
      Py_XDECREF(element);
      return false;
    }
    // whose length is two
    if (PySequence_Size(element) != 2) {
      _PyErr_SetString(PyExc_ValueError, "Expected a 2-sequence");
      Py_XDECREF(element);
      return false;
    }
    // the first item is a string
    PyObject* op = PySequence_GetItem(element, 0);
    if (!PyString_Check(op)) {
      _PyErr_SetString(PyExc_TypeError, "Expected a string");
      Py_XDECREF(op);
      Py_XDECREF(element);
      return false;
    }
    const char* op_str = PyString_AsString(op);

    // second is the argument
    PyObject* arg = PySequence_GetItem(element, 1);

    // we have a reference to the items, we don't need a reference to the 
    // sequence
    Py_XDECREF(element);

    // create a basic block for the start of this operation
    BasicBlock* block = BasicBlock::Create(op_str, function);
    // if there was a previous block, jump from that one to this operation's
    // start block
    if (last) {
      BranchInst::Create(block, last);
      last = NULL;
    }

    if (!strcmp(op_str, "literal")) {
      last = literal(block, arg, false);
    } else if (!strcmp(op_str, "not_literal")) {
      last = literal(block, arg, true);
    } else if (!strcmp(op_str, "any")) {
      last = any(block);
    } else if (!strcmp(op_str, "in")) {
      last = in(block, arg);
    } else if (!strcmp(op_str, "max_repeat")) {
      last = repeat(block, arg, seq, index, true);
    } else if (!strcmp(op_str, "min_repeat")) {
      last = repeat(block, arg, seq, index, false);
    } else if (!strcmp(op_str, "subpattern_begin")) {
      last = subpattern_begin(block, arg);
    } else if (!strcmp(op_str, "subpattern_end")) {
      last = subpattern_end(block, arg);
    } else if (!strcmp(op_str, "branch")) {
      last = branch(block, arg);
    } else if (!strcmp(op_str, "groupref_exists")) {
      last = groupref_exists(block, arg);
    } else if (!strcmp(op_str, "at")) {
      // the arg is just a string
      if (!PyString_Check(arg)) {
        _PyErr_SetString(PyExc_TypeError, "Expected a string");
        return NULL;
      }
      const char* arg_str = PyString_AsString(arg);
      if (!strcmp(arg_str, "at_end")) {
        last = at_end(block);
      } else if (!strcmp(arg_str, "at_beginning")) {
        last = at_beginning(block);
      } else if (!strcmp(arg_str, "at_boundary")) {
        last = at_boundary(block, false);
      } else if (!strcmp(arg_str, "at_non_boundary")) {
        last = at_boundary(block, true);
      } else {
        PyErr_Format(PyExc_ValueError, "Unexpected SRE at code '%s'", arg_str);
        return NULL;
      }
    } else {
      PyErr_Format(PyExc_ValueError, "Unsupported SRE code '%s'", op_str);
      Py_XDECREF(op);
      Py_XDECREF(arg);
      return false;
    }

    // release operation name and arguments
    Py_XDECREF(op);
    Py_XDECREF(arg);

    if (PyErr_Occurred()) {
      // an error occurred, return
      return false;
    }

    if (last == NULL) {
      // control flow ends, we're done here
      return optimize(function);
    }

    index++;
  }

  // we've processed the whole pattern, if we are matching here, return success
  BranchInst::Create(return_offset, last);

  return optimize(function);

}

BasicBlock*
CompiledRegEx::literal(BasicBlock* block, PyObject* arg, bool not_literal) {
  // the argument should just be an integer
  if (!PyInt_Check(arg)) {
    _PyErr_SetString(PyExc_TypeError, "Expected an integer");
    return NULL;
  }

  // get the next character
  block = loadCharacter(block);
  // is it equal to the character in the pattern
  Value* c_equal = new llvm::ICmpInst(llvm::ICmpInst::ICMP_EQ, character,
      ConstantInt::get(CHAR_TYPE, PyInt_AsLong(arg)), 
      "c_equal", block);

  // create a block to continue to on success
  BasicBlock* post = BasicBlock::Create("post_literal", function);

  // depending on the kind of operation either continue or return not_found
  if (not_literal) { /* not_literal */
    BranchInst::Create(return_not_found, post, c_equal, block);
  } else { /* literal */
    BranchInst::Create(post, return_not_found, c_equal, block);
  }

  // clear any Python exception state that we don't care about
  PyErr_Clear();
  return post;
}

BasicBlock*
CompiledRegEx::any(BasicBlock* block) {
  // get the next character
  block = loadCharacter(block);

  if (regex.flags & SRE_FLAG_DOTALL) {
    // 'any' matches anything
  } else {
    // 'any' matches anything except for '\n'
    // is newline?
    Value* c_newline = new llvm::ICmpInst(llvm::ICmpInst::ICMP_EQ, character,
        ConstantInt::get(CHAR_TYPE, '\n'), "c_newline", block);

    // create a block to continue to on success
    BasicBlock* post = BasicBlock::Create("post_any", function);
    BranchInst::Create(return_not_found, post, c_newline, block);
    block = post;
  }

  return block;
}

BasicBlock*
CompiledRegEx::in(BasicBlock* block, PyObject* arg) {
  // the argument should be a sequence
  if (!PySequence_Check(arg)) {
    _PyErr_SetString(PyExc_TypeError, "Expected a sequence");
    return NULL;
  }

  // get the next character
  block = loadCharacter(block);

  // create a block where more tests can take place (ie: ranges, categories)
  BasicBlock* more_tests = BasicBlock::Create("more_tests", function);

  // create a block for matches
  BasicBlock* matched = BasicBlock::Create("matched", function);

  // create a switch instruction to use for all the literals
  SwitchInst* switch_ = SwitchInst::Create(character, more_tests, 
      PySequence_Size(arg), block);

  // [^...] ?
  bool negate = false;

  // iterate through the argument building a switch statement and other tests
  Py_ssize_t arg_length = PySequence_Size(arg);
  for(Py_ssize_t i=0; i<arg_length; i++) {
    // each item in the sequences should be a 2-tuple
    PyObject* item = PySequence_GetItem(arg, i);
    if (!PyTuple_Check(item)) {
      _PyErr_SetString(PyExc_TypeError, "Expected a tuple");
      Py_XDECREF(item);
      return NULL;
    }
    if (PyTuple_Size(item) != 2) {
      _PyErr_SetString(PyExc_ValueError, "Expected a 2-tuple");
      Py_XDECREF(item);
      return NULL;
    }

    // get the op and arguments
    PyObject* op = PyTuple_GetItem(item, 0);
    PyObject* op_arg = PyTuple_GetItem(item, 1);

    // the op is a string, 
    if (!PyString_Check(op)) {
      _PyErr_SetString(PyExc_TypeError, "Expected a string");
      Py_XDECREF(item);
      return false;
    }
    const char* op_str = PyString_AsString(op);

    // handle each kind of thing that can appear in an "in" operation
    if (i == 0 && !strcmp(op_str, "negate")) {
      // if the first op is 'negate' then we match anything *not* listed
      negate = true;
    } else if (!strcmp(op_str, "literal")) {
      // the argument should just be an integer
      if (!PyInt_Check(op_arg)) {
        _PyErr_SetString(PyExc_TypeError, "Expected an integer");
        return NULL;
      }

      // add a switch case
      switch_->addCase(ConstantInt::get(CHAR_TYPE, PyInt_AsLong(op_arg)),
          negate?return_not_found:matched);
    } else if (!strcmp(op_str, "range")) {
      // parse the start and end of the range
      int from, to;
      if(!PyArg_ParseTuple(op_arg, "ii", &from, &to)) {
        _PyErr_SetString(PyExc_ValueError, "Expected a 2-tuple of integers");
        Py_XDECREF(item);
        return NULL;
      }
      // lose the reference to the tuple
      Py_XDECREF(item);

      BasicBlock* yet_more_tests = BasicBlock::Create("more_tests", function);
      testRange(more_tests, character, from, to, 
          negate ? return_not_found : matched, yet_more_tests);
      more_tests = yet_more_tests;
    } else if (!strcmp(op_str, "category")) {
      if (!PyString_Check(op_arg)) {
        _PyErr_SetString(PyExc_TypeError, "Expected a string category name");
        Py_XDECREF(item);
        return false;
      }
      const char* category_name = PyString_AsString(op_arg);
      BasicBlock* yet_more_tests = BasicBlock::Create("more_tests", function);
      if (!testCategory(more_tests, character, category_name, 
            negate?return_not_found:matched, yet_more_tests)) {
        return false;
      }
      more_tests = yet_more_tests;
    } else {
      PyErr_Format(PyExc_ValueError, "Unsupported SRE code '%s' in 'in'", op_str);
      Py_XDECREF(item);
      return false;
    }
    Py_XDECREF(item);
  }

  // if we get to more_tests at the end of the 'in' tuple, then there isn't
  // a match.
  BranchInst::Create(negate?matched:return_not_found, more_tests);
  
  // clear any Python exception state that we don't care about
  PyErr_Clear();
  return matched;
}

BasicBlock*
CompiledRegEx::branch(BasicBlock* block, PyObject* arg) {
  // @arg is a tuple of (None, [branch1, branch2, branch3...])
  if (!PyTuple_Check(arg)) {
    _PyErr_SetString(PyExc_TypeError, "Expected a tuple");
    return NULL;
  }

  PyObject* branches = PyTuple_GetItem(arg, 1);
  // branches should be a sequence
  if (!PySequence_Check(branches)) {
    _PyErr_SetString(PyExc_TypeError, "Expected a sequence");
    return NULL;
  }

  Py_ssize_t num_branches = PySequence_Size(branches);
  if (num_branches == -1) {
    _PyErr_SetString(PyExc_TypeError, "Failed to get sequence length");
    return NULL;
  }

  // the basic block that we'll jump to when we've matched a branch
  BasicBlock* matched = BasicBlock::Create("matched", function);

  // prepare the arguments for the branches
  std::vector<Value*> args;
  args.push_back(string);
  args.push_back(loadOffset(block));
  args.push_back(end_offset);
  args.push_back(groups);

  for (Py_ssize_t i=0; i<num_branches; i++) {
    // the block we'll go to if there's a match
    BasicBlock* match =  BasicBlock::Create("match", function);
    // the next basic block we'll branch from
    BasicBlock* next = BasicBlock::Create("branch", function);

    // get the branch sequence
    PyObject* branch = PySequence_GetItem(branches, i);
    if (branch == NULL) {
      _PyErr_SetString(PyExc_TypeError, "Failed to get branch");
      return NULL;
    }

    // compile it to a function
    CompiledRegEx compiled_branch(regex);
    compiled_branch.Compile(branch, 0);
    // done with the branch object
    Py_XDECREF(branch);

    // call it
    Value* branch_result = CallInst::Create(compiled_branch.function, 
        args.begin(), args.end(), "branch_result", block);
    // check it
    Value* branch_result_not_found = new ICmpInst(ICmpInst::ICMP_EQ, 
        branch_result, regex.not_found, "branch_result_not_found", block);
    // no match means run the next check, otherwise go to match:
    BranchInst::Create(next, match, branch_result_not_found, block);

    // store the new offset, go to the final block
    storeOffset(match, branch_result);
    BranchInst::Create(matched, match);

    // next becomes current
    block = next;
  }

  // once we've tried all of the branches we've failed
  BranchInst::Create(return_not_found, block);

  // clear any Python exception state that we don't care about
  PyErr_Clear();

  return matched;
}

BasicBlock*
CompiledRegEx::at_end(BasicBlock* block)
{
  // match end of string, or \n before the end of the string
  // if in MULTILINE mode, also match just \n
  
  bool multiline = regex.flags & SRE_FLAG_MULTILINE;
 
  // make the blocks we need
  BasicBlock* test_slash_n = BasicBlock::Create("test_slash_n", function);
  BasicBlock* test_near_end;
  if (!multiline) {
   test_near_end = BasicBlock::Create("test_near_end", function);
  }
  BasicBlock* next_block = BasicBlock::Create("block", function);
  Value* offset = loadOffset(block);

  // are we at the end?
  Value* ended = new ICmpInst(ICmpInst::ICMP_UGE, offset, end_offset, 
      "ended", block);
  BranchInst::Create(next_block, test_slash_n, ended, block);

  // is there a \n in the current position?
  Value* c_ptr = GetElementPtrInst::Create(string, offset, "c_ptr", 
      test_slash_n);
  Value* c = new LoadInst(c_ptr, "c", test_slash_n);
  Value* c_slash_n = new ICmpInst(ICmpInst::ICMP_EQ, c, 
      ConstantInt::get(CHAR_TYPE, '\n'), "c_slash_n", test_slash_n);
  // for MULTILINE the \n means a match, for non MULTILINE we need to check
  // that we're almost at the end
  BranchInst::Create(multiline ? next_block : test_near_end, return_not_found,
      c_slash_n, test_slash_n);

  if (!multiline) {
    // are we near the end?
    Value* offset_plus_one = BinaryOperator::CreateAdd(offset,
        ConstantInt::get(OFFSET_TYPE, 1), "offset_plus_one", test_near_end);
    Value* near_end = new ICmpInst(ICmpInst::ICMP_UGE, offset_plus_one, 
        end_offset, "near_end", test_near_end);
    // either go to the next or return not_found
    BranchInst::Create(next_block, return_not_found, near_end, test_near_end);
  }

  return next_block;
}


BasicBlock*
CompiledRegEx::at_beginning(BasicBlock* block)
{
  // match the start of the string, also in multiline mode match after a
  // \n

  bool multiline = regex.flags & SRE_FLAG_MULTILINE;

  Value* offset = loadOffset(block);

  BasicBlock* test_slash_n;
  if (multiline) {
   test_slash_n = BasicBlock::Create("test_slash_n", function);
  }
  BasicBlock* next_block = BasicBlock::Create("block", function);

  // are we at the start of the string?
  Value* start = new ICmpInst(ICmpInst::ICMP_EQ, offset, 
      ConstantInt::get(OFFSET_TYPE, 0), "start", block);
  // for multiline, we also check for \n before the offset
  BranchInst::Create(next_block, multiline ? test_slash_n : return_not_found,
      start, block);

  if (multiline) {
    // load the character back one
    Value* previous_offset = BinaryOperator::CreateSub(offset,
        ConstantInt::get(OFFSET_TYPE, 1), "previous_offset", test_slash_n);
    Value* previous_c_ptr = GetElementPtrInst::Create(string, 
        previous_offset, "previous_c_ptr", 
      test_slash_n);
    Value* previous_c = new LoadInst(previous_c_ptr, "previous_c", 
        test_slash_n);
    // is it \n?
    Value* previous_c_slash_n = new ICmpInst(ICmpInst::ICMP_EQ, previous_c, 
        ConstantInt::get(CHAR_TYPE, '\n'), "previous_c_slash_n", 
        test_slash_n);
    BranchInst::Create(next_block, return_not_found, previous_c_slash_n,
        test_slash_n);
  }

  return next_block;
}

BasicBlock*
CompiledRegEx::at_boundary(BasicBlock* block, bool non_boundary) {
  // at a boundary if next (@offset) and previous (@offset-1) have different
  // word-ness, as determined by category_word. 
  // also, the start and end of strings are non-word.
 
  // set up blocks
  BasicBlock* test_prev = BasicBlock::Create("test_prev", function);
  BasicBlock* post_test_prev = BasicBlock::Create("post_test_prev", function);
  BasicBlock* pre_test_next = BasicBlock::Create("pre_test_next", function);
  BasicBlock* test_next = BasicBlock::Create("test_next", function);
  BasicBlock* post_test_next = BasicBlock::Create("post_test_next", function);
  BasicBlock* test_word = BasicBlock::Create("test_word", function);
  BasicBlock* next_block = BasicBlock::Create("block", function);

  // initial block
  // variables to hold the word-ness of the previous and next characters
  Value* prev_word_ptr = new llvm::AllocaInst(BOOL_TYPE, "prev_word_ptr", 
      block);
  Value* next_word_ptr = new llvm::AllocaInst(BOOL_TYPE, "next_word_ptr", 
      block);

  // load the offset
  Value* offset = loadOffset(block);
  // set the next/prev word-ness values based on the offset
  Value* not_start = new ICmpInst(ICmpInst::ICMP_NE, offset,
      ConstantInt::get(OFFSET_TYPE, 0), "not_start", block);
  new StoreInst(not_start, prev_word_ptr, block);
  Value* not_end = new ICmpInst(ICmpInst::ICMP_ULT, offset, 
      end_offset, "not_end", block);
  new StoreInst(not_end, next_word_ptr, block);

  // if we're not at the start then test the word-ness of the previous char
  BranchInst::Create(test_prev, pre_test_next, not_start, block);

  // test the previous character's word-ness
  Value* prev_off = BinaryOperator::CreateSub(offset, 
      ConstantInt::get(OFFSET_TYPE, 1), "prev_off", test_prev);
  Value* prev_c_ptr = GetElementPtrInst::Create(string, prev_off, "prev_c_ptr",
      test_prev);
  Value* prev_c = new LoadInst(prev_c_ptr, "prev_c", test_prev);
  testCategory(test_prev, prev_c, "category_word", pre_test_next, 
      post_test_prev);
  
  // the previous is not a word, store that
  new StoreInst(ConstantInt::get(BOOL_TYPE, 0), prev_word_ptr, post_test_prev);
  BranchInst::Create(pre_test_next, post_test_prev);

  // if we're not at the end then test the word-ness of the next character
  BranchInst::Create(test_next, test_word, not_end, pre_test_next);

  // test the next character's word-ness
  Value* next_c_ptr = GetElementPtrInst::Create(string, offset, "next_c_ptr",
      test_next);
  Value* next_c = new LoadInst(next_c_ptr, "next_c", test_next);
  testCategory(test_next, next_c, "category_word", test_word, post_test_next);

  // the next is not a word, store that
  new StoreInst(ConstantInt::get(BOOL_TYPE, 0), next_word_ptr, post_test_next);
  BranchInst::Create(test_word, post_test_next);

  // compare the word-ness of the previous and next characters
  Value* prev_word = new LoadInst(prev_word_ptr, "prev_word", test_word);
  Value* next_word = new LoadInst(next_word_ptr, "next_word", test_word);
  Value* boundary = new ICmpInst(ICmpInst::ICMP_NE, prev_word, next_word,
      "boundary", test_word);

  if (non_boundary) {
    BranchInst::Create(return_not_found, next_block, boundary, test_word);
  } else {
    BranchInst::Create(next_block, return_not_found, boundary, test_word);
  }

  return next_block;
}

BasicBlock*
CompiledRegEx::repeat(BasicBlock* block, PyObject* arg, PyObject* seq, 
    Py_ssize_t index, bool is_greedy) {
  // parse the arguments
  int min, max;
  PyObject* sub_pattern;
  
  if (!PyArg_ParseTuple(arg, "iiO", &min, &max, &sub_pattern) ||
      !PySequence_Check(sub_pattern)) {
    _PyErr_SetString(PyExc_TypeError, "Expected a tuple: int, int, sequence");
    return NULL;
  }

  CompiledRegEx* repeated = new CompiledRegEx(regex);
  repeated->Compile(sub_pattern, 0);

  if (min > 0) {
    for (int i=0; i<=min; i++) {
      // build the arguments to the function
      std::vector<Value*> args;
      args.push_back(string);
      args.push_back(loadOffset(block));
      args.push_back(end_offset);
      args.push_back(groups);
      Value* repeat_result = CallInst::Create(repeated->function, 
          args.begin(), args.end(), "repeat_result", block);
      Value* repeat_result_not_found = new ICmpInst(ICmpInst::ICMP_EQ,
          repeat_result, regex.not_found, "repeat_result_not_found", block);
      BasicBlock* next = BasicBlock::Create("repeat", function);
      BranchInst::Create(return_not_found, next, repeat_result_not_found,
          block);
      block = next;
      storeOffset(block, repeat_result);
    }
  }

  if (max > min) {
    // there are an indeterminate number of repetitions
    // time to harness the power of RECURSION!

    // compile everything after this instruction...
    CompiledRegEx* after = new CompiledRegEx(regex);
    after->Compile(seq, index+1);

    // make a function for recursion
    Function* recurse;
    if (is_greedy) {
      recurse = greedy(repeated->function, after->function);
    } else {
      recurse = nongreedy(repeated->function, after->function);
    }

    // call it
    std::vector<Value*> args;
    args.push_back(string);
    args.push_back(loadOffset(block));
    args.push_back(end_offset);
    args.push_back(groups);
    args.push_back(ConstantInt::get(OFFSET_TYPE, max-min));
    Value* recurse_result = CallInst::Create(recurse, args.begin(), args.end(),
        "recurse_result", block);
    Value* recurse_result_not_found = new ICmpInst(ICmpInst::ICMP_EQ,
        recurse_result, regex.not_found, "recurse_result_not_found", block);

    BasicBlock* return_recurse_result = 
      BasicBlock::Create("return_recurse_result", function);
    ReturnInst::Create(recurse_result, return_recurse_result);

    BasicBlock* call_after = BasicBlock::Create("call_after", function);
    args.resize(4); // use the same args as recurse without the countdown
    ReturnInst::Create(CallInst::Create(after->function, args.begin(),
          args.end(), "after_result", call_after), call_after);

    BranchInst::Create(call_after, return_recurse_result,
        recurse_result_not_found, block);

    return NULL;
  }

  return block;
}

BasicBlock*
CompiledRegEx::subpattern_begin(BasicBlock* block, PyObject* arg) {
  // the argument should just be an integer
  if (!PyInt_Check(arg)) {
    _PyErr_SetString(PyExc_TypeError, "Expected an integer");
    return NULL;
  }

  // get the group id
  long id = PyInt_AsLong(arg);

  // make sure the group ID isn't larger than we expect
  if (id > regex.groups) {
    _PyErr_SetString(PyExc_ValueError, "Unexpected group id");
    return NULL;
  }

  // get the current offset
  Value* off = loadOffset(block);
  // store the start location
  Value* start_ptr = GetElementPtrInst::Create(groups, 
      ConstantInt::get(OFFSET_TYPE, (id-1)*2), "start_ptr", block);
  new StoreInst(off, start_ptr, block);

  return block;
}

BasicBlock*
CompiledRegEx::subpattern_end(BasicBlock* block, PyObject* arg) {
  // the argument should just be an integer
  if (!PyInt_Check(arg)) {
    _PyErr_SetString(PyExc_TypeError, "Expected an integer");
    return NULL;
  }

  // get the group id
  long id = PyInt_AsLong(arg);

  // make sure the group ID isn't larger than we expect
  if (id > regex.groups) {
    _PyErr_SetString(PyExc_ValueError, "Unexpected group id");
    return NULL;
  }

  // get the current offset
  Value* off = loadOffset(block);
  // store the end location
  Value* end_ptr = GetElementPtrInst::Create(groups, 
      ConstantInt::get(OFFSET_TYPE, (id-1)*2+1), "end_ptr", block);
  new StoreInst(off, end_ptr, block);

  return block;
}

BasicBlock*
CompiledRegEx::groupref_exists(BasicBlock* block, PyObject* arg) {
  // @arg should be a tuple of (group-number, yes-seq, no-seq)
  int groupnum;
  PyObject* yes_seq;
  PyObject* no_seq;
  if (!PyArg_ParseTuple(arg, "iOO", &groupnum, &yes_seq, &no_seq)) {
    _PyErr_SetString(PyExc_ValueError, "Expected a 3-tuple");
  }

  if (!PySequence_Check(yes_seq)) {
    _PyErr_SetString(PyExc_ValueError, "Expected a sequence");
  }

  if (no_seq != Py_None && !PySequence_Check(no_seq)) {
    _PyErr_SetString(PyExc_ValueError, "Expected a sequence or None");
  }

  // create the blocks we need
  BasicBlock* yes = BasicBlock::Create("yes", function);
  BasicBlock* no = BasicBlock::Create("no", function);
  BasicBlock* next_block = BasicBlock::Create("block", function);

  // prepare the arguments for the sub-expressions
  std::vector<Value*> args;
  args.push_back(string);
  args.push_back(loadOffset(block));
  args.push_back(end_offset);
  args.push_back(groups);

  // compile the yes seq to a function
  CompiledRegEx yes_compiled(regex);
  yes_compiled.Compile(yes_seq, 0);
  // call it from the yes block
  Value* yes_result = CallInst::Create(yes_compiled.function, 
      args.begin(), args.end(), "yes_result", yes);
  // save the result
  storeOffset(yes, yes_result);
  // continue...
  BranchInst::Create(next_block, yes);

  // if there's a no function...
  if (no_seq != Py_None) {
    CompiledRegEx no_compiled(regex);
    no_compiled.Compile(no_seq, 0);
    // call it from the no block
    Value* no_result = CallInst::Create(no_compiled.function, 
        args.begin(), args.end(), "no_result", no);
    // save the result
    storeOffset(no, no_result);
  }
  // continue...
  BranchInst::Create(next_block, no);

  // generate code to check if the specified group has matched
  // get the pointer to the end offset of the group
  Value* end_ptr = GetElementPtrInst::Create(groups, 
      ConstantInt::get(OFFSET_TYPE, (groupnum-1)*2+1), "end_ptr", block);
  // load the end value
  Value* end_off = new LoadInst(end_ptr, "end", block);
  // check if the end_off is not found
  Value* end_not_found = new ICmpInst(ICmpInst::ICMP_EQ, end_off,
      regex.not_found, "end_not_found", block);
  // either yes or no
  BranchInst::Create(no, yes, end_not_found, block);

  return next_block;
}

static PyObject *
RegEx_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  RegEx *self;

  self = (RegEx *)type->tp_alloc(type, 0);
  if (self != NULL) {
    // create a module for this pattern
    self->module = new Module("LlvmRe", llvm::getGlobalContext());

    // set up some handy constants
    self->not_found = ConstantInt::getSigned(OFFSET_TYPE, -1);
  }

  return (PyObject *)self;
}


static int
RegEx_init(RegEx *self, PyObject *args, PyObject *kwds)
{
  PyObject *seq=NULL;
  int flags, groups;

  static char *kwlist[] = {"seq", "flags", "groups", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "Oii", kwlist, 
        &seq, &flags, &groups)) {
    return -1; 
  }
  
  // make sure we got a sequence
  if (!PySequence_Check(seq)) {
    _PyErr_SetString(PyExc_TypeError, "Expected a sequence");
    return -1;
  }

  self->flags = flags;
  self->groups = groups;
  
  // compile that sequence into LLVM bytecode
  self->compiled = new CompiledRegEx(*self, true);

  Py_INCREF(seq);
  if (!self->compiled->Compile(seq, 0)) {
    delete self->compiled;
    self->compiled = NULL;
    Py_DECREF(seq);
    return -1;
  }
  Py_DECREF(seq);

  // compile a new function to find rather than match
 
  // function argument types
  std::vector<const Type*> args_type;
  args_type.push_back(CHAR_POINTER_TYPE); // string
  args_type.push_back(OFFSET_TYPE); // offset
  args_type.push_back(OFFSET_TYPE); // end_offset
  args_type.push_back(OFFSET_POINTER_TYPE); // groups
  args_type.push_back(OFFSET_POINTER_TYPE); // start_ptr
  FunctionType *func_type = FunctionType::get(OFFSET_TYPE, args_type, false);

  // FIXME: choose better function flags
  // create the find function
  self->find = Function::Create(func_type, Function::ExternalLinkage, 
      "find", self->module);
  // get and name the arguments
  Function::arg_iterator func_args = self->find->arg_begin();
  Value* string = func_args++;
  string->setName("string");
  Value* offset = func_args++;
  offset->setName("offset");
  Value* end_offset = func_args++;
  end_offset->setName("end_offset");
  Value* groups_arg = func_args++;
  groups_arg->setName("groups");
  Value* start_ptr = func_args++;
  start_ptr->setName("start_ptr");

  // create basic blocks
  BasicBlock* entry = BasicBlock::Create("entry", self->find);
  BasicBlock* test_offset = BasicBlock::Create("test_offset", self->find);
  BasicBlock* match = BasicBlock::Create("match", self->find);
  BasicBlock* increment = BasicBlock::Create("increment", self->find);
  BasicBlock* return_not_found = BasicBlock::Create("return_not_found", self->find);
  BasicBlock* return_match_result = BasicBlock::Create("return_match_result", self->find);

  // create the entry BasicBlock
  Value* offset_ptr = new llvm::AllocaInst(OFFSET_TYPE, "offset_ptr", entry);
  new StoreInst(offset, offset_ptr, entry);
  BranchInst::Create(test_offset, entry);

  // create the test_offset BasicBlock
  // get the current offset
  offset = new LoadInst(offset_ptr, "offset", test_offset);
  // make sure it's not past the end of the string
  Value* ended = new ICmpInst(ICmpInst::ICMP_UGT, offset, end_offset, "ended",
      test_offset);
  BranchInst::Create(return_not_found, match, ended, test_offset);

  // create the match BasicBlock
  // call the recently compiled match function
  std::vector<Value*> call_args;
  call_args.push_back(string);
  call_args.push_back(offset);
  call_args.push_back(end_offset);
  call_args.push_back(groups_arg);
  Value* match_result = CallInst::Create(self->compiled->function,
      call_args.begin(), call_args.end(), "match_result", match);
  Value* match_result_not_found = new ICmpInst(ICmpInst::ICMP_EQ, 
      match_result, self->not_found, "match_result_not_found", match);
  BranchInst::Create(increment, return_match_result, match_result_not_found, 
      match);

  // create the increment BasicBlock
  offset = BinaryOperator::CreateAdd(offset, 
      ConstantInt::get(OFFSET_TYPE, 1), "increment", increment);
  new StoreInst(offset, offset_ptr, increment);
  BranchInst::Create(test_offset, increment);

  // create the return_not_found BasicBlock
  ReturnInst::Create(self->not_found, return_not_found);

  // create the return_match_result BasicBlock
  // put the offset in our outparam
  new StoreInst(new LoadInst(offset_ptr, "offset", return_match_result),
      start_ptr, return_match_result);
  ReturnInst::Create(match_result, return_match_result);

  return 0;
}

static PyObject*
RegEx_dump(RegEx* self) {
  self->module->dump();
  Py_INCREF(Py_None);
  return Py_None;
}

static PyObject*
RegEx_match(RegEx* self, PyObject* args) {
  Py_UNICODE* characters;
  int length, pos, end;
  if (!PyArg_ParseTuple(args, "u#ii", &characters, &length, &pos, &end)) {
    return NULL;
  }

  PyGlobalLlvmData *global_llvm_data = 
    PyThreadState_GET()->interp->global_llvm_data;
  ExecutionEngine *engine = global_llvm_data->getExecutionEngine();

  MatchFunction func_ptr = (MatchFunction)
    engine->getPointerToFunction(self->compiled->function);

  ReOffset* groups = NULL;
  if (self->groups) {
    groups = new ReOffset[self->groups*2];
  }

  ReOffset result = (func_ptr)(characters, pos, end, groups);

  if (result >= 0) {
    // matched, return a list of offsets
    PyObject* groups_list = PyList_New(self->groups*2 + 2);
    // r[0] - the start of the whole match
    PyList_SetItem(groups_list, 0, PyInt_FromLong((long)pos));
    // r[0] - the end of the whole match
    PyList_SetItem(groups_list, 1, PyInt_FromLong((long)result));
    // for each group, the start and end offsets
    if (self->groups) {
      for (int i=0; i<self->groups*2; i++) {
        PyList_SetItem(groups_list, i+2, PyInt_FromLong((long)groups[i]));
      }
      delete[] groups;
    }

    Py_INCREF(groups_list);

    return groups_list;
  } else {
    // no match, return None
    Py_INCREF(Py_None);
    return Py_None;
  }
}


static PyObject*
RegEx_find(RegEx* self, PyObject* args) {
  Py_UNICODE* characters;
  int length, pos, end;
  if (!PyArg_ParseTuple(args, "u#ii", &characters, &length, &pos, &end)) {
    return NULL;
  }

  PyGlobalLlvmData *global_llvm_data = 
    PyThreadState_GET()->interp->global_llvm_data;
  ExecutionEngine *engine = global_llvm_data->getExecutionEngine();

  FindFunction func_ptr = (FindFunction)
    engine->getPointerToFunction(self->find);

  ReOffset* groups = NULL;
  if (self->groups) {
    groups = new ReOffset[self->groups*2];
  }

  ReOffset start;

  ReOffset result = (func_ptr)(characters, pos, end, groups, &start);

  if (result >= 0) {
    // matched, return a list of offsets
    PyObject* groups_list = PyList_New(self->groups*2 + 2);
    // r[0] - the start of the whole match
    PyList_SetItem(groups_list, 0, PyInt_FromLong((long)start));
    // r[0] - the end of the whole match
    PyList_SetItem(groups_list, 1, PyInt_FromLong((long)result));
    // for each group, the start and end offsets
    if (self->groups) {
      for (int i=0; i<self->groups*2; i++) {
        PyList_SetItem(groups_list, i+2, PyInt_FromLong((long)groups[i]));
      }
      delete[] groups;
    }

    Py_INCREF(groups_list);

    return groups_list;
  } else {
    // no match, return None
    Py_INCREF(Py_None);
    return Py_None;
  }
}


static PyMethodDef RegEx_methods[] = {
  {"dump", (PyCFunction)RegEx_dump, METH_NOARGS,
   "Dump the LLVM code for the RegEx", },
  {"match", (PyCFunction)RegEx_match, METH_VARARGS,
   "Match the pattern against the start of a string", },
  {"find", (PyCFunction)RegEx_find, METH_VARARGS,
   "Find the pattern in a string", },
  {NULL}  /* Sentinel */
};
  
static PyTypeObject RegExType = {
  PyObject_HEAD_INIT(NULL)
  0,                         /*ob_size*/
  "llvmre.RegEx",            /*tp_name*/
  sizeof(RegEx),             /*tp_basicsize*/
  0,                         /*tp_itemsize*/
  0,                         /*tp_dealloc*/
  0,                         /*tp_print*/
  0,                         /*tp_getattr*/
  0,                         /*tp_setattr*/
  0,                         /*tp_compare*/
  0,                         /*tp_repr*/
  0,                         /*tp_as_number*/
  0,                         /*tp_as_sequence*/
  0,                         /*tp_as_mapping*/
  0,                         /*tp_hash */
  0,                         /*tp_call*/
  0,                         /*tp_str*/
  0,                         /*tp_getattro*/
  0,                         /*tp_setattro*/
  0,                         /*tp_as_buffer*/
  Py_TPFLAGS_DEFAULT| Py_TPFLAGS_BASETYPE, /*tp_flags*/
  "RegEx objects",           /* tp_doc */
  0,                         /* tp_traverse */
  0,                         /* tp_clear */
  0,                         /* tp_richcompare */
  0,                         /* tp_weaklistoffset */
  0,                         /* tp_iter */
  0,                         /* tp_iternext */
  RegEx_methods,             /* tp_methods */
  0/*RegEx_members*/,             /* tp_members */
  0,                         /* tp_getset */
  0,                         /* tp_base */
  0,                         /* tp_dict */
  0,                         /* tp_descr_get */
  0,                         /* tp_descr_set */
  0,                         /* tp_dictoffset */
  (initproc)RegEx_init,      /* tp_init */
  0,                         /* tp_alloc */
  RegEx_new,                 /* tp_new */
};

static PyMethodDef llvmre_methods[] = {
  {NULL}  /* Sentinel */
};

#ifndef PyMODINIT_FUNC  /* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC
init_llvmre(void) 
{
  PyObject* m;

  if (PyType_Ready(&RegExType) < 0)
    return;

  m = Py_InitModule3("_llvmre", llvmre_methods,
             "JIT Python regular expressions using LLVM");

  Py_INCREF(&RegExType);
  PyModule_AddObject(m, "RegEx", (PyObject *)&RegExType);
}
