// RUN: clang -fsyntax-only -verify -parse-noop %s
#if 0
#include <stdio.h>
void _Block_byref_release(void*src){}

int main() {
   __block  int X = 1234;
   __block  const char * message = "HELLO";

   X = X - 1234;

   X += 1;

   printf ("%s(%d)\n", message, X);
   X -= 1;

   return X;
}
#endif
