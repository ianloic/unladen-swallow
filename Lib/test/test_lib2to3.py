# Skipping test_parser and test_all_fixers
# because of running
from lib2to3.tests import test_fixers, test_pytree, test_util, test_refactor
import sys
from test.test_support import run_unittest
import unittest

def suite():
    tests = unittest.TestSuite()
    loader = unittest.TestLoader()
    for m in (test_fixers,test_pytree,test_util, test_refactor):
        tests.addTests(loader.loadTestsFromModule(m))
    return tests

def test_main():
    run_unittest(suite())


if __name__ == '__main__':
    if sys.flags.optimize >= 2:
        print >>sys.stderr, "test_lib2to3 --",
        print >>sys.stderr, "skipping some tests due to -O flag."
        sys.stderr.flush()
    test_main()
