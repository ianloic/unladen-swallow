# Very rudimentary test of threading module

import test.test_support
from test.test_support import verbose
import os
import random
import re
import subprocess
import sys
import threading
import thread
import time
import unittest
import weakref

# A trivial mutable counter.
class Counter(object):
    def __init__(self):
        self.value = 0
    def inc(self):
        self.value += 1
    def dec(self):
        self.value -= 1
    def get(self):
        return self.value

class TestThread(threading.Thread):
    def __init__(self, name, testcase, sema, mutex, nrunning):
        threading.Thread.__init__(self, name=name)
        self.testcase = testcase
        self.sema = sema
        self.mutex = mutex
        self.nrunning = nrunning

    def run(self):
        delay = random.random() / 10000.0
        if verbose:
            print 'task %s will run for %.1f usec' % (
                self.name, delay * 1e6)

        with self.sema:
            with self.mutex:
                self.nrunning.inc()
                if verbose:
                    print self.nrunning.get(), 'tasks are running'
                self.testcase.assert_(self.nrunning.get() <= 3)

            time.sleep(delay)
            if verbose:
                print 'task', self.name, 'done'

            with self.mutex:
                self.nrunning.dec()
                self.testcase.assert_(self.nrunning.get() >= 0)
                if verbose:
                    print '%s is finished. %d tasks are running' % (
                        self.name, self.nrunning.get())

class ThreadTests(unittest.TestCase):

    # Create a bunch of threads, let each do some work, wait until all are
    # done.
    def test_various_ops(self):
        # This takes about n/3 seconds to run (about n/3 clumps of tasks,
        # times about 1 second per clump).
        NUMTASKS = 10

        # no more than 3 of the 10 can run at once
        sema = threading.BoundedSemaphore(value=3)
        mutex = threading.RLock()
        numrunning = Counter()

        threads = []

        for i in range(NUMTASKS):
            t = TestThread("<thread %d>"%i, self, sema, mutex, numrunning)
            threads.append(t)
            self.failUnlessEqual(t.ident, None)
            self.assert_(re.match('<TestThread\(.*, initial\)>', repr(t)))
            t.start()

        if verbose:
            print 'waiting for all tasks to complete'
        for t in threads:
            t.join(NUMTASKS)
            self.assert_(not t.is_alive())
            self.failIfEqual(t.ident, 0)
            self.assert_(re.match('<TestThread\(.*, \w+ -?\d+\)>', repr(t)))
        if verbose:
            print 'all tasks done'
        self.assertEqual(numrunning.get(), 0)

    # run with a small(ish) thread stack size (256kB)
    def test_various_ops_small_stack(self):
        if verbose:
            print 'with 256kB thread stack size...'
        try:
            threading.stack_size(262144)
        except thread.error:
            if verbose:
                print 'platform does not support changing thread stack size'
            return
        self.test_various_ops()
        threading.stack_size(0)

    # run with a large thread stack size (1MB)
    def test_various_ops_large_stack(self):
        if verbose:
            print 'with 1MB thread stack size...'
        try:
            threading.stack_size(0x100000)
        except thread.error:
            if verbose:
                print 'platform does not support changing thread stack size'
            return
        self.test_various_ops()
        threading.stack_size(0)

    def test_foreign_thread(self):
        # Check that a "foreign" thread can use the threading module.
        def f(mutex):
            # Acquiring an RLock forces an entry for the foreign
            # thread to get made in the threading._active map.
            r = threading.RLock()
            r.acquire()
            r.release()
            mutex.release()

        mutex = threading.Lock()
        mutex.acquire()
        tid = thread.start_new_thread(f, (mutex,))
        # Wait for the thread to finish.
        mutex.acquire()
        self.assert_(tid in threading._active)
        self.assert_(isinstance(threading._active[tid],
                                threading._DummyThread))
        del threading._active[tid]

    # PyThreadState_SetAsyncExc() is a CPython-only gimmick, not (currently)
    # exposed at the Python level.  This test relies on ctypes to get at it.
    def test_PyThreadState_SetAsyncExc(self):
        try:
            import ctypes
        except ImportError:
            if verbose:
                print "test_PyThreadState_SetAsyncExc can't import ctypes"
            return  # can't do anything

        set_async_exc = ctypes.pythonapi.PyThreadState_SetAsyncExc

        class AsyncExc(Exception):
            pass

        exception = ctypes.py_object(AsyncExc)

        # `worker_started` is set by the thread when it's inside a try/except
        # block waiting to catch the asynchronously set AsyncExc exception.
        # `worker_saw_exception` is set by the thread upon catching that
        # exception.
        worker_started = threading.Event()
        worker_saw_exception = threading.Event()

        class Worker(threading.Thread):
            def run(self):
                self.id = thread.get_ident()
                self.finished = False

                try:
                    while True:
                        worker_started.set()
                        time.sleep(0.1)
                except AsyncExc:
                    self.finished = True
                    worker_saw_exception.set()

        t = Worker()
        t.daemon = True # so if this fails, we don't hang Python at shutdown
        t.start()
        if verbose:
            print "    started worker thread"

        # Try a thread id that doesn't make sense.
        if verbose:
            print "    trying nonsensical thread id"
        result = set_async_exc(ctypes.c_long(-1), exception)
        self.assertEqual(result, 0)  # no thread states modified

        # Now raise an exception in the worker thread.
        if verbose:
            print "    waiting for worker thread to get started"
        worker_started.wait()
        if verbose:
            print "    verifying worker hasn't exited"
        self.assert_(not t.finished)
        if verbose:
            print "    attempting to raise asynch exception in worker"
        result = set_async_exc(ctypes.c_long(t.id), exception)
        self.assertEqual(result, 1) # one thread state modified
        if verbose:
            print "    waiting for worker to say it caught the exception"
        worker_saw_exception.wait(timeout=10)
        self.assert_(t.finished)
        if verbose:
            print "    all OK -- joining worker"
        if t.finished:
            t.join()
        # else the thread is still running, and we have no way to kill it

    def test_finalize_runnning_thread(self):
        # Issue 1402: the PyGILState_Ensure / _Release functions may be called
        # very late on python exit: on deallocation of a running thread for
        # example.
        try:
            import ctypes
        except ImportError:
            if verbose:
                print("test_finalize_with_runnning_thread can't import ctypes")
            return  # can't do anything

        rc = subprocess.call([sys.executable, "-c", """if 1:
            import ctypes, sys, time, thread

            # This lock is used as a simple event variable.
            ready = thread.allocate_lock()
            ready.acquire()

            # Module globals are cleared before __del__ is run
            # So we save the functions in class dict
            class C:
                ensure = ctypes.pythonapi.PyGILState_Ensure
                release = ctypes.pythonapi.PyGILState_Release
                def __del__(self):
                    state = self.ensure()
                    self.release(state)

            def waitingThread():
                x = C()
                ready.release()
                time.sleep(100)

            thread.start_new_thread(waitingThread, ())
            ready.acquire()  # Be sure the other thread is waiting.
            sys.exit(42)
            """])
        self.assertEqual(rc, 42)

    def test_finalize_with_trace(self):
        # Issue1733757
        # Avoid a deadlock when sys.settrace steps into threading._shutdown
        rc = subprocess.call([sys.executable, "-c", """if 1:
            import sys, threading

            # A deadlock-killer, to prevent the
            # testsuite to hang forever
            def killer():
                import os, time
                time.sleep(2)
                print 'program blocked; aborting'
                os._exit(2)
            t = threading.Thread(target=killer)
            t.daemon = True
            t.start()

            # This is the trace function
            def func(frame, event, arg):
                threading.current_thread()
                return func

            sys.settrace(func)
            """])
        self.failIf(rc == 2, "interpreted was blocked")
        self.failUnless(rc == 0, "Unexpected error")


    def test_enumerate_after_join(self):
        # Try hard to trigger #1703448: a thread is still returned in
        # threading.enumerate() after it has been join()ed.
        enum = threading.enumerate
        old_interval = sys.getcheckinterval()
        try:
            for i in xrange(1, 100):
                # Try a couple times at each thread-switching interval
                # to get more interleavings.
                sys.setcheckinterval(i // 5)
                t = threading.Thread(target=lambda: None)
                t.start()
                t.join()
                l = enum()
                self.assertFalse(t in l,
                    "#1703448 triggered after %d trials: %s" % (i, l))
        finally:
            sys.setcheckinterval(old_interval)

    def test_no_refcycle_through_target(self):
        class RunSelfFunction(object):
            def __init__(self, should_raise):
                # The links in this refcycle from Thread back to self
                # should be cleaned up when the thread completes.
                self.should_raise = should_raise
                self.thread = threading.Thread(target=self._run,
                                               args=(self,),
                                               kwargs={'yet_another':self})
                self.thread.start()

            def _run(self, other_ref, yet_another):
                if self.should_raise:
                    raise SystemExit

        cyclic_object = RunSelfFunction(should_raise=False)
        weak_cyclic_object = weakref.ref(cyclic_object)
        cyclic_object.thread.join()
        del cyclic_object
        self.assertEquals(None, weak_cyclic_object(),
                          msg=('%d references still around' %
                               sys.getrefcount(weak_cyclic_object())))

        raising_cyclic_object = RunSelfFunction(should_raise=True)
        weak_raising_cyclic_object = weakref.ref(raising_cyclic_object)
        raising_cyclic_object.thread.join()
        del raising_cyclic_object
        self.assertEquals(None, weak_raising_cyclic_object(),
                          msg=('%d references still around' %
                               sys.getrefcount(weak_raising_cyclic_object())))


class ThreadJoinOnShutdown(unittest.TestCase):

    def _run_and_join(self, script):
        script = """if 1:
            import sys, os, time, threading

            # a thread, which waits for the main program to terminate
            def joiningfunc(mainthread):
                mainthread.join()
                print 'end of thread'
        \n""" + script

        p = subprocess.Popen([sys.executable, "-c", script], stdout=subprocess.PIPE)
        rc = p.wait()
        data = p.stdout.read().replace('\r', '')
        self.assertEqual(rc, 0, "Unexpected error")
        self.assertEqual(data, "end of main\nend of thread\n")

    def test_1_join_on_shutdown(self):
        # The usual case: on exit, wait for a non-daemon thread
        script = """if 1:
            import os
            t = threading.Thread(target=joiningfunc,
                                 args=(threading.current_thread(),))
            t.start()
            time.sleep(0.1)
            print 'end of main'
            """
        self._run_and_join(script)


    def test_2_join_in_forked_process(self):
        # Like the test above, but from a forked interpreter
        if not hasattr(os, 'fork'):
            return
        script = """if 1:
            childpid = os.fork()
            if childpid != 0:
                os.waitpid(childpid, 0)
                sys.exit(0)

            t = threading.Thread(target=joiningfunc,
                                 args=(threading.current_thread(),))
            t.start()
            print 'end of main'
            """
        self._run_and_join(script)

    def test_3_join_in_forked_from_thread(self):
        # Like the test above, but fork() was called from a worker thread
        # In the forked process, the main Thread object must be marked as stopped.
        if not hasattr(os, 'fork'):
            return
        # Skip platforms with known problems forking from a worker thread.
        # See http://bugs.python.org/issue3863.
        if sys.platform in ('freebsd4', 'freebsd5', 'freebsd6', 'os2emx'):
            print >>sys.stderr, ('Skipping test_3_join_in_forked_from_thread'
                                 ' due to known OS bugs on'), sys.platform
            return
        script = """if 1:
            main_thread = threading.current_thread()
            def worker():
                childpid = os.fork()
                if childpid != 0:
                    os.waitpid(childpid, 0)
                    sys.exit(0)

                t = threading.Thread(target=joiningfunc,
                                     args=(main_thread,))
                print 'end of main'
                t.start()
                t.join() # Should not block: main_thread is already stopped

            w = threading.Thread(target=worker)
            w.start()
            """
        self._run_and_join(script)


class ThreadAndForkTests(unittest.TestCase):

    def assertScriptHasOutput(self, script, expected_output):
        p = subprocess.Popen([sys.executable, "-c", script],
                             stdout=subprocess.PIPE)
        rc = p.wait()
        data = p.stdout.read().decode().replace('\r', '')
        self.assertEqual(rc, 0, "Unexpected error")
        self.assertEqual(data, expected_output)

    def test_join_fork_stop_deadlock(self):
        # There used to be a possible deadlock when forking from a child
        # thread.  See http://bugs.python.org/issue6643.

        if not hasattr(os, 'fork'):
            return
        # Skip platforms with known problems forking from a worker thread.
        # See http://bugs.python.org/issue3863.
        if sys.platform in ('freebsd4', 'freebsd5', 'freebsd6', 'os2emx'):
            raise unittest.SkipTest('due to known OS bugs on ' + sys.platform)

        # The script takes the following steps:
        # - The main thread in the parent process starts a new thread and then
        #   tries to join it.
        # - The join operation acquires the Lock inside the thread's __block
        #   Condition.  (See threading.py:Thread.join().)
        # - We stub out the acquire method on the condition to force it to wait
        #   until the child thread forks.  (See LOCK ACQUIRED HERE)
        # - The child thread forks.  (See LOCK HELD and WORKER THREAD FORKS
        #   HERE)
        # - The main thread of the parent process enters Condition.wait(),
        #   which releases the lock on the child thread.
        # - The child process returns.  Without the necessary fix, when the
        #   main thread of the child process (which used to be the child thread
        #   in the parent process) attempts to exit, it will try to acquire the
        #   lock in the Thread.__block Condition object and hang, because the
        #   lock was held across the fork.

        script = """if True:
            import os, time, threading
            finish_join = False
            start_fork = False

            def worker():
                # Wait until this thread's lock is acquired before forking to
                # create the deadlock.
                global finish_join
                while not start_fork:
                    time.sleep(0.01)
                # LOCK HELD: Main thread holds lock across this call.
                childpid = os.fork()
                finish_join = True
                if childpid != 0:
                    # Parent process just waits for child.
                    os.waitpid(childpid, 0)
                # Child process should just return.

            w = threading.Thread(target=worker)

            # Stub out the private condition variable's lock acquire method.
            # This acquires the lock and then waits until the child has forked
            # before returning, which will release the lock soon after.  If
            # someone else tries to fix this test case by acquiring this lock
            # before forking instead of reseting it, the test case will
            # deadlock when it shouldn't.
            condition = w._Thread__block
            orig_acquire = condition.acquire
            call_count_lock = threading.Lock()
            call_count = 0
            def my_acquire():
                global call_count
                global start_fork
                orig_acquire()  # LOCK ACQUIRED HERE
                start_fork = True
                if call_count == 0:
                    while not finish_join:
                        time.sleep(0.01)  # WORKER THREAD FORKS HERE
                with call_count_lock:
                    call_count += 1
            condition.acquire = my_acquire

            w.start()
            w.join()
            print('end of main')
            """
        self.assertScriptHasOutput(script, "end of main\n")

    def test_thread_fork_thread_hang(self):
        # Check that a thread that forks and then spawns a daemon thread can
        # exit properly.  Previously, it would just exit one thread instead of
        # shutting down the entire process, so the daemon thread would prevent
        # the process from exiting, causing the parent process to hang in
        # waitpid().

        if not hasattr(os, 'fork'):
            return
        # Skip platforms with known problems forking from a worker thread.
        # See http://bugs.python.org/issue3863.
        if sys.platform in ('freebsd4', 'freebsd5', 'freebsd6', 'os2emx'):
            raise unittest.SkipTest('due to known OS bugs on ' + sys.platform)
        script = """if True:
            import os, threading
            def worker():
                childpid = os.fork()
                if childpid != 0:
                    # Parent waits for child.
                    os.waitpid(childpid, 0)
                else:
                    # Child spawns a daemon thread and then returns
                    # immediately.
                    def daemon():
                        while True:
                            pass
                    d = threading.Thread(target=daemon)
                    d.daemon = True
                    d.start()
                    # Try to exit the process by returning from the last
                    # non-daemon thread.  We used to hang because the daemon
                    # thread was still running:
                    # http://bugs.python.org/issue6642
                    print('end of thread')

            w = threading.Thread(target=worker)
            w.start()
            w.join()
            print('end of main')
            """
        self.assertScriptHasOutput(script, "end of thread\nend of main\n")

    def test_thread_fork_atexit(self):
        # Check that a spawned thread that forks properly executes atexit
        # handlers.  Previously, Py_Finalize was not being called because the
        # main thread in the child process does not return through Py_Main().
        # Now there is a check in thread_PyThread_exit_thread() to run these
        # finalizers if the exiting thread happens to be the main thread.

        if not hasattr(os, 'fork'):
            return
        # Skip platforms with known problems forking from a worker thread.
        # See http://bugs.python.org/issue3863.
        if sys.platform in ('freebsd4', 'freebsd5', 'freebsd6', 'os2emx'):
            raise unittest.SkipTest('due to known OS bugs on ' + sys.platform)
        script = """if True:
            import os, threading, atexit

            def worker():
                # Setup a pipe between the processes, and register an atexit
                # handler to write the pid to the pipe.  Note, these are file
                # descriptors, not file-like objects.
                (reader, writer) = os.pipe()
                def write_atexit():
                    if writer is None:
                        return
                    os.write(writer, str(os.getpid()))
                    os.close(writer)
                atexit.register(write_atexit)

                childpid = os.fork()
                if childpid != 0:  # Parent
                    os.close(writer)
                    # Throw away the writer so that the atexit handler does
                    # nothing in the parent.
                    writer = None
                    os.waitpid(childpid, 0)
                    output = os.read(reader, 100)
                    if output == str(childpid):
                        print "successfully read child pid"
                    else:
                        print "got bad output:", output
                else:  # Child
                    os.close(reader)
                    # The child should just return without exiting.  We want to
                    # verify that the atexit handler gets called.

            w = threading.Thread(target=worker)
            w.start()
            w.join()
            """
        self.assertScriptHasOutput(script, "successfully read child pid\n")


class ThreadingExceptionTests(unittest.TestCase):
    # A RuntimeError should be raised if Thread.start() is called
    # multiple times.
    def test_start_thread_again(self):
        thread = threading.Thread()
        thread.start()
        self.assertRaises(RuntimeError, thread.start)

    def test_releasing_unacquired_rlock(self):
        rlock = threading.RLock()
        self.assertRaises(RuntimeError, rlock.release)

    def test_waiting_on_unacquired_condition(self):
        cond = threading.Condition()
        self.assertRaises(RuntimeError, cond.wait)

    def test_notify_on_unacquired_condition(self):
        cond = threading.Condition()
        self.assertRaises(RuntimeError, cond.notify)

    def test_semaphore_with_negative_value(self):
        self.assertRaises(ValueError, threading.Semaphore, value = -1)
        self.assertRaises(ValueError, threading.Semaphore, value = -sys.maxint)

    def test_joining_current_thread(self):
        current_thread = threading.current_thread()
        self.assertRaises(RuntimeError, current_thread.join);

    def test_joining_inactive_thread(self):
        thread = threading.Thread()
        self.assertRaises(RuntimeError, thread.join)

    def test_daemonize_active_thread(self):
        thread = threading.Thread()
        thread.start()
        self.assertRaises(RuntimeError, setattr, thread, "daemon", True)


def test_main():
    test.test_support.run_unittest(ThreadTests,
                                   ThreadJoinOnShutdown,
                                   ThreadingExceptionTests,
                                   ThreadAndForkTests,
                                   )

if __name__ == "__main__":
    test_main()
