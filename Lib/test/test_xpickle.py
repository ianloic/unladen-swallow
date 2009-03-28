# test_pickle dumps and loads pickles via pickle.py.
# test_cpickle does the same, but via the cPickle module.
# This test covers the other two cases, making pickles with one module and
# loading them via the other.

import cPickle
import os
import pickle
import sys
import unittest

# Python 2.3 doesn't have this, but the worker doesn't need it anyway.
# This is just here to make the worker execute cleanly under 2.3.
if sys.version_info >= (2, 4):
    import subprocess

from test import test_support
from test.pickletester import AbstractPickleTests

class DumpCPickle_LoadPickle(AbstractPickleTests):

    error = KeyError

    def dumps(self, arg, proto=0, fast=False):
        # Ignore fast
        return cPickle.dumps(arg, proto)

    def loads(self, buf):
        # Ignore fast
        return pickle.loads(buf)

class DumpPickle_LoadCPickle(AbstractPickleTests):

    error = cPickle.BadPickleGet

    def dumps(self, arg, proto=0, fast=False):
        # Ignore fast
        return pickle.dumps(arg, proto)

    def loads(self, buf):
        # Ignore fast
        return cPickle.loads(buf)

def have_python_version(name):
    """Check whether the given name is a valid Python binary.

    This respects your PATH.

    Args:
        name: short string name of a Python binary such as "python2.4".

    Returns:
        True if the name is valid, False otherwise.
    """
    # Written like this for Python 2.3 compat.
    return os.system(name + " -c 'import sys; sys.exit()'") == 0

def send_to_worker(python, obj, proto):
    """Bounce an object through another version of Python using cPickle.

    This will pickle the object, send it to a child process where it will be
    unpickled, then repickled and sent back to the parent process.

    Args:
        python: the name of the Python binary to start.
        obj: object to pickle.
        proto: pickle protocol number to use.

    Returns:
        The pickled data received from the child process.
    """
    data = cPickle.dumps((proto, obj), proto)
    worker = subprocess.Popen([python, __file__, "worker"],
                              stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE)
    stdout, stderr = worker.communicate(data)
    if worker.returncode != 0:
        raise RuntimeError(stderr)
    return stdout


class AbstractCompatTests(AbstractPickleTests):

    python = None
    error = cPickle.BadPickleGet

    def setUp(self):
        self.assertTrue(self.python)

    def dumps(self, arg, proto=0, fast=False):
        return send_to_worker(self.python, arg, proto)

    def loads(self, input):
        return cPickle.loads(input)

    # These tests are disabled because they require some special setup
    # on the worker that's hard to keep in sync.
    def test_global_ext1(self):
        pass

    def test_global_ext2(self):
        pass

    def test_global_ext4(self):
        pass


if not have_python_version("python2.3"):
    class Python23Compat(unittest.TestCase):
        pass
else:
    class Python23Compat(AbstractCompatTests):

        python = "python2.3"

        # Disable these tests for Python 2.3. Making them pass would require
        # nontrivially monkeypatching the pickletester module in the worker.
        def test_reduce_calls_base(self):
            pass

        def test_reduce_ex_calls_base(self):
            pass


if not have_python_version("python2.4"):
    class Python24Compat(unittest.TestCase):
        pass
else:
    class Python24Compat(AbstractCompatTests):

        python = "python2.4"

        # Disable these tests for Python 2.4. Making them pass would require
        # nontrivially monkeypatching the pickletester module in the worker.
        def test_reduce_calls_base(self):
            pass

        def test_reduce_ex_calls_base(self):
            pass


if not have_python_version("python2.5"):
    class Python25Compat(unittest.TestCase):
        pass
else:
    class Python25Compat(AbstractCompatTests):

        python = "python2.5"


def worker_main(in_stream, out_stream):
    message = cPickle.load(in_stream)
    protocol, obj = message
    cPickle.dump(obj, out_stream, protocol)


def test_main():
    test_support.run_unittest(
        DumpCPickle_LoadPickle,
        DumpPickle_LoadCPickle,
        Python23Compat,
        Python24Compat,
        Python25Compat,
    )

if __name__ == "__main__":
    if "worker" in sys.argv:
        worker_main(sys.stdin, sys.stdout)
    else:
        test_main()
