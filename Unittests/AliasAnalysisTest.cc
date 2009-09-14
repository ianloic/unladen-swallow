#include "Util/PyAliasAnalysis.h"

#include "Python.h"
#include "Python/global_llvm_data.h"
#include "Util/ConstantMirror.h"
#include "Util/PyTypeBuilder.h"

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/BasicBlock.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/Function.h"
#include "llvm/ModuleProvider.h"
#include "llvm/PassManager.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Target/TargetData.h"
#include "gtest/gtest.h"
#include <algorithm>

using llvm::AliasAnalysis;
using llvm::Constant;
using llvm::Function;
using llvm::FunctionPassManager;
using llvm::GlobalValue;
using llvm::GlobalVariable;
using llvm::IRBuilder;
using llvm::Pass;
using llvm::Value;
using llvm::cast;

namespace {

class PythonRuntime {
public:
    PythonRuntime()
    {
        Py_NoSiteFlag = true;
        Py_Initialize();
    }
    ~PythonRuntime()
    {
        Py_Finalize();
    }
};

struct capture_ref_t {};
const capture_ref_t capture_ref = {};

class PyHandle {
public:
    PyHandle() : obj_(NULL) {}
    PyHandle(const PyHandle &other) : obj_(other.obj_) {
        Py_XINCREF(obj_);
    }
    void swap(PyHandle& other) {
        std::swap(this->obj_, other.obj_);
    }
    PyHandle &operator=(PyHandle other) {
        this->swap(other);
        return *this;
    }
    ~PyHandle() {
        Py_XDECREF(this->obj_);
    }

    explicit PyHandle(PyObject *obj) : obj_(obj) {
        Py_XINCREF(obj);
    }
    PyHandle(PyObject *obj, capture_ref_t) : obj_(obj) {
    }

    operator PyObject *() { return this->obj_; }
    PyObject *operator->() { return this->obj_; }
    template<typename ObjectT>
    ObjectT *as() { return (ObjectT*)this->obj_; }

private:
    PyObject *obj_;
};

class AliasAnalysisTest : public testing::Test {
protected:
    AliasAnalysisTest()
        : global_data_(*PyGlobalLlvmData::Get()),
          mirror_(this->global_data_.constant_mirror()),
          fpm_(this->global_data_.module_provider()),
          function_(Function::Create(PyTypeBuilder<void()>::get(
                                         this->global_data_.context()),
                                     GlobalValue::ExternalLinkage,
                                     "function", this->global_data_.module())),
          builder_(llvm::BasicBlock::Create(this->function_->getContext(), "",
                                            this->function_))
    {
        fpm_.add(new llvm::TargetData(
                     *this->global_data_.getExecutionEngine()
                     ->getTargetData()));
        // Make sure nothing stupid happens.
        fpm_.add(llvm::createVerifierPass());
    }

    PythonRuntime pr_;
    PyGlobalLlvmData &global_data_;
    PyConstantMirror &mirror_;
    FunctionPassManager fpm_;
    Function *function_;
    IRBuilder<> builder_;
};

TEST_F(AliasAnalysisTest, ImmutableValue)
{
    PyHandle an_int(PyInt_FromLong(7));
    Constant *int_gv = this->mirror_.GetGlobalVariableFor(an_int);
    Value *int_refcnt = PyTypeBuilder<PyIntObject>::ob_refcnt(
        this->builder_, int_gv);
    Value *int_type_ptr = PyTypeBuilder<PyIntObject>::ob_type(
        this->builder_, int_gv);
    Value *int_ival = PyTypeBuilder<PyIntObject>::ob_ival(
        this->builder_, int_gv);

    // PyAliasAnalysis can't look through load instructions, but the
    // constant propagators that know about AliasAnalysis should
    // handle this for us.
    GlobalVariable *int_type = cast<GlobalVariable>(
        this->mirror_.GetGlobalVariableFor((PyObject*)&PyInt_Type));
    Value *type_refcnt = PyTypeBuilder<PyTypeObject>::ob_refcnt(
        this->builder_, int_type);
    Value *type_number = PyTypeBuilder<PyTypeObject>::tp_as_number(
        this->builder_, int_type);

    Constant *int_as_number =
        cast<llvm::ConstantStruct>(int_type->getInitializer())->getOperand(
            PyTypeBuilder<PyTypeObject>::tp_as_number_index(
                this->global_data_.context()));
    Value *divmod = PyTypeBuilder<PyNumberMethods>::nb_divmod(
        this->builder_, int_as_number);

    this->builder_.CreateRetVoid();

    Pass *pass = CreatePyAliasAnalysis(this->global_data_);
    AliasAnalysis *aa = dynamic_cast<AliasAnalysis*>(pass);
    this->fpm_.add(pass);
    this->fpm_.run(*this->function_);

    EXPECT_FALSE(aa->pointsToConstantMemory(int_gv));
    EXPECT_FALSE(aa->pointsToConstantMemory(int_refcnt));
    EXPECT_TRUE(aa->pointsToConstantMemory(int_type_ptr));
    EXPECT_TRUE(aa->pointsToConstantMemory(int_ival));

    EXPECT_FALSE(aa->pointsToConstantMemory(int_type))
        << "The whole type includes the non-const refcount";
    EXPECT_FALSE(aa->pointsToConstantMemory(type_refcnt));
    EXPECT_TRUE(aa->pointsToConstantMemory(type_number));

    EXPECT_TRUE(aa->pointsToConstantMemory(int_as_number));
    EXPECT_TRUE(aa->pointsToConstantMemory(divmod));
}

TEST_F(AliasAnalysisTest, ImmutableTypeMutableValue)
{
    PyHandle a_list(PyList_New(1));
    PyList_SetItem(a_list, 0, PyInt_FromLong(3));
    Value *list_gv = this->builder_.CreateBitCast(
        this->mirror_.GetGlobalVariableFor(a_list),
        PyTypeBuilder<PyListObject*>::get(this->global_data_.context()));
    Value *list_refcnt = PyTypeBuilder<PyListObject>::ob_refcnt(
        this->builder_, list_gv);
    Value *list_type_ptr = PyTypeBuilder<PyListObject>::ob_type(
        this->builder_, list_gv);
    Value *list_allocated = PyTypeBuilder<PyListObject>::allocated(
        this->builder_, list_gv);

    // PyAliasAnalysis can't look through load instructions, but the
    // constant propagators that know about AliasAnalysis should
    // handle this for us.
    GlobalVariable *list_type = cast<GlobalVariable>(
        this->mirror_.GetGlobalVariableFor((PyObject*)&PyList_Type));
    Value *type_refcnt = PyTypeBuilder<PyTypeObject>::ob_refcnt(
        this->builder_, list_type);
    Value *type_type = PyTypeBuilder<PyTypeObject>::ob_type(
        this->builder_, list_type);
    Value *type_sequence = PyTypeBuilder<PyTypeObject>::tp_as_sequence(
        this->builder_, list_type);

    Constant *list_as_sequence =
        cast<llvm::ConstantStruct>(list_type->getInitializer())->getOperand(
            PyTypeBuilder<PyTypeObject>::tp_as_sequence_index(
                this->global_data_.context()));
    Value *slice = PyTypeBuilder<PySequenceMethods>::sq_slice(
        this->builder_, list_as_sequence);

    this->builder_.CreateRetVoid();

    Pass *pass = CreatePyAliasAnalysis(this->global_data_);
    AliasAnalysis *aa = dynamic_cast<AliasAnalysis*>(pass);
    this->fpm_.add(pass);
    this->fpm_.run(*this->function_);

    EXPECT_FALSE(aa->pointsToConstantMemory(list_gv));
    EXPECT_FALSE(aa->pointsToConstantMemory(list_refcnt));
    EXPECT_TRUE(aa->pointsToConstantMemory(list_type_ptr));
    EXPECT_FALSE(aa->pointsToConstantMemory(list_allocated));

    EXPECT_FALSE(aa->pointsToConstantMemory(list_type))
        << "The whole type includes the non-const refcount";
    EXPECT_FALSE(aa->pointsToConstantMemory(type_refcnt));
    EXPECT_TRUE(aa->pointsToConstantMemory(type_type));
    EXPECT_TRUE(aa->pointsToConstantMemory(type_sequence));

    EXPECT_TRUE(aa->pointsToConstantMemory(list_as_sequence));
    EXPECT_TRUE(aa->pointsToConstantMemory(slice));
}

TEST_F(AliasAnalysisTest, MutableType)
{
    PyHandle globals(PyDict_New());
    ASSERT_EQ(0, PyDict_SetItemString(
                  globals, "__builtins__", PyEval_GetBuiltins()));
    PyHandle locals(PyDict_New());
    PyHandle run_result(PyRun_String("class A(object): pass\na = A()\n",
                                     Py_file_input, globals, locals));
    ASSERT_TRUE(run_result != NULL);
    PyObject *object = PyDict_GetItemString(locals, "a");
    ASSERT_TRUE(object != NULL);
    PyObject *type = PyDict_GetItemString(locals, "A");
    ASSERT_TRUE(type != NULL);
    Value *object_gv = this->builder_.CreateBitCast(
        this->mirror_.GetGlobalVariableFor(object),
        PyTypeBuilder<PyObject*>::get(this->global_data_.context()));
    Value *object_refcnt = PyTypeBuilder<PyObject>::ob_refcnt(
        this->builder_, object_gv);
    Value *object_type_ptr = PyTypeBuilder<PyObject>::ob_type(
        this->builder_, object_gv);

    // PyAliasAnalysis can't look through load instructions, but the
    // constant propagators that know about AliasAnalysis should
    // handle this for us.
    GlobalVariable *type_gv = cast<GlobalVariable>(
        this->mirror_.GetGlobalVariableFor(type));
    Value *type_refcnt = PyTypeBuilder<PyTypeObject>::ob_refcnt(
        this->builder_, type_gv);
    Value *type_type = PyTypeBuilder<PyTypeObject>::ob_type(
        this->builder_, type_gv);
    Value *type_repr = PyTypeBuilder<PyTypeObject>::tp_repr(
        this->builder_, type_gv);

    this->builder_.CreateRetVoid();

    Pass *pass = CreatePyAliasAnalysis(this->global_data_);
    AliasAnalysis *aa = dynamic_cast<AliasAnalysis*>(pass);
    this->fpm_.add(pass);
    this->fpm_.run(*this->function_);

    EXPECT_FALSE(aa->pointsToConstantMemory(object_gv));
    EXPECT_FALSE(aa->pointsToConstantMemory(object_refcnt));
    EXPECT_FALSE(aa->pointsToConstantMemory(object_type_ptr));

    EXPECT_FALSE(aa->pointsToConstantMemory(type_gv));
    EXPECT_FALSE(aa->pointsToConstantMemory(type_refcnt));
    EXPECT_TRUE(aa->pointsToConstantMemory(type_type))
        << "You can't change a (simple) type's type.";
    EXPECT_FALSE(aa->pointsToConstantMemory(type_repr));
}

}  // namespace
