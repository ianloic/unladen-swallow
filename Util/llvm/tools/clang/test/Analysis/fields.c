// RUN: clang -analyze -checker-cfref %s --analyzer-store-basic -verify &&
// RUN: clang -analyze -checker-cfref %s --analyzer-store-region -verify &&
// RUN: clang -analyze -checker-simple %s -verify

unsigned foo();
typedef struct bf { unsigned x:2; } bf;
void bar() {
  bf y;
  *(unsigned*)&y = foo();
  y.x = 1;
}
