// RUN: clang-cc -verify -fsyntax-only %s

static void (*fp0)(void) __attribute__((noreturn));

static void __attribute__((noreturn)) f0(void) {
  fatal();
} // expected-warning {{function declared 'noreturn' should not return}}

// On K&R
int f1() __attribute__((noreturn));

int g0 __attribute__((noreturn)); // expected-warning {{'noreturn' attribute only applies to function types}}

int f2() __attribute__((noreturn(1, 2))); // expected-error {{attribute requires 0 argument(s)}}

void f3() __attribute__((noreturn));
void f3() {
  return;  // expected-warning {{function 'f3' declared 'noreturn' should not return}}
}

#pragma clang diagnostic error "-Winvalid-noreturn"

void f4() __attribute__((noreturn));
void f4() {
  return;  // expected-error {{function 'f4' declared 'noreturn' should not return}}
}

// PR4685
extern void f5 (unsigned long) __attribute__ ((__noreturn__));

void
f5 (unsigned long size)
{
  
}
