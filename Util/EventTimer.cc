#include "Util/EventTimer.h"

#include "Python.h"

#include "Include/pystate.h"
#include "Include/pythread.h"

#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MutexGuard.h"

#include <algorithm>
#include <numeric>
#include <time.h>
#include <utility>
#include <vector>
#if defined(_M_IX86) || defined(_M_X64) /* x86 or x64 on MSVC */
#include <intrin.h>  /* for __rdtsc() */
#endif


#define PY_EVENT_BUFFER_SIZE 10000


#ifdef WITH_TSC


/// Timer class used to measure times between various events, such as the time
/// between a CALL_FUNCTION opcode start and the execution of the function.
/// At Python-shutdown, the event log is printed to stderr.  This class is
/// declared here instead of in the header so that the header can be included
/// by straight C files.

class _PyEventTimer {

public:
    ~_PyEventTimer();

    static const char * const EventToString(_PyEventId event);

    typedef std::vector< _PyEvent > EventVector;

    void LogEvent(_PyEventId event);

    void PrintData();

private:
    // Serialize mutations of this->data_.
    llvm::sys::Mutex lock_;

    // Central respository for all the data. This maps (Label, Label) pairs
    // to a vector of times in nanoseconds (based on clock_gettime()).
    EventVector data_;

};


static llvm::ManagedStatic< _PyEventTimer > event_timer;

// XXX(rnk): I have only tested this on x86_64.  It needs to be tested on i386
// and PPC.
static inline tsc_t
read_tsc() {
        tsc_t time;

#if defined(__ppc__) /* <- Don't know if this is the correct symbol; this
			   section should work for GCC on any PowerPC
			   platform, irrespective of OS.
			   POWER?  Who knows :-) */

	register unsigned long tbu, tb, tbu2;
  loop:
	asm volatile ("mftbu %0" : "=r" (tbu) );
	asm volatile ("mftb  %0" : "=r" (tb)  );
	asm volatile ("mftbu %0" : "=r" (tbu2));
	if (__builtin_expect(tbu != tbu2, 0)) goto loop;

	/* The slightly peculiar way of writing the next lines is
	   compiled better by GCC than any other way I tried. */
	((long*)(time))[0] = tbu;
	((long*)(time))[1] = tb;

#elif defined(__x86_64__) || defined(__amd64__)

	uint64_t low, high;
	asm volatile ("rdtsc" : "=a" (low), "=d" (high));
	time = (high << 32) | low;

#elif defined(__i386__)

        asm volatile("rdtsc" : "=A" (time));

#elif defined(_M_IX86) || defined(_M_X64) /* x86 or x64 on MSVC */

	time = __rdtsc();

#endif

        return time;
}

/// _PyEventTimer

void
_PyLogEvent(_PyEventId event) {
    event_timer->LogEvent(event);
}

// This must be kept in sync with the Event enum in EventTimer.h
static const char * const event_names[] = {
    "CALL_START_EVAL",
    "CALL_START_LLVM",
    "CALL_ENTER_EVAL",
    "CALL_ENTER_PYOBJ_CALL",
    "CALL_ENTER_C",
    "CALL_ENTER_LLVM",
    "CALL_END_EVAL",
    "LLVM_COMPILE_START",
    "LLVM_COMPILE_END",
    "JIT_START",
    "JIT_END",
    "EXCEPT_RAISE_EVAL",
    "EXCEPT_RAISE_LLVM",
    "EXCEPT_CATCH_EVAL",
    "EXCEPT_CATCH_LLVM",
};

const char * const
_PyEventTimer::EventToString(_PyEventId event_id) {
    return event_names[(int)event_id];
}

void
_PyEventTimer::LogEvent(_PyEventId event_id) {
    // XXX(rnk): This probably has more overhead than we'd like.
    PyThreadState *tstate = PyThreadState_GET();
    if (tstate->interp->tscdump) {
        _PyEvent event;
        event.thread_id = PyThread_get_thread_ident();
        event.event_id = event_id;
        event.time = read_tsc();
        llvm::MutexGuard locked(this->lock_);
        this->data_.push_back(event);
        if (this->data_.size() > PY_EVENT_BUFFER_SIZE) {
            this->PrintData();
        }
    }
}

void
_PyEventTimer::PrintData() {
    // Print the data to stderr as a tab separated file.
    for (EventVector::iterator it = this->data_.begin();
         it != this->data_.end(); ++it) {
        const char * const str_name = this->EventToString(it->event_id);
        fprintf(stderr, "%ld\t%s\t%llu\n",
                it->thread_id, str_name, it->time);
    }
    this->data_.clear();
}

_PyEventTimer::~_PyEventTimer() {
    this->PrintData();
}


#endif  // WITH_TSC
