// RUN: clang -emit-llvm %s -o %t

#ifdef __APPLE__
#include <Carbon/Carbon.h>

void f() {
  CFSTR("Hello, World!");
}

// rdar://6151192
void *G = CFSTR("yo joe");

#endif
