#!/usr/bin/awk -f
#
# This script removes #line declarations, incorrect stack variable
# declarations, empty comments, and VM_DEBUG sections from ceval-vm.i
# between when vmgen generates it from ceval.vmg and when it's copied
# into Include/.  It would probably be possible to make ceval-vm.i
# compile without this script, but it doesn't seem worth the time.

/\/\*  \*\// { next }
/^#line/     { next }

/^IF___none__TOS/ { next }
/^__none__/       { next }
/^incref/         { next }
/^decref/         { next }
/^next/           { next }

/[ \t]+$/ { sub(/[ \t]+$/, "") }

BEGIN               { s = 0 }
/^#ifdef VM_DEBUG$/ { s = 1 }
s == 0              { print }
s == 1              {       }
/^#endif$/          { s = 0 }

# eof
