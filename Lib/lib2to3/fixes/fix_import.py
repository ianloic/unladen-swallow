"""Fixer for import statements.
If spam is being imported from the local directory, this import:
    from spam import eggs
Becomes:
    from .spam import eggs

And this import:
    import spam
Becomes:
    from . import spam
"""

# Local imports
from .. import fixer_base
from os.path import dirname, join, exists, pathsep
from ..fixer_util import FromImport

class FixImport(fixer_base.BaseFix):

    PATTERN = """
    import_from< type='from' imp=any 'import' ['('] any [')'] >
    |
    import_name< type='import' imp=any >
    """

    def transform(self, node, results):
        imp = results['imp']

        if unicode(imp).startswith('.'):
            # Already a new-style import
            return

        if not probably_a_local_import(unicode(imp), self.filename):
            # I guess this is a global import -- skip it!
            return

        if results['type'].value == 'from':
            # Some imps are top-level (eg: 'import ham')
            # some are first level (eg: 'import ham.eggs')
            # some are third level (eg: 'import ham.eggs as spam')
            # Hence, the loop
            while not hasattr(imp, 'value'):
                imp = imp.children[0]
            imp.value = "." + imp.value
            node.changed()
        else:
            new = FromImport('.', getattr(imp, 'content', None) or [imp])
            new.set_prefix(node.get_prefix())
            node = new
        return node

def probably_a_local_import(imp_name, file_path):
    # Must be stripped because the right space is included by the parser
    imp_name = imp_name.split('.', 1)[0].strip()
    base_path = dirname(file_path)
    base_path = join(base_path, imp_name)
    # If there is no __init__.py next to the file its not in a package
    # so can't be a relative import.
    if not exists(join(dirname(base_path), '__init__.py')):
        return False
    for ext in ['.py', pathsep, '.pyc', '.so', '.sl', '.pyd']:
        if exists(base_path + ext):
            return True
    return False
