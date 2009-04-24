// RUN: clang -fsyntax-only -verify %s &&
// RUN: clang -fsyntax-only -verify %s -triple x86_64-apple-darwin9

void f1(int a)
{
    __builtin_va_list ap;
    
    __builtin_va_start(ap, a, a); // expected-error {{too many arguments to function}}
    __builtin_va_start(ap, a); // expected-error {{'va_start' used in function with fixed args}}
}

void f2(int a, int b, ...)
{
    __builtin_va_list ap;
    
    __builtin_va_start(ap, 10); // expected-warning {{second parameter of 'va_start' not last named argument}}
    __builtin_va_start(ap, a); // expected-warning {{second parameter of 'va_start' not last named argument}}
    __builtin_va_start(ap, b);
}

void f3(float a, ...)
{
    __builtin_va_list ap;
    
    __builtin_va_start(ap, a);
    __builtin_va_start(ap, (a));
}


// stdarg: PR3075
void f4(const char *msg, ...) {
 __builtin_va_list ap;
 __builtin_stdarg_start((ap), (msg));
 __builtin_va_end (ap);
}

void f5() {
  __builtin_va_list ap;
  __builtin_va_start(ap,ap); // expected-error {{'va_start' used in function with fixed args}}
}

void f6(int a, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap); // expected-error {{too few arguments to function}}
}

// PR3350
void
foo(__builtin_va_list authors, ...) {
  __builtin_va_start (authors, authors);
  (void)__builtin_va_arg(authors, int);
  __builtin_va_end (authors);
}

