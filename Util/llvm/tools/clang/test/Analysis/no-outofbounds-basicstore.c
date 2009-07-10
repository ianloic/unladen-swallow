// RUN: clang-cc -checker-cfref -analyze -analyzer-store=basic -verify %s &&
// RUN: clang-cc -checker-cfref -analyze -analyzer-store=basic-new-cast -verify %s

void f() {
  long x = 0;
  char *y = (char*) &x;
  char c = y[0] + y[1] + y[2]; // no-warning
}
