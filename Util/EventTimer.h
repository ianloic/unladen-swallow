// -*- C++ -*-
#ifndef UTIL_TIMERS_H
#define UTIL_TIMERS_H

#ifndef __cplusplus
#error This header expects to be included only in C++ source
#endif

#include "Python.h"

#include "llvm/Support/ManagedStatic.h"
#include "llvm/System/Mutex.h"

#include <map>
#include <pthread.h>
#include <time.h>
#include <utility>
#include <vector>


/// Timer class used to measure times between various events, such as the time
/// between a CALL_FUNCTION opcode start and the execution of the function.
/// At Python-shutdown, the event log is printed to stdout.

class _PyEventTimer {

public:
    ~_PyEventTimer();

    // This must be kept in sync with event_names in EventTimer.cc
    enum Event {
        CALL_START_EVAL,        // Top of CALL_FUNCTION_* opcodes
        CALL_START_LLVM,        // Top of CALL_FUNCTION_* LLVM IRs
        CALL_ENTER_EVAL,        // Top of PyEval_EvalFrame
        CALL_ENTER_PYOBJ_CALL,  // Any call to PyObject_Call from eval.cc
        CALL_ENTER_C,           // Before calling C methods in eval.cc
        CALL_ENTER_LLVM,        // Top of function entry block in LLVM
        CALL_END_EVAL,          // Bottom of CALL_FUNCTION* opcodes
        LLVM_COMPILE_START,     // Before JITing or looking up native code
        LLVM_COMPILE_END,       // After JITing or looking up native code
        JIT_START,              // Start of LLVM jitting
        JIT_END,                // End of LLVM jitting
    };

    static const char * const EventToString(Event event);

    typedef std::vector< std::pair<Event, uint64_t> > EventVector;

    void LogEvent(Event event);

private:
    // Serialize mutations of this->data_.
    llvm::sys::Mutex lock_;

    // Central respository for all the data. This maps (Label, Label) pairs
    // to a vector of times in nanoseconds (based on clock_gettime()).
    EventVector data_;

};

/// Vanilla C function wrapping C++ method so that it can be called from LLVM
/// IR.
extern "C" void _PyLogEvent(_PyEventTimer::Event event);

/// Simple macro that wraps up the ifdef WITH_TSC check so that callers don't
/// have to spell it out in their code.
#ifdef WITH_TSC
#define PY_LOG_EVENT(event) _PyLogEvent(event)
#else
#define PY_LOG_EVENT(event)
#endif


#endif  // UTIL_TIMERS_H
