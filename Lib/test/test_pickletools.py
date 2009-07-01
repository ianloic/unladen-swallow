import pickle
import pickletools
from test import test_support
from test.pickletester import AbstractPickleTests
from test.pickletester import AbstractPickleModuleTests

class OptimizedPickleTests(AbstractPickleTests, AbstractPickleModuleTests):

    def dumps(self, arg, proto=0, fast=False):
        return pickletools.optimize(pickle.dumps(arg, proto))

    def loads(self, buf):
        return pickle.loads(buf)

    def dump(self, arg, buf, proto=0, fast=False):
        data = self.dumps(arg, proto, fast)
        buf.write(data)

    def load(self, buf):
        return pickle.load(buf)

    module = pickle
    error = KeyError

def test_main():
    test_support.run_unittest(OptimizedPickleTests)
    test_support.run_doctest(pickletools)


if __name__ == "__main__":
    test_main()
