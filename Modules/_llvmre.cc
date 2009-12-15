/* _llvmre.cpp */

#include "Python.h"
#include "_llvmfunctionobject.h"
#include "llvm_compile.h"
#include "Python/global_llvm_data.h"
#include "Util/PyTypeBuilder.h"

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
#include "llvm/LLVMContext.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/Casting.h"

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
using llvm::AllocaInst;
using llvm::LoadInst;
using llvm::StoreInst;
using llvm::ICmpInst;
using llvm::ZExtInst;
using llvm::SExtInst;

using llvm::ExecutionEngine;
using llvm::EngineBuilder;

using llvm::LLVMContext;
using llvm::getGlobalContext;

using llvm::cast;
using llvm::dyn_cast;
using llvm::inst_begin;
using llvm::inst_end;
using llvm::inst_iterator;

using llvm::SmallPtrSet;

// helper to produce better error messages
#define _PyErr_SetString(T,S) \
  PyErr_Format(T, S " (in %s at %s:%d)",__PRETTY_FUNCTION__ , __FILE__, __LINE__)


typedef int32_t ReOffset;
typedef ReOffset (*MatchFunction)(Py_UNICODE*, ReOffset, ReOffset, ReOffset*);
typedef ReOffset (*FindFunction)(Py_UNICODE*, ReOffset, ReOffset, ReOffset*, ReOffset*);

// forward declarations
class RegularExpression;
class CompiledExpression;


#ifndef TESTER
/* a Python object representing a regular expression */
typedef struct {
  PyObject_HEAD

  /* the root compiled regular expression */
  RegularExpression* re;

} RegEx;
#endif /* TESTER */

/* a singleton object representing global state, reusable values and
 * functions that don't belong anywhere else */
class RegularExpressionModule {
  public:
    // values
    Value* not_found;

    // types
    const IntegerType* charType;
    const IntegerType* boolType;
    const IntegerType* offsetType;
    const PointerType* charPointerType;
    const PointerType* offsetPointerType;

    RegularExpressionModule() {
      // get Unladen Swallow's LLVM context
      LLVMContext *context = &PyGlobalLlvmData::Get()->context();

      // initialize types and values that are used later all over the place
      charType = PyTypeBuilder<Py_UNICODE>::get(*context);
      boolType = PyTypeBuilder<bool>::get(*context);
      offsetType = PyTypeBuilder<int>::get(*context);
      charPointerType = PyTypeBuilder<Py_UNICODE*>::get(*context);
      offsetPointerType = PyTypeBuilder<int*>::get(*context);
      // set up some handy constants
      not_found = ConstantInt::getSigned(offsetType, -1);
    }
    ~RegularExpressionModule() {
      // I don't think we can clean up those values safely :(
    }

  private:
    typedef SmallPtrSet<Function*, 16> DumpedFunctionSet;

    void dump(Function* function, DumpedFunctionSet& dumped) {
      function->dump();
      dumped.insert(function);

      // iterate the function's instructions, looking for a call
      for (inst_iterator i = inst_begin(function), e = inst_end(function); 
          i != e; i++) { 
        if (CallInst* inst = dyn_cast<CallInst>(&*i)) {
          // found a call, what's being called?
          Function* called = inst->getCalledFunction();
          if (called && !dumped.count(called)) {
            // it's never dumped before, dump it.
            dump(called, dumped);
          }
        }
      }
    }
  public:
    // dump a function and everything it calls
    void dump(Function* function) {
      DumpedFunctionSet dumped;
      dump(function, dumped);
    }

    // optimize an LLVM function
    void optimize(Function* function) {
      // FIXME: we need to implement cross-function optimization,
      // like inlining.

      // FIXME: chose re-specific optimizations
      PyGlobalLlvmData::Get()->Optimize(*function, 2);
    }

};
static RegularExpressionModule* REM = NULL;

/* a compiled regular expression atom */
class CompiledExpression {
  public:
    CompiledExpression(RegularExpression& re, bool first=false);
    virtual ~CompiledExpression();

    // compile the result of sre_parse.parse
    bool Compile(PyObject* seq, Py_ssize_t index, bool subpattern=true);

    // this holds useful re-wide state
    RegularExpression& re;
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
    BasicBlock* groupref(BasicBlock* block, PyObject* arg);
    BasicBlock* groupref_exists(BasicBlock* block, PyObject* arg);
    BasicBlock* assert_(BasicBlock* block, PyObject* arg, bool assert_not);
    BasicBlock* at_end(BasicBlock* block);
    BasicBlock* at_end_string(BasicBlock* block);
    BasicBlock* at_beginning(BasicBlock* block);
    BasicBlock* at_beginning_string(BasicBlock* block);
    BasicBlock* at_boundary(BasicBlock* block, bool non_boundary);

    // helpers to generate commonly used code
    Value* loadOffset(BasicBlock* block);
    void storeOffset(BasicBlock* block, Value* value);
    BasicBlock* loadCharacter(BasicBlock* block);
    Function* greedy(Function* repeat, Function* after);
    Function* nongreedy(Function* repeat, Function* after);
    void testRange(BasicBlock* block, Value* c, int from, int to, BasicBlock* member, BasicBlock* nonmember);
    bool testCategory(BasicBlock* block, Value* c, const char* category, BasicBlock* member, BasicBlock* nonmember);

    BasicBlock* createBlock(const char* name="block", Function* func=NULL) {
      return BasicBlock::Create(context(), name, func?func:function);
    }

    inline LLVMContext& context();

    template<typename R, typename A1, bool isSigned, bool toBool>
    inline Value* callGlobalFunction(const char* name, Value* argument, 
        BasicBlock* block);
};


/* a regular expression */
class RegularExpression : CompiledExpression {
  public:
    RegularExpression();
    virtual ~RegularExpression();

    bool Compile(PyObject* seq, int flags, int groups);

    PyObject* Match(Py_UNICODE* characters, int length, int pos, int end);
    PyObject* Find(Py_UNICODE* characters, int length, int pos, int end);

    // Unladed Swallow global LLVM data
    PyGlobalLlvmData* global_data;
    // LLVM Context
    static LLVMContext* context;
    // LLVM module
    Module* module;
    // the execution engine
    static ExecutionEngine* ee;

    int flags;
    int groups;

    // create an LLVM function associated with this re
    inline Function* createFunction(const char* name, 
                                    bool internal, 
                                    const Type* extra_arg_type = NULL);

    // the find function
    Function* find_function;
  private:
    bool CompileFind();
    ReOffset* AllocateGroupsArray();
    PyObject* ProcessResult(ReOffset start,
                            ReOffset result,
                            ReOffset* groups_array);
    // the function pointers
    MatchFunction match_fp;
    FindFunction find_fp;

    // all of the functions created by this regex
    typedef std::vector<Function*> Functions;
    Functions functions;

};

LLVMContext* RegularExpression::context = NULL;
ExecutionEngine* RegularExpression::ee = NULL;

RegularExpression::RegularExpression() 
  : CompiledExpression(*this, true), find_function(NULL)
{
  // use the Unladen Swallow LLVM context
  global_data = PyGlobalLlvmData::Get();

  if (context == NULL) {
    //context = new LLVMContext();
    context = &global_data->context();
  }
 
  // create a module for this pattern
  //module = new Module("LlvmRe", *context);
  module = global_data->module();

  // get an execution engine
  if (ee == NULL) {
    //ee = EngineBuilder(module).create();
    ee = global_data->getExecutionEngine();
  }
}

RegularExpression::~RegularExpression() 
{
  // first free all of the JIT state associated with this expression
  for (Functions::reverse_iterator i = functions.rbegin(); 
      i < functions.rend(); ++i) {
    ee->freeMachineCodeForFunction(*i);
  }
  match_fp = NULL;
  find_fp = NULL;

  // free the functions associated with this regex
  bool made_changes;
  int passes = 0;
  int remaining = 0;
  do {
    made_changes = false;
    remaining = 0;
    for (Functions::reverse_iterator i = functions.rbegin(); 
        i < functions.rend(); ++i) {
      Function* f = *i;
      if (f == NULL) continue; // skip already freed functions
      if (f->use_empty()) {
        f->eraseFromParent();
        *i = NULL;
        made_changes = true;
      } else {
        remaining++;
      }
    }
    passes++;
  } while (made_changes);
}

Function* 
RegularExpression::createFunction(const char* name, 
                                  bool internal, 
                                  const Type* extra_arg_type)
{
  // function argument types
  std::vector<const Type*> args_type;
  args_type.push_back(REM->charPointerType); // string
  args_type.push_back(REM->offsetType); // offset
  args_type.push_back(REM->offsetType); // end_offset
  args_type.push_back(REM->offsetPointerType); // groups
  if (extra_arg_type != NULL) {
    args_type.push_back(extra_arg_type); // start_ptr / counter
  }
  FunctionType *func_type = FunctionType::get(REM->offsetType, args_type, false);

  Function* func = Function::Create(func_type, 
      internal ? Function::InternalLinkage : Function::ExternalLinkage,
      name, re.module);

  functions.push_back(func);

  return func;
}

bool 
RegularExpression::Compile(PyObject* seq, 
                           int flags, 
                           int groups) 
{
  this->flags = flags;
  this->groups = groups;

  if (CompiledExpression::Compile(seq, 0, false) && CompileFind()) {
    match_fp = (MatchFunction) ee->getPointerToFunction(function);
    find_fp = (FindFunction) ee->getPointerToFunction(find_function);
    return true;
  } else {
    return false;
  }
}

bool
RegularExpression::CompileFind() 
{
    // create the find function
  find_function = createFunction("find", false, REM->offsetPointerType);
  // get and name the arguments
  Function::arg_iterator func_args = find_function->arg_begin();
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
  BasicBlock* entry = BasicBlock::Create(*context, 
      "entry", find_function);
  BasicBlock* test_offset = BasicBlock::Create(*context, 
      "test_offset", find_function);
  BasicBlock* match = BasicBlock::Create(*context, 
      "match", find_function);
  BasicBlock* increment = BasicBlock::Create(*context, 
      "increment", find_function);
  BasicBlock* return_not_found = BasicBlock::Create(*context, 
      "return_not_found", find_function);
  BasicBlock* return_match_result = BasicBlock::Create(*context, 
      "return_match_result", find_function);

  // create the entry BasicBlock
  Value* offset_ptr = new AllocaInst(REM->offsetType, "offset_ptr", entry);
  new StoreInst(offset, offset_ptr, entry);
  BranchInst::Create(test_offset, entry);

  // create the test_offset BasicBlock
  // get the current offset
  offset = new LoadInst(offset_ptr, "offset", test_offset);
  // make sure it's not past the end of the string
  Value* ended = new ICmpInst(*test_offset, ICmpInst::ICMP_UGT, offset, 
      end_offset, "ended");
  BranchInst::Create(return_not_found, match, ended, test_offset);

  // create the match BasicBlock
  // call the recently compiled match function
  std::vector<Value*> call_args;
  call_args.push_back(string);
  call_args.push_back(offset);
  call_args.push_back(end_offset);
  call_args.push_back(groups_arg);
  Value* match_result = CallInst::Create(function,
      call_args.begin(), call_args.end(), "match_result", match);
  Value* match_result_not_found = new ICmpInst(*match, ICmpInst::ICMP_EQ, 
      match_result, REM->not_found, "match_result_not_found");
  BranchInst::Create(increment, return_match_result, match_result_not_found, 
      match);

  // create the increment BasicBlock
  offset = BinaryOperator::CreateAdd(offset, 
      ConstantInt::get(REM->offsetType, 1), "increment", increment);
  new StoreInst(offset, offset_ptr, increment);
  BranchInst::Create(test_offset, increment);

  // create the return_not_found BasicBlock
  ReturnInst::Create(*context, REM->not_found, return_not_found);

  // create the return_match_result BasicBlock
  // put the offset in our outparam
  new StoreInst(new LoadInst(offset_ptr, "offset", return_match_result),
      start_ptr, return_match_result);
  ReturnInst::Create(*context, match_result, return_match_result);

  REM->optimize(find_function);
  return true;
}

ReOffset*
RegularExpression::AllocateGroupsArray()
{
  if (groups) {
    int groups_size = groups*2 + 1;
    // allocate the array
    ReOffset* groups_array = new ReOffset[groups_size];
    // set all members to -1 (aka REM->not_found)
    for (int i=0; i<groups_size; i++) {
      groups_array[i] = -1;
    }
    return groups_array;
  } else {
    // no array is needed if there are no groups, right?
    return NULL;
  }
}

PyObject*
RegularExpression::ProcessResult(ReOffset start,
                                 ReOffset result,
                                 ReOffset* groups_array)
{
  if (result >= 0) {
    // matched, return a list of offsets
    PyObject* groups_list = PyList_New(groups*2 + 3);
    // r[0] - the start of the whole match
    PyList_SetItem(groups_list, 0, PyInt_FromLong((long)start));
    // r[0] - the end of the whole match
    PyList_SetItem(groups_list, 1, PyInt_FromLong((long)result));
    // for each group, the start and end offsets
    if (groups) {
      for (int i=0; i<groups*2 + 1; i++) {
        PyList_SetItem(groups_list, i+2, 
            PyInt_FromLong((long)groups_array[i]));
      }
    } else {
      // if there were no groups still stick a lastindex in there
      PyList_SetItem(groups_list, 2, PyInt_FromLong(-1L));
    }

    // FIXME: does this leak?
    Py_INCREF(groups_list);

    if (groups) {
      delete[] groups_array;
    }
    
    return groups_list;
  } else {
    if (groups) {
      delete[] groups_array;
    }
    
    // no match, return None
    Py_INCREF(Py_None);
    return Py_None;
  }

}

PyObject*
RegularExpression::Match(Py_UNICODE* characters, 
                         int length, 
                         int pos, 
                         int end)
{
  ReOffset* groups_array = AllocateGroupsArray();

  ReOffset result = (match_fp)(characters, pos, end, groups_array);

  return ProcessResult(pos, result, groups_array);
}

PyObject*
RegularExpression::Find(Py_UNICODE* characters, 
                        int length, 
                        int pos, 
                        int end)
{
  ReOffset start;
  ReOffset* groups_array = AllocateGroupsArray();
  ReOffset result = (find_fp)(characters, pos, end, groups_array, &start);
  return ProcessResult(start, result, groups_array);
}


CompiledExpression::CompiledExpression(RegularExpression& re, bool first) 
  : re(re), first(first) {
}

CompiledExpression::~CompiledExpression() {
}


LLVMContext& 
CompiledExpression::context() 
{
  return *re.context;
}

Value* 
CompiledExpression::loadOffset(BasicBlock* block) {
  return new LoadInst(offset_ptr, "offset", block);
}

void
CompiledExpression::storeOffset(BasicBlock* block, Value* value) {
  new StoreInst(value, offset_ptr, block);
}

BasicBlock* 
CompiledExpression::loadCharacter(BasicBlock* block) {
  // get the current offset
  Value* offset = loadOffset(block);
  // make sure it's not past the end of the string
  Value* ended = new ICmpInst(*block, ICmpInst::ICMP_UGE, offset, end_offset, 
      "ended");
  // if it's ended, return REM->not_found, otherwise, continue
  // I'm scared this will fuck things up
  BasicBlock* new_block = createBlock();
  BranchInst::Create(return_not_found, new_block, ended, block);

  block = new_block;

  // load the character at the right offset
  Value* c_ptr = GetElementPtrInst::Create(string, offset, "c_ptr", block);
  character = new LoadInst(c_ptr, "c", block);
  // increment the offset
  offset = BinaryOperator::CreateAdd(offset,
      ConstantInt::get(REM->offsetType, 1), "increment", block);
  storeOffset(block, offset);
  return block;
}

template<typename R, typename A1, bool isSigned, bool toBool>
Value* 
CompiledExpression::callGlobalFunction(const char* name, 
                                       Value*      a1, 
                                       BasicBlock* block) {
  // get the Function* for the global function
  Function* gf = cast<Function>(
      re.module->getOrInsertFunction(name,
        PyTypeBuilder<R(A1)>::get(context()))
      );

  // get the llvm::Type of the argument
  const Type* A1_type = PyTypeBuilder<A1>::get(context());

  // convert the argument to the correct type using correct sign semantics
  // or leave it if the types already match
  Value* args[1];
  if (A1_type == a1->getType()) {
    args[0] = a1;
  } else if (isSigned) {
    args[0] = 
      new SExtInst(a1, A1_type, "", block);
  } else {
    args[0] = 
      new ZExtInst(a1, A1_type, "", block);
  }
  // call the function
  Value* result = CallInst::Create(gf, args, args+1, "result", block);

  if (toBool) {
    // cast an integer result to boolean. we do this often
    return new ICmpInst(*block, ICmpInst::ICMP_NE,
      result, ConstantInt::get(PyTypeBuilder<R>::get(context()), 0),
      "result_bool");
  } else {
    return result;
  }
}


Function*
CompiledExpression::greedy(Function* repeat, Function* after) 
{
  // create the function
  Function* function = re.createFunction("recurse", true, REM->offsetType);

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
  BasicBlock* call_repeat = createBlock("call_repeat", function);
  BasicBlock* count = createBlock("count", function);
  BasicBlock* recurse = createBlock("recurse", function);
  BasicBlock* call_after = createBlock("call_after", function);
  BasicBlock* return_offset = createBlock("return_offset", function);

  // set up BasicBlock to return REM->not_found
  BasicBlock* return_not_found = createBlock("return_not_found", function);
  ReturnInst::Create(context(), REM->not_found, return_not_found);

  // call repeat
  std::vector<Value*> call_args;
  call_args.push_back(string);
  call_args.push_back(offset);
  call_args.push_back(end_offset);
  call_args.push_back(groups);
  Value* repeat_result = CallInst::Create(repeat, call_args.begin(),
      call_args.end(), "repeat_result", call_repeat);
  Value* repeat_result_not_found = new ICmpInst(*call_repeat, ICmpInst::ICMP_EQ,
      repeat_result, REM->not_found, "repeat_result_not_found");
  BranchInst::Create(return_not_found, count, repeat_result_not_found, 
      call_repeat);

  // count
  Value* remaining = BinaryOperator::CreateSub(countdown,
      ConstantInt::get(REM->offsetType, 1), "remaining", count);
  Value* stop_recursion = new ICmpInst(*count, ICmpInst::ICMP_EQ, remaining,
      ConstantInt::get(REM->offsetType, 0), "stop_recursion");
  BranchInst::Create(call_after, recurse, stop_recursion, count);

  // recurse
  call_args[1] = repeat_result;
  call_args.push_back(remaining);
  Value* recurse_result = CallInst::Create(function, call_args.begin(),
      call_args.end(), "recurse_result", recurse);
  Value* recurse_result_not_found = new ICmpInst(*recurse, ICmpInst::ICMP_EQ,
      recurse_result, REM->not_found, "recurse_result_not_found");
  BranchInst::Create(call_after, return_offset, recurse_result_not_found, 
      recurse);

  // after
  call_args.resize(4);
  Value* after_result = CallInst::Create(after, call_args.begin(), 
      call_args.end(), "after_result", call_after);
  ReturnInst::Create(context(), after_result, call_after);

  // return_offset
  ReturnInst::Create(context(), recurse_result, return_offset);

  REM->optimize(function);

  return function; 
}

Function*
CompiledExpression::nongreedy(Function* repeat, Function* after) 
{
  // create the function
  Function* function = re.createFunction("recurse", true, REM->offsetType);

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
  BasicBlock* call_after = createBlock("call_after", function);
  BasicBlock* call_repeat = createBlock("call_repeat", function);
  BasicBlock* count = createBlock("count", function);
  BasicBlock* recurse = createBlock("recurse", function);
  BasicBlock* return_after_result = createBlock("return_after_result", function);

  // set up BasicBlock to return REM->not_found
  BasicBlock* return_not_found = createBlock("return_not_found", function);
  ReturnInst::Create(context(), REM->not_found, return_not_found);

  // call after
  std::vector<Value*> call_args;
  call_args.push_back(string);
  call_args.push_back(offset);
  call_args.push_back(end_offset);
  call_args.push_back(groups);
  Value* after_result = CallInst::Create(after, call_args.begin(),
      call_args.end(), "after_result", call_after);
  Value* after_result_not_found = new ICmpInst(*call_after, ICmpInst::ICMP_EQ,
      after_result, REM->not_found, "after_result_not_found");
  BranchInst::Create(call_repeat, return_after_result, after_result_not_found,
      call_after);

  // call repeat
  Value* repeat_result = CallInst::Create(repeat, call_args.begin(),
      call_args.end(), "repeat_result", call_repeat);
  Value* repeat_result_not_found = new ICmpInst(*call_repeat, ICmpInst::ICMP_EQ,
      repeat_result, REM->not_found, "repeat_result_not_found");
  BranchInst::Create(return_not_found, count, repeat_result_not_found, 
      call_repeat);

  // count
  Value* remaining = BinaryOperator::CreateSub(countdown,
      ConstantInt::get(REM->offsetType, 1), "remaining", count);
  Value* stop_recursion = new ICmpInst(*count, ICmpInst::ICMP_EQ, remaining,
      ConstantInt::get(REM->offsetType, 0), "stop_recursion");
  BranchInst::Create(return_not_found, recurse, stop_recursion, count);

  // recurse
  call_args[1] = repeat_result;
  call_args.push_back(remaining);
  Value* recurse_result = CallInst::Create(function, call_args.begin(),
      call_args.end(), "recurse_result", recurse);
  ReturnInst::Create(context(), recurse_result, recurse);

  // return_after_result
  ReturnInst::Create(context(), after_result, return_after_result);

  REM->optimize(function);

  return function; 
}

void
CompiledExpression::testRange(BasicBlock* block,
                         Value*      c,
                         int         from,
                         int         to,
                         BasicBlock* member, 
                         BasicBlock* nonmember) 
{
  /** in @block, test if the current character (@c) is in the range
   * @from to @to inclusive. If so jump to @member, else jump to @nonmember */

  // create a couple of new basic blocks for the range test
  BasicBlock* greater_equal = createBlock("greater_equal");
  // test the character >= from
  Value* is_ge = new llvm::ICmpInst(*block, llvm::ICmpInst::ICMP_UGE, c,
      ConstantInt::get(REM->charType, from), "is_ge");
  BranchInst::Create(greater_equal, nonmember, is_ge, block);
  // test the character <= to
  Value* is_le = new llvm::ICmpInst(*greater_equal, llvm::ICmpInst::ICMP_ULE, 
      c, ConstantInt::get(REM->charType, to), "is_le");
  BranchInst::Create(member, nonmember, is_le, greater_equal);
}

bool
CompiledExpression::testCategory(BasicBlock* block,
                            Value* c,
                            const char* category, 
                            BasicBlock* member, 
                            BasicBlock* nonmember) 
{
  /** in @block, test if the character (@c) is a member 
   *  of @category and branch to @member or @nonmember as appropriate */
  if (!strcmp(category, "category_digit")) {
    if (re.flags & SRE_FLAG_UNICODE) {
      // for unicode we call Py_UNICODE_ISDIGIT
      // call the function
      Value* result = callGlobalFunction<int, Py_UNICODE, false, true>(
          "_PyLlvm_UNICODE_ISDIGIT", c, block);
      // go to the right successor block
      BranchInst::Create(member, nonmember, result, block);
    } else {
      // for non-unicode, test if it's in the range '0' - '9'
      testRange(block, c, '0', '9', member, nonmember);
    }
  } else if (!strcmp(category, "category_not_digit")) {
    // the opposite of the digit category
    testCategory(block, c, "category_digit", nonmember, member);
  } else if (!strcmp(category, "category_word")) {
    if (re.flags & SRE_FLAG_LOCALE) {
      // match [0-9_] and whatever system isalnum matches
      BasicBlock* tmp1 = createBlock("category_word_1");
      BasicBlock* tmp2 = createBlock("category_word_2");
      testRange(block, c, '0', '9', member, tmp1);
      Value* is_underscore = new ICmpInst(*tmp1, ICmpInst::ICMP_EQ, c,
        ConstantInt::get(REM->charType, '_'), "is_underscore");
      BranchInst::Create(member, tmp2, is_underscore, tmp1);
      // call the function
      Value* result = 
        callGlobalFunction<int,int,false,true>("isalnum", c, tmp2);
      // go to the right successor block
      BranchInst::Create(member, nonmember, result, tmp2);
    } else if (re.flags & SRE_FLAG_UNICODE) {
      // match [0-9_] and whatever Py_UNICODE_ISALNUM matches
      BasicBlock* tmp1 = createBlock("category_word_1");
      BasicBlock* tmp2 = createBlock("category_word_2");
      testRange(block, c, '0', '9', member, tmp1);
      Value* is_underscore = new ICmpInst(*tmp1, ICmpInst::ICMP_EQ, c,
        ConstantInt::get(REM->charType, '_'), "is_underscore");
      BranchInst::Create(member, tmp2, is_underscore, tmp1);
      // call the function
      Value* result =
        callGlobalFunction<int,Py_UNICODE,false,true>(
            "_PyLlvm_UNICODE_ISALNUM", c, tmp2);
      // go to the right successor block
      BranchInst::Create(member, nonmember, result, tmp2);
    } else {
      // match [a-zA-Z0-9_]
      BasicBlock* tmp1 = createBlock("category_word_1");
      BasicBlock* tmp2 = createBlock("category_word_2");
      BasicBlock* tmp3 = createBlock("category_word_3");
      testRange(block, c, 'a', 'z', member, tmp1);
      testRange(tmp1, c, 'A', 'Z', member, tmp2);
      testRange(tmp2, c, '0', '9', member, tmp3);
      Value* is_underscore = new ICmpInst(*tmp3, ICmpInst::ICMP_EQ, c,
        ConstantInt::get(REM->charType, '_'), "is_underscore");
      BranchInst::Create(member, nonmember, is_underscore, tmp3);
    }
  } else if (!strcmp(category, "category_not_word")) {
    testCategory(block, c, "category_word", nonmember, member);
  } else if (!strcmp(category, "category_space")) {
    // match [ \t\n\r\f\v]
    BasicBlock* unmatched = createBlock();
    // create a switch instruction
    SwitchInst* switch_ = SwitchInst::Create(c, unmatched, 6, block);
    switch_->addCase(ConstantInt::get(REM->charType, ' '), member);
    switch_->addCase(ConstantInt::get(REM->charType, '\t'), member);
    switch_->addCase(ConstantInt::get(REM->charType, '\n'), member);
    switch_->addCase(ConstantInt::get(REM->charType, '\r'), member);
    switch_->addCase(ConstantInt::get(REM->charType, '\f'), member);
    switch_->addCase(ConstantInt::get(REM->charType, '\v'), member);
    if (re.flags & SRE_FLAG_LOCALE) {
      // also match isspace
      Value* result = 
        callGlobalFunction<int,int,false,true>("isspace", c, unmatched);
      // go to the right successor block
      BranchInst::Create(member, nonmember, result, unmatched);
    } else if (re.flags & SRE_FLAG_UNICODE) {
      // also match Py_UNICODE_ISSPACE
      Value* result =
        callGlobalFunction<int,Py_UNICODE,false,true>(
            "_PyLlvm_UNICODE_ISSPACE", c, unmatched);
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
CompiledExpression::Compile(PyObject*  seq, 
                            Py_ssize_t index,
                            bool       subpattern)
{
  // make sure we got a sequence
  if (!PySequence_Check(seq)) {
    _PyErr_SetString(PyExc_TypeError, "Expected a sequence");
    return false;
  }

  function = re.createFunction("pattern", subpattern);

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
  entry = createBlock("entry");
  offset_ptr = new AllocaInst(REM->offsetType, "offset_ptr", entry);
  new StoreInst(offset, offset_ptr, entry);

  // create a block that returns the offset
  return_offset = createBlock("return_offset");
  Value* _offset = new LoadInst(offset_ptr, "offset", return_offset);
  ReturnInst::Create(context(), _offset, return_offset);

  // create a block that returns "not found"
  return_not_found = createBlock("return_not_found");

  // create a first basic block to be used
  BasicBlock* first = createBlock("first");

  last = first;

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
    BasicBlock* block = createBlock(op_str);
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
    } else if (!strcmp(op_str, "groupref")) {
      last = groupref(block, arg);
    } else if (!strcmp(op_str, "groupref_exists")) {
      last = groupref_exists(block, arg);
    } else if (!strcmp(op_str, "assert")) {
      last = assert_(block, arg, false);
    } else if (!strcmp(op_str, "assert_not")) {
      last = assert_(block, arg, true);
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
      } else if (!strcmp(arg_str, "at_beginning_string")) {
        last = at_beginning_string(block);
      } else if (!strcmp(arg_str, "at_end_string")) {
        last = at_end_string(block);
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
      break;
    }

    index++;
  }

  if (last != NULL) {
    // we've processed the whole pattern, if we are matching here, 
    // return success
    BranchInst::Create(return_offset, last);
  }

  // jump from the entry to the first block
  BranchInst::Create(first, entry);

  // add the return instruction to return_not_found
  ReturnInst::Create(context(), REM->not_found, return_not_found);

  // optimize the function
  REM->optimize(function);

  return true;
}

BasicBlock*
CompiledExpression::literal(BasicBlock* block, PyObject* arg, bool not_literal) {
  // the argument should just be an integer
  if (!PyInt_Check(arg)) {
    _PyErr_SetString(PyExc_TypeError, "Expected an integer");
    return NULL;
  }

  // get the character literal
  int c = PyInt_AsLong(arg);

  // get the next character from the string
  block = loadCharacter(block);

  // create a block to continue to on success
  BasicBlock* post = createBlock("post_literal");

  Py_UNICODE upper, lower;

  if (re.flags & SRE_FLAG_IGNORECASE && 
      (upper=Py_UNICODE_TOUPPER(c)) != (lower=Py_UNICODE_TOLOWER(c))) {
    // create a small switch to test upper & lower cases
    SwitchInst* switch_ = SwitchInst::Create(character,
        not_literal ? post : return_not_found, 2, block);
    // lower and upper cases
    switch_->addCase(ConstantInt::get(REM->charType, lower),
        not_literal ? return_not_found : post);
    switch_->addCase(ConstantInt::get(REM->charType, upper),
        not_literal ? return_not_found : post);
  } else {
    // is it equal to the character in the pattern
    Value* c_equal = new llvm::ICmpInst(*block, llvm::ICmpInst::ICMP_EQ, 
        character, ConstantInt::get(REM->charType, c), "c_equal");

    // depending on the kind of operation either continue or return REM->not_found
    if (not_literal) { /* not_literal */
      BranchInst::Create(return_not_found, post, c_equal, block);
    } else { /* literal */
      BranchInst::Create(post, return_not_found, c_equal, block);
    }
  }

  // clear any Python exception state that we don't care about
  PyErr_Clear();
  return post;
}

BasicBlock*
CompiledExpression::any(BasicBlock* block) {
  // get the next character
  block = loadCharacter(block);

  if (re.flags & SRE_FLAG_DOTALL) {
    // 'any' matches anything
  } else {
    // 'any' matches anything except for '\n'
    // is newline?
    Value* c_newline = new llvm::ICmpInst(*block, llvm::ICmpInst::ICMP_EQ, 
        character, ConstantInt::get(REM->charType, '\n'), "c_newline");

    // create a block to continue to on success
    BasicBlock* post = createBlock("post_any");
    BranchInst::Create(return_not_found, post, c_newline, block);
    block = post;
  }

  return block;
}

BasicBlock*
CompiledExpression::in(BasicBlock* block, PyObject* arg) {
  // the argument should be a sequence
  if (!PySequence_Check(arg)) {
    _PyErr_SetString(PyExc_TypeError, "Expected a sequence");
    return NULL;
  }

  // get the next character
  block = loadCharacter(block);

  // create a block where more tests can take place (ie: ranges, categories)
  BasicBlock* more_tests = createBlock("more_tests");

  // create a block for matches
  BasicBlock* matched = createBlock("matched");

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
        Py_XDECREF(item);
        return NULL;
      }

      // add a switch case
      switch_->addCase(ConstantInt::get(REM->charType, PyInt_AsLong(op_arg)),
          negate?return_not_found:matched);
    } else if (!strcmp(op_str, "range")) {
      // parse the start and end of the range
      int from, to;
      if(!PyArg_ParseTuple(op_arg, "ii", &from, &to)) {
        _PyErr_SetString(PyExc_ValueError, "Expected a 2-tuple of integers");
        Py_XDECREF(item);
        return NULL;
      }
      BasicBlock* yet_more_tests = createBlock("more_tests");
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
      BasicBlock* yet_more_tests = createBlock("more_tests");
      if (!testCategory(more_tests, character, category_name, 
            negate?return_not_found:matched, yet_more_tests)) {
        Py_XDECREF(item);
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
CompiledExpression::branch(BasicBlock* block, PyObject* arg) {
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
  BasicBlock* matched = createBlock("matched");

  // prepare the arguments for the branches
  std::vector<Value*> args;
  args.push_back(string);
  args.push_back(loadOffset(block));
  args.push_back(end_offset);
  args.push_back(groups);

  for (Py_ssize_t i=0; i<num_branches; i++) {
    // the block we'll go to if there's a match
    BasicBlock* match =  createBlock("match");
    // the next basic block we'll branch from
    BasicBlock* next = createBlock("branch");

    // get the branch sequence
    PyObject* branch = PySequence_GetItem(branches, i);
    if (branch == NULL) {
      _PyErr_SetString(PyExc_TypeError, "Failed to get branch");
      return NULL;
    }

    // compile it to a function
    CompiledExpression compiled_branch(re);
    compiled_branch.Compile(branch, 0);
    // done with the branch object
    Py_XDECREF(branch);

    // call it
    Value* branch_result = CallInst::Create(compiled_branch.function, 
        args.begin(), args.end(), "branch_result", block);
    // check it
    Value* branch_result_not_found = new ICmpInst(*block, ICmpInst::ICMP_EQ, 
        branch_result, REM->not_found, "branch_result_not_found");
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
CompiledExpression::at_end(BasicBlock* block)
{
  // match end of string, or \n before the end of the string
  // if in MULTILINE mode, also match just \n
  
  bool multiline = re.flags & SRE_FLAG_MULTILINE;
 
  // make the blocks we need
  BasicBlock* test_slash_n = createBlock("test_slash_n");
  BasicBlock* test_near_end;
  if (!multiline) {
   test_near_end = createBlock("test_near_end");
  }
  BasicBlock* next_block = createBlock("block");
  Value* offset = loadOffset(block);

  // are we at the end?
  Value* ended = new ICmpInst(*block, ICmpInst::ICMP_UGE, offset, end_offset, 
      "ended");
  BranchInst::Create(next_block, test_slash_n, ended, block);

  // is there a \n in the current position?
  Value* c_ptr = GetElementPtrInst::Create(string, offset, "c_ptr", 
      test_slash_n);
  Value* c = new LoadInst(c_ptr, "c", test_slash_n);
  Value* c_slash_n = new ICmpInst(*test_slash_n, ICmpInst::ICMP_EQ, c, 
      ConstantInt::get(REM->charType, '\n'), "c_slash_n");
  // for MULTILINE the \n means a match, for non MULTILINE we need to check
  // that we're almost at the end
  BranchInst::Create(multiline ? next_block : test_near_end, return_not_found,
      c_slash_n, test_slash_n);

  if (!multiline) {
    // are we near the end?
    Value* offset_plus_one = BinaryOperator::CreateAdd(offset,
        ConstantInt::get(REM->offsetType, 1), "offset_plus_one", test_near_end);
    Value* near_end = new ICmpInst(*test_near_end, ICmpInst::ICMP_UGE, 
        offset_plus_one, end_offset, "near_end");
    // either go to the next or return REM->not_found
    BranchInst::Create(next_block, return_not_found, near_end, test_near_end);
  }

  return next_block;
}


BasicBlock*
CompiledExpression::at_beginning(BasicBlock* block)
{
  // match the start of the string, also in multiline mode match after a
  // \n

  bool multiline = re.flags & SRE_FLAG_MULTILINE;

  Value* offset = loadOffset(block);

  BasicBlock* test_slash_n;
  if (multiline) {
   test_slash_n = createBlock("test_slash_n");
  }
  BasicBlock* next_block = createBlock("block");

  // are we at the start of the string?
  Value* start = new ICmpInst(*block, ICmpInst::ICMP_EQ, offset, 
      ConstantInt::get(REM->offsetType, 0), "start");
  // for multiline, we also check for \n before the offset
  BranchInst::Create(next_block, multiline ? test_slash_n : return_not_found,
      start, block);

  if (multiline) {
    // load the character back one
    Value* previous_offset = BinaryOperator::CreateSub(offset,
        ConstantInt::get(REM->offsetType, 1), "previous_offset", test_slash_n);
    Value* previous_c_ptr = GetElementPtrInst::Create(string, 
        previous_offset, "previous_c_ptr", 
      test_slash_n);
    Value* previous_c = new LoadInst(previous_c_ptr, "previous_c", 
        test_slash_n);
    // is it \n?
    Value* previous_c_slash_n = new ICmpInst(*test_slash_n, 
        ICmpInst::ICMP_EQ, previous_c, ConstantInt::get(REM->charType, '\n'),
        "previous_c_slash_n");
    BranchInst::Create(next_block, return_not_found, previous_c_slash_n,
        test_slash_n);
  }

  return next_block;
}

BasicBlock*
CompiledExpression::at_beginning_string(BasicBlock* block)
{
  // match the start of the string

  BasicBlock* next_block = createBlock("block");

  // are we at the start of the string?
  Value* offset = loadOffset(block);
  Value* start = new ICmpInst(*block, ICmpInst::ICMP_EQ, offset, 
      ConstantInt::get(REM->offsetType, 0), "start");
  BranchInst::Create(next_block, return_not_found, start, block);

  return next_block;
}

BasicBlock*
CompiledExpression::at_end_string(BasicBlock* block)
{
  // match the end of the string

  BasicBlock* next_block = createBlock("block");

  // are we at the end of the string?
  Value* offset = loadOffset(block);
  Value* ended = new ICmpInst(*block, ICmpInst::ICMP_UGE, offset, end_offset,
      "ended");
  BranchInst::Create(next_block, return_not_found, ended, block);

  return next_block;
}

BasicBlock*
CompiledExpression::at_boundary(BasicBlock* block, bool non_boundary) {
  // at a boundary if next (@offset) and previous (@offset-1) have different
  // word-ness, as determined by category_word. 
  // also, the start and end of strings are non-word.
 
  // set up blocks
  BasicBlock* test_prev = createBlock("test_prev");
  BasicBlock* post_test_prev = createBlock("post_test_prev");
  BasicBlock* pre_test_next = createBlock("pre_test_next");
  BasicBlock* test_next = createBlock("test_next");
  BasicBlock* post_test_next = createBlock("post_test_next");
  BasicBlock* test_word = createBlock("test_word");
  BasicBlock* next_block = createBlock("block");

  // initial block
  // variables to hold the word-ness of the previous and next characters
  Value* prev_word_ptr = new AllocaInst(REM->boolType, "prev_word_ptr", 
      block);
  Value* next_word_ptr = new AllocaInst(REM->boolType, "next_word_ptr", 
      block);

  // load the offset
  Value* offset = loadOffset(block);
  // set the next/prev word-ness values based on the offset
  Value* not_start = new ICmpInst(*block, ICmpInst::ICMP_NE, offset,
      ConstantInt::get(REM->offsetType, 0), "not_start");
  new StoreInst(not_start, prev_word_ptr, block);
  Value* not_end = new ICmpInst(*block, ICmpInst::ICMP_ULT, offset, 
      end_offset, "not_end");
  new StoreInst(not_end, next_word_ptr, block);

  // if we're not at the start then test the word-ness of the previous char
  BranchInst::Create(test_prev, pre_test_next, not_start, block);

  // test the previous character's word-ness
  Value* prev_off = BinaryOperator::CreateSub(offset, 
      ConstantInt::get(REM->offsetType, 1), "prev_off", test_prev);
  Value* prev_c_ptr = GetElementPtrInst::Create(string, prev_off, "prev_c_ptr",
      test_prev);
  Value* prev_c = new LoadInst(prev_c_ptr, "prev_c", test_prev);
  testCategory(test_prev, prev_c, "category_word", pre_test_next, 
      post_test_prev);
  
  // the previous is not a word, store that
  new StoreInst(ConstantInt::get(REM->boolType, 0), prev_word_ptr, post_test_prev);
  BranchInst::Create(pre_test_next, post_test_prev);

  // if we're not at the end then test the word-ness of the next character
  BranchInst::Create(test_next, test_word, not_end, pre_test_next);

  // test the next character's word-ness
  Value* next_c_ptr = GetElementPtrInst::Create(string, offset, "next_c_ptr",
      test_next);
  Value* next_c = new LoadInst(next_c_ptr, "next_c", test_next);
  testCategory(test_next, next_c, "category_word", test_word, post_test_next);

  // the next is not a word, store that
  new StoreInst(ConstantInt::get(REM->boolType, 0), next_word_ptr, post_test_next);
  BranchInst::Create(test_word, post_test_next);

  // compare the word-ness of the previous and next characters
  Value* prev_word = new LoadInst(prev_word_ptr, "prev_word", test_word);
  Value* next_word = new LoadInst(next_word_ptr, "next_word", test_word);
  Value* boundary = new ICmpInst(*test_word, ICmpInst::ICMP_NE, 
      prev_word, next_word, "boundary");

  if (non_boundary) {
    BranchInst::Create(return_not_found, next_block, boundary, test_word);
  } else {
    BranchInst::Create(next_block, return_not_found, boundary, test_word);
  }

  return next_block;
}

BasicBlock*
CompiledExpression::repeat(BasicBlock* block, PyObject* arg, PyObject* seq, 
    Py_ssize_t index, bool is_greedy) {
  // parse the arguments
  int min, max;
  PyObject* sub_pattern;
  
  if (!PyArg_ParseTuple(arg, "iiO", &min, &max, &sub_pattern) ||
      !PySequence_Check(sub_pattern)) {
    _PyErr_SetString(PyExc_TypeError, "Expected a tuple: int, int, sequence");
    return NULL;
  }

  CompiledExpression* repeated = new CompiledExpression(re);
  repeated->Compile(sub_pattern, 0);

  for (int i=0; i<min; i++) {
    // build the arguments to the function
    std::vector<Value*> args;
    args.push_back(string);
    args.push_back(loadOffset(block));
    args.push_back(end_offset);
    args.push_back(groups);
    Value* repeat_result = CallInst::Create(repeated->function, 
        args.begin(), args.end(), "repeat_result", block);
    Value* repeat_result_not_found = new ICmpInst(*block, ICmpInst::ICMP_EQ,
        repeat_result, REM->not_found, "repeat_result_not_found");
    BasicBlock* next = createBlock("repeat");
    BranchInst::Create(return_not_found, next, repeat_result_not_found,
        block);
    block = next;
    storeOffset(block, repeat_result);
  }

  if (max > min) {
    // there are an indeterminate number of repetitions
    // time to harness the power of RECURSION!

    // compile everything after this instruction...
    CompiledExpression* after = new CompiledExpression(re);
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
    args.push_back(ConstantInt::get(REM->offsetType, max-min));
    Value* recurse_result = CallInst::Create(recurse, args.begin(), args.end(),
        "recurse_result", block);
    Value* recurse_result_not_found = new ICmpInst(*block, ICmpInst::ICMP_EQ,
        recurse_result, REM->not_found, "recurse_result_not_found");

    BasicBlock* return_recurse_result = 
      createBlock("return_recurse_result");
    ReturnInst::Create(context(), recurse_result, return_recurse_result);

    BasicBlock* call_after = createBlock("call_after");
    args.resize(4); // use the same args as recurse without the countdown
    ReturnInst::Create(context(), CallInst::Create(after->function, args.begin(),
          args.end(), "after_result", call_after), call_after);

    BranchInst::Create(call_after, return_recurse_result,
        recurse_result_not_found, block);

    return NULL;
  }

  return block;
}

BasicBlock*
CompiledExpression::subpattern_begin(BasicBlock* block, PyObject* arg) {
  // the argument should just be an integer
  if (!PyInt_Check(arg)) {
    _PyErr_SetString(PyExc_TypeError, "Expected an integer");
    return NULL;
  }

  // get the group id
  long id = PyInt_AsLong(arg);

  // make sure the group ID isn't larger than we expect
  if (id > re.groups) {
    _PyErr_SetString(PyExc_ValueError, "Unexpected group id");
    return NULL;
  }

  // get the current offset
  Value* off = loadOffset(block);
  // store the start location
  int start_offset = (id-1)*2;
  Value* start_ptr = GetElementPtrInst::Create(groups, 
      ConstantInt::get(REM->offsetType, start_offset), "start_ptr", block);
  new StoreInst(off, start_ptr, block);

  // save the previous end offset when the function begins
  Value* old_start_offset_ptr = new AllocaInst(REM->offsetType, 
      "old_start_offset_ptr", entry);
  start_ptr = GetElementPtrInst::Create(groups, 
      ConstantInt::get(REM->offsetType, start_offset), "start_ptr", entry);
  new StoreInst(new LoadInst(start_ptr, "old_start", entry), 
      old_start_offset_ptr, entry);

  // if this expression fails, restore the start offset
  start_ptr = GetElementPtrInst::Create(groups, 
      ConstantInt::get(REM->offsetType, start_offset), "start_ptr", 
      return_not_found);
  new StoreInst(new LoadInst(old_start_offset_ptr, "old_start", 
        return_not_found), start_ptr, return_not_found);

  return block;
}

BasicBlock*
CompiledExpression::subpattern_end(BasicBlock* block, PyObject* arg) {
  // the argument should just be an integer
  if (!PyInt_Check(arg)) {
    _PyErr_SetString(PyExc_TypeError, "Expected an integer");
    return NULL;
  }

  // get the group id
  long id = PyInt_AsLong(arg);

  // make sure the group ID isn't larger than we expect
  if (id > re.groups) {
    _PyErr_SetString(PyExc_ValueError, "Unexpected group id");
    return NULL;
  }

  // get the current offset
  Value* off = loadOffset(block);
  // store the end location
  int end_offset = (id-1)*2+1;
  Value* end_ptr = GetElementPtrInst::Create(groups, 
      ConstantInt::get(REM->offsetType, end_offset), "end_ptr", block);
  new StoreInst(off, end_ptr, block);

  // store the group index at the end of the group array for
  // MatchObject.lastindex
  Value* lastindex_ptr = GetElementPtrInst::Create(groups,
      ConstantInt::get(REM->offsetType, re.groups*2), "lastindex_ptr", block);
  new StoreInst(ConstantInt::get(REM->offsetType, id), lastindex_ptr, block);

  // save the previous end offset when the function begins
  Value* old_end_offset_ptr = new AllocaInst(REM->offsetType, "old_end_offset_ptr",
      entry);
  end_ptr = GetElementPtrInst::Create(groups, 
      ConstantInt::get(REM->offsetType, end_offset), "end_ptr", entry);
  new StoreInst(new LoadInst(end_ptr, "old_end", entry), old_end_offset_ptr, 
      entry);

  // if this expression fails, restore the end offset
  end_ptr = GetElementPtrInst::Create(groups, 
      ConstantInt::get(REM->offsetType, end_offset), "end_ptr", return_not_found);
  new StoreInst(new LoadInst(old_end_offset_ptr, "old_end", return_not_found), 
      end_ptr, return_not_found);

  return block;
}

BasicBlock*
CompiledExpression::groupref(BasicBlock* block, PyObject* arg) {
  // FIXME: support IGNORECASE
  // @arg should be an integer
  if (!PyInt_Check(arg)) {
    _PyErr_SetString(PyExc_TypeError, "Expected an integer");
    return NULL;
  }

  // the group id
  int groupnum = PyInt_AsLong(arg);
  // make sure it's valid
  if (groupnum < 0 || groupnum > re.groups) {
    _PyErr_SetString(PyExc_ValueError, "Unexpected group id");
    return NULL;
  }

  // get the current position
  Value* offset = loadOffset(block);

  // find the start and end positions
  Value* start_ptr = GetElementPtrInst::Create(groups, 
    ConstantInt::get(REM->offsetType, (groupnum-1)*2), "start_ptr", block);
  Value* start_off = new LoadInst(start_ptr, "start_off", block);
  Value* end_ptr = GetElementPtrInst::Create(groups, 
    ConstantInt::get(REM->offsetType, (groupnum-1)*2+1), "end_ptr", block);
  Value* end_off = new LoadInst(end_ptr, "end_off", block);

  // check if the group exists
  Value* start_exists = new ICmpInst(*block, ICmpInst::ICMP_NE, start_off, 
      REM->not_found, "start_exists");
  Value* end_exists = new ICmpInst(*block, ICmpInst::ICMP_NE, end_off, 
      REM->not_found, "end_exists");
  Value* group_exists = BinaryOperator::CreateAnd(start_exists, end_exists,
      "group_exists", block);

  // find the length of the group
  Value* group_length = BinaryOperator::CreateSub(end_off, start_off, 
      "group_length", block);
  // where would the groupref end?
  Value* groupref_end = BinaryOperator::CreateAdd(offset, group_length, 
      "groupref_end", block);
  // do we have enough characters?
  Value* groupref_fits = new ICmpInst(*block, ICmpInst::ICMP_ULE, groupref_end,
      end_offset, "groupref_fits");

  // so, is it even possible that this groupref will work?
  Value* groupref_possible = BinaryOperator::CreateAnd(group_exists,
      groupref_fits, "groupref_possible", block);

  // create a block for testing the groupref
  BasicBlock* groupref_test = createBlock("groupre_test");

  // jump to that block if appropriate
  BranchInst::Create(groupref_test, return_not_found, groupref_possible, 
      block);

  // allocate a local variable to hold the offset into the group that we are
  // testing
  Value* group_off_ptr = new AllocaInst(REM->offsetType, "group_off_ptr", entry);

  // store the start address in that
  new StoreInst(start_off, group_off_ptr, groupref_test);

  // create blocks for looping over the group
  BasicBlock* groupref_loop = createBlock("groupref_loop");
  BasicBlock* groupref_loop_a = createBlock("groupref_loop_a");
  // create a block to continue into
  BasicBlock* next = createBlock("block");

  // jump into the loop
  BranchInst::Create(groupref_loop, groupref_test);

  // load the offset (that we're getting group contents from)
  Value* group_off = new LoadInst(group_off_ptr, "group_off", groupref_loop);
  // have we finished looping?
  Value* group_finished = new ICmpInst(*groupref_loop, ICmpInst::ICMP_EQ, 
      group_off, end_off, "group_finished");
  // if we finish looping then we have a match
  BranchInst::Create(next, groupref_loop_a, group_finished, groupref_loop);

  // load the next character in the string
  Value* string_c_off = loadOffset(groupref_loop_a);
  Value* string_c_ptr = GetElementPtrInst::Create(string, string_c_off, 
      "string_c_ptr", groupref_loop_a);
  Value* string_c = new LoadInst(string_c_ptr, "string_c", groupref_loop_a);
  // load the next character in the group
  Value* group_c_ptr = GetElementPtrInst::Create(string, group_off, 
      "group_c_ptr", groupref_loop_a);
  Value* group_c = new LoadInst(group_c_ptr, "group_c", groupref_loop_a);

  // increment offsets
  string_c_off = BinaryOperator::CreateAdd(string_c_off, 
      ConstantInt::get(REM->offsetType, 1), "increment", groupref_loop_a);
  storeOffset(groupref_loop_a, string_c_off);
  group_off = BinaryOperator::CreateAdd(group_off,
      ConstantInt::get(REM->offsetType, 1), "group_off_inc", groupref_loop_a);
  new StoreInst(group_off, group_off_ptr, groupref_loop_a);

  // compare the group and string values
  Value* groupref_match = new ICmpInst(*groupref_loop_a, ICmpInst::ICMP_EQ, 
      group_c, string_c, "groupref_match");
  // if they match then continue looping, otherwise return REM->not_found
  BranchInst::Create(groupref_loop, return_not_found, groupref_match, 
      groupref_loop_a);

  return next;
}
BasicBlock*
CompiledExpression::groupref_exists(BasicBlock* block, PyObject* arg) {
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
  BasicBlock* yes = createBlock("yes");
  BasicBlock* no = createBlock("no");
  BasicBlock* next_block = createBlock("block");

  // prepare the arguments for the sub-expressions
  std::vector<Value*> args;
  args.push_back(string);
  args.push_back(loadOffset(block));
  args.push_back(end_offset);
  args.push_back(groups);

  // compile the yes seq to a function
  CompiledExpression yes_compiled(re);
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
    CompiledExpression no_compiled(re);
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
      ConstantInt::get(REM->offsetType, (groupnum-1)*2+1), "end_ptr", block);
  // load the end value
  Value* end_off = new LoadInst(end_ptr, "end", block);
  // check if the end_off is not found
  Value* end_not_found = new ICmpInst(*block, ICmpInst::ICMP_EQ, end_off,
      REM->not_found, "end_not_found");
  // either yes or no
  BranchInst::Create(no, yes, end_not_found, block);

  return next_block;
}

BasicBlock*
CompiledExpression::assert_(BasicBlock* block, PyObject* arg, bool assert_not) {
  // @arg is a tuple of (direction, pattern)
  // direction is 1 or -1, pattern is a sequence
  // FIXME: implement backwards assertions
  int direction;
  PyObject* pattern;
  if (!PyArg_ParseTuple(arg, "iO", &direction, &pattern) ||
      !PySequence_Check(pattern)) {
    _PyErr_SetString(PyExc_TypeError, "Expected a tuple: direction, sequence");
  }

  if (direction != 1) {
    _PyErr_SetString(PyExc_ValueError, "Expected direction == 1");
  }

  // compile the assertion expression
  CompiledExpression compiled(re);
  compiled.Compile(pattern, 0);

  // call the assertion expression
  std::vector<Value*> args;
  args.push_back(string);
  args.push_back(loadOffset(block));
  args.push_back(end_offset);
  args.push_back(groups);
  Value* assert_result = CallInst::Create(compiled.function, 
      args.begin(), args.end(), "assert_result", block);

  // did it fail?
  Value* assert_not_found = new ICmpInst(*block, ICmpInst::ICMP_EQ,
      assert_result, REM->not_found, "assert_not_found");

  // create a block for continuation
  BasicBlock* next = createBlock("block");
  if (assert_not) {
    BranchInst::Create(next, return_not_found, assert_not_found, block);
  } else {
    BranchInst::Create(return_not_found, next, assert_not_found, block);
  }

  return next;
}

#ifndef TESTER

static PyObject *
RegEx_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  RegEx *self;

  self = (RegEx *)type->tp_alloc(type, 0);
  if (self != NULL) {
    self->re = new RegularExpression();
  }

  return (PyObject *)self;
}


static int
RegEx_init(RegEx *self, PyObject *args, PyObject *kwds)
{
  PyObject *seq=NULL;
  int flags, groups;

  static const char *kwlist[] = {"seq", "flags", "groups", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "Oii", (char**)kwlist, 
        &seq, &flags, &groups)) {
    return -1; 
  }
  
  // make sure we got a sequence
  if (!PySequence_Check(seq)) {
    _PyErr_SetString(PyExc_TypeError, "Expected a sequence");
    return -1;
  }

  Py_INCREF(seq);
  if (!self->re->Compile(seq, flags, groups)) {
    delete self->re;
    self->re = NULL;
    Py_DECREF(seq);
    return -1;
  }
  Py_DECREF(seq);

  return 0;
}

static void
RegEx_dealloc(RegEx* self)
{
  if (self && self->re) {
    delete self->re;
  }
}

static PyObject*
RegEx_dump(RegEx* self) {
  if (self->re) {
    REM->dump(self->re->find_function);
  }
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

  if (self->re) {
    return self->re->Match(characters, length, pos, end);
  } else {
    return NULL;
  }
}


static PyObject*
RegEx_find(RegEx* self, PyObject* args) {
  Py_UNICODE* characters;
  int length, pos, end;
  if (!PyArg_ParseTuple(args, "u#ii", &characters, &length, &pos, &end)) {
    return NULL;
  }

  if (self->re) {
    return self->re->Find(characters, length, pos, end);
  } else {
    return NULL;
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
  (destructor)RegEx_dealloc, /*tp_dealloc*/
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

  REM = new RegularExpressionModule();
  // FIXME: deallocate REM on Python module destruction
}

#endif /* TESTER */
