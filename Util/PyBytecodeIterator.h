// -*- C++ -*-
#ifndef UTIL_PYBYTECODEITERATOR_H
#define UTIL_PYBYTECODEITERATOR_H

#ifndef __cplusplus
#error This header expects to be included only in C++ source
#endif


class PyBytecodeIterator {
public:
    // Initializes the iterator to point to the first opcode in the
    // bytecode string.
    PyBytecodeIterator(PyObject *bytecode_string);
    // Allow the default copy operations.

    int Opcode() const { return this->opcode_; }
    int Oparg() const { return this->oparg_; }
    size_t CurIndex() const { return this->cur_index_; }
    size_t NextIndex() const { return this->next_index_; }
    bool Done() const { return this->cur_index_ == this->bytecode_size_; }
    bool Error() const { return this->error_; }

    // Advances the iterator by one opcode, including the effect of
    // any EXTENDED_ARG opcode in the way.  If there is an
    // EXTENDED_ARG, this->CurIndex() will point to it rather than the
    // actual opcode, since that's where jumps land.  If the bytecode
    // is malformed, this will set a Python error and cause
    // this->Error() to return true.
    void Advance();

private:
    int opcode_;
    int oparg_;
    size_t cur_index_;
    size_t next_index_;
    bool error_;
    const unsigned char *const bytecode_str_;
    const size_t bytecode_size_;
};

#endif  // UTIL_PYBYTECODEITERATOR_H
