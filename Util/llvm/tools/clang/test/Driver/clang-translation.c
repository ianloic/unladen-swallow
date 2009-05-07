// RUN: clang -ccc-host-triple i386-unknown-unknown -### -S -O0 -Os %s -o %t.s -fverbose-asm 2> %t.log
// RUN: grep '"-triple" "i386-unknown-unknown"' %t.log &&
// RUN: grep '"-S"' %t.log &&
// RUN: grep '"-disable-free"' %t.log &&
// RUN: grep '"--relocation-model" "static"' %t.log &&
// RUN: grep '"--disable-fp-elim"' %t.log &&
// RUN: grep '"--unwind-tables=0"' %t.log &&
// RUN: grep '"--fmath-errno=1"' %t.log &&
// RUN: grep '"-Os"' %t.log &&
// RUN: grep '"-o" .*clang-translation\.c\.out\.tmp\.s' %t.log &&
// RUN: grep '"--asm-verbose"' %t.log &&
// RUN: true
