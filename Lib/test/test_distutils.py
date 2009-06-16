"""Tests for distutils.

The tests for distutils are defined in the distutils.tests package;
the test_suite() function there returns a test suite that's ready to
be run.
"""

import distutils.tests
import sys
import test.test_support


def test_main():
    test.test_support.run_unittest(distutils.tests.test_suite())


if __name__ == "__main__":
    if sys.flags.optimize >= 2:
        print >>sys.stderr, "test_distutils --",
        print >>sys.stderr, "skipping some tests due to -O flag."
        sys.stderr.flush()
    test_main()
