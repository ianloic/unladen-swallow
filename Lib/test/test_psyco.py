from test.test_support import run_unittest
import cStringIO
import dis
import psyco
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


def test_main():
    run_unittest(PsycoTests)

if __name__ == "__main__":
    test_main()
