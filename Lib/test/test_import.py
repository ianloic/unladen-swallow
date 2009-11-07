import __builtin__
import os
import py_compile
import random
import shutil
import sys
import unittest
import warnings
from test import test_support


def remove_files(name):
    for f in (name + os.extsep + "py",
              name + os.extsep + "pyc",
              name + os.extsep + "pyo",
              name + os.extsep + "pyw",
              name + "$py.class"):
        if os.path.exists(f):
            os.remove(f)


class ImportTests(unittest.TestCase):

    def test_case_sensitivity(self):
        # Brief digression to test that import is case-sensitive:  if we got
        # this far, we know for sure that "random" exists.
        try:
            import RAnDoM
        except ImportError:
            pass
        else:
            self.fail("import of RAnDoM should have failed (case mismatch)")

    def test_double_const(self):
        # Another brief digression to test the accuracy of manifest float
        # constants.
        from test import double_const  # Don't blink -- that *was* the test.

    def test_import(self):
        def test_with_extension(ext):
            # The extension is normally ".py"; perhaps ".pyw".
            source = test_support.TESTFN + ext
            pyo = test_support.TESTFN + os.extsep + "pyo"
            if sys.platform.startswith('java'):
                pyc = test_support.TESTFN + "$py.class"
            else:
                pyc = test_support.TESTFN + os.extsep + "pyc"

            f = open(source, "w")
            try:
                print >> f, "# This tests importing a", ext, "file"
                a = random.randrange(1000)
                b = random.randrange(1000)
                print >> f, "a =", a
                print >> f, "b =", b
            finally:
                f.close()

            try:
                try:
                    mod = __import__(test_support.TESTFN)
                except ImportError, err:
                    self.fail("import from %s failed: %s" % (ext, err))

                self.assertEquals(mod.a, a,
                    "module loaded (%s) but contents invalid" % mod)
                self.assertEquals(mod.b, b,
                    "module loaded (%s) but contents invalid" % mod)
            finally:
                os.unlink(source)

            try:
                reload(mod)
            except ImportError, err:
                self.fail("import from .pyc/.pyo failed: %s" % err)
            finally:
                try:
                    os.unlink(pyc)
                except OSError:
                    pass
                try:
                    os.unlink(pyo)
                except OSError:
                    pass
                del sys.modules[test_support.TESTFN]

        sys.path.insert(0, os.curdir)
        try:
            test_with_extension(os.extsep + "py")
            if sys.platform.startswith("win"):
                for ext in [".PY", ".Py", ".pY", ".pyw", ".PYW", ".pYw"]:
                    test_with_extension(ext)
        finally:
            del sys.path[0]

    def test_imp_module(self):
        # Verify that the imp module can correctly load and find .py files
        import imp
        x = imp.find_module("os")
        os = imp.load_module("os", *x)

    def test_module_with_large_stack(self, module='longlist'):
        # Regression test for http://bugs.python.org/issue561858.
        filename = module + os.extsep + 'py'

        # Create a file with a list of 65000 elements.
        f = open(filename, 'w+')
        try:
            f.write('d = [\n')
            for i in range(65000):
                f.write('"",\n')
            f.write(']')
        finally:
            f.close()

        # Compile & remove .py file, we only need .pyc (or .pyo).
        f = open(filename, 'r')
        try:
            py_compile.compile(filename)
        finally:
            f.close()
        os.unlink(filename)

        # Need to be able to load from current dir.
        sys.path.append('')

        # This used to crash.
        exec 'import ' + module

        # Cleanup.
        del sys.path[-1]
        for ext in 'pyc', 'pyo':
            fname = module + os.extsep + ext
            if os.path.exists(fname):
                os.unlink(fname)

    def test_failing_import_sticks(self):
        source = test_support.TESTFN + os.extsep + "py"
        f = open(source, "w")
        try:
            print >> f, "a = 1/0"
        finally:
            f.close()

        # New in 2.4, we shouldn't be able to import that no matter how often
        # we try.
        sys.path.insert(0, os.curdir)
        try:
            for _ in range(3):
                try:
                    mod = __import__(test_support.TESTFN)
                except ZeroDivisionError:
                    if test_support.TESTFN in sys.modules:
                        self.fail("damaged module in sys.modules")
                else:
                    self.fail("was able to import a damaged module")
        finally:
            sys.path.pop(0)
            remove_files(test_support.TESTFN)

    def test_failing_reload(self):
        # A failing reload should leave the module object in sys.modules.
        source = test_support.TESTFN + os.extsep + "py"
        f = open(source, "w")
        try:
            print >> f, "a = 1"
            print >> f, "b = 2"
        finally:
            f.close()

        sys.path.insert(0, os.curdir)
        try:
            mod = __import__(test_support.TESTFN)
            self.assert_(test_support.TESTFN in sys.modules)
            self.assertEquals(mod.a, 1, "module has wrong attribute values")
            self.assertEquals(mod.b, 2, "module has wrong attribute values")

            # On WinXP, just replacing the .py file wasn't enough to
            # convince reload() to reparse it.  Maybe the timestamp didn't
            # move enough.  We force it to get reparsed by removing the
            # compiled file too.
            remove_files(test_support.TESTFN)

            # Now damage the module.
            f = open(source, "w")
            try:
                print >> f, "a = 10"
                print >> f, "b = 20//0"
            finally:
                f.close()

            self.assertRaises(ZeroDivisionError, reload, mod)

            # But we still expect the module to be in sys.modules.
            mod = sys.modules.get(test_support.TESTFN)
            self.failIf(mod is None, "expected module to be in sys.modules")

            # We should have replaced a w/ 10, but the old b value should
            # stick.
            self.assertEquals(mod.a, 10, "module has wrong attribute values")
            self.assertEquals(mod.b, 2, "module has wrong attribute values")
        finally:
            sys.path.pop(0)
            remove_files(test_support.TESTFN)
            if test_support.TESTFN in sys.modules:
                del sys.modules[test_support.TESTFN]

    def test_infinite_reload(self):
        # http://bugs.python.org/issue742342 reports that Python segfaults
        # (infinite recursion in C) when faced with self-recursive reload()ing.
        sys.path.insert(0, os.path.dirname(__file__))
        try:
            import infinite_reload
        finally:
            sys.path.pop(0)

    def test_import_name_binding(self):
        # import x.y.z binds x in the current namespace.
        import test as x
        import test.test_support
        self.assert_(x is test, x.__name__)
        self.assert_(hasattr(test.test_support, "__file__"))

        # import x.y.z as w binds z as w.
        import test.test_support as y
        self.assert_(y is test.test_support, y.__name__)

    def test_import_initless_directory_warning(self):
        with warnings.catch_warnings():
            # Just a random non-package directory we always expect to be
            # somewhere in sys.path...
            warnings.simplefilter('error', ImportWarning)
            self.assertRaises(ImportWarning, __import__, "site-packages")

    def test_import_by_filename(self):
        path = os.path.abspath(test_support.TESTFN)
        try:
            __import__(path)
        except ImportError, err:
            self.assertEqual("Import by filename is not supported.",
                              err.args[0])
        else:
            self.fail("import by path didn't raise an exception")


class PathsTests(unittest.TestCase):
    path = test_support.TESTFN

    def setUp(self):
        os.mkdir(self.path)
        self.syspath = sys.path[:]

    def tearDown(self):
        shutil.rmtree(self.path)
        sys.path = self.syspath

    def test_trailing_slash(self):
        # Regression test for http://bugs.python.org/issue1293.
        f = open(os.path.join(self.path, 'test_trailing_slash.py'), 'w')
        try:
            f.write("testdata = 'test_trailing_slash'")
        finally:
            f.close()
        sys.path.append(self.path+'/')
        mod = __import__("test_trailing_slash")
        self.assertEqual(mod.testdata, 'test_trailing_slash')
        test_support.unload("test_trailing_slash")


class RelativeImportTests(unittest.TestCase):
    def tearDown(self):
        try:
            del sys.modules["test.relimport"]
        except:
            pass

    def test_relimport_star(self):
        # This will import * from .test_import.
        from . import relimport
        self.assertTrue(hasattr(relimport, "RelativeImportTests"))

    def test_issue3221(self):
        # Regression test for http://bugs.python.org/issue3221.
        def check_absolute():
            exec "from os import path" in ns
        def check_relative():
            exec "from . import relimport" in ns

        # Check both OK with __package__ and __name__ correct
        ns = dict(__package__='test', __name__='test.notarealmodule')
        check_absolute()
        check_relative()

        # Check both OK with only __name__ wrong
        ns = dict(__package__='test', __name__='notarealpkg.notarealmodule')
        check_absolute()
        check_relative()

        # Check relative fails with only __package__ wrong
        ns = dict(__package__='foo', __name__='test.notarealmodule')
        with test_support.check_warnings() as w:
            check_absolute()
            self.assert_('foo' in str(w.message))
            self.assertEqual(w.category, RuntimeWarning)
        self.assertRaises(SystemError, check_relative)

        # Check relative fails with __package__ and __name__ wrong
        ns = dict(__package__='foo', __name__='notarealpkg.notarealmodule')
        with test_support.check_warnings() as w:
            check_absolute()
            self.assert_('foo' in str(w.message))
            self.assertEqual(w.category, RuntimeWarning)
        self.assertRaises(SystemError, check_relative)

        # Check both fail with package set to a non-string
        ns = dict(__package__=object())
        self.assertRaises(ValueError, check_absolute)
        self.assertRaises(ValueError, check_relative)


class OverridingImportBuiltinTests(unittest.TestCase):
    def test_override_builtin(self):
        # Test that overriding __builtin__.__import__ can bypass sys.modules.
        import os

        def foo():
            import os
            return os
        self.assertEqual(foo(), os)  # Quick sanity check.

        with test_support.swap_attr(__builtin__, "__import__", lambda *x: 5):
            self.assertEqual(foo(), 5)

        # Test what happens when we shadow __import__ in globals(); this
        # currently does not impact the import process, but if this changes,
        # other code will need to change, so keep this test as a tripwire.
        with test_support.swap_item(globals(), "__import__", lambda *x: 5):
            self.assertEqual(foo(), os)


def test_main(verbose=None):
    test_support.run_unittest(ImportTests, PathsTests, RelativeImportTests,
                              OverridingImportBuiltinTests)

if __name__ == '__main__':
    # Test needs to be a package, so we can do relative import.
    from test.test_import import test_main
    test_main()
