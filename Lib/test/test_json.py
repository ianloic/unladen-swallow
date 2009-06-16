"""Tests for json.

The tests for json are defined in the json.tests package;
the test_suite() function there returns a test suite that's ready to
be run.
"""

import json.tests
import sys
import test.test_support


def test_main():
    test.test_support.run_unittest(json.tests.test_suite())


if __name__ == "__main__":
    if sys.flags.optimize >= 2:
        print >>sys.stderr, "test_json -- skipping some tests due to -O flag."
        sys.stderr.flush()
    test_main()
