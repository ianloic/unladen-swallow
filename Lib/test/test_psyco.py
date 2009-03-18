from test import test_grammar, test_support
import cStringIO
import dis
import subprocess
import sys
import unittest


def disassemble(func):
    f = cStringIO.StringIO()
    tmp = sys.stdout
    sys.stdout = f
    try:
        dis.dis(func)
    finally:
        sys.stdout = tmp
    result = f.getvalue()
    f.close()
    return result


class PsycoTests(unittest.TestCase):
    def test_simple_jit(self):
        func = """def %s():
            buffer = cStringIO.StringIO()
            buffer.write('Hello ')
            buffer.write('World')
            return buffer.getvalue()"""
        functions = dict(cStringIO=cStringIO)
        exec (func % 'optimized') in functions
        opcodes = disassemble(functions['optimized'])
        self.assert_('FA:' in opcodes, "'FA:' did not appear in %s" % opcodes)
        psyco.bind(functions['optimized'])

        exec (func % 'normal') in functions
        self.assertEquals(functions['normal'](), functions['optimized']())

    def test_jit_table(self):
        func = r"""def %s(table):
            buffer = cStringIO.StringIO()
            buffer_write = buffer.write
            buffer_write('<table>\n')
            for row in table:
                buffer_write('<tr>\n')
                for column in row:
                    buffer_write('<td>')
                    buffer_write('%%s' %% column)
                    buffer_write('</td>\n')
                buffer_write('</tr>\n')
            buffer_write('</table>\n')
            return buffer.getvalue()"""
        functions = dict(cStringIO=cStringIO)
        exec (func % 'optimized') in functions
        opcodes = disassemble(functions['optimized'])
        self.assert_('FA:' in opcodes, "'FA:' did not appear in %s" % opcodes)
        psyco.bind(functions['optimized'])

        exec (func % 'normal') in functions
        arg = [xrange(5) for _ in xrange(5)]
        self.assertEquals(functions['normal'](arg), functions['optimized'](arg))

    def test_grammar(self):
        psyco.bind(test_grammar.GrammarTests)
        test_grammar.test_main()

def test_main():
    # Because Psyco pollutes the interpreter, we want to sandbox it as much as
    # possible. Failing to do so can cause other tests (anything having to do
    # with frames or tracing) to fail if run after test_psyco. The pollution
    # happens at import-time, so delay that until we're safely inside the
    # subprocess.
    if "child" in sys.argv:
        global psyco
        import psyco
        test_support.run_unittest(PsycoTests)
    else:
        pipe = subprocess.PIPE
        if test_support.verbose:
            pipe = None

        child = subprocess.Popen([sys.executable, "-E", __file__, "child"],
                                 stdout=pipe, stderr=pipe)
        result, err = child.communicate()
        if child.returncode != 0:
            raise AssertionError(err or "Test failed in subprocess")
        if test_support.verbose and result:
            print result,


if __name__ == "__main__":
    test_main()
