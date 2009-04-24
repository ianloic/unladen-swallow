// RUN: clang -fsyntax-only -verify %s

#include <stddef.h>

struct S // expected-note {{candidate}}
{
  S(int, int, double); // expected-note {{candidate}}
  S(double, int); // expected-note {{candidate}} expected-note {{candidate}}
  S(float, int); // expected-note {{candidate}} expected-note {{candidate}}
};
struct T; // expected-note{{forward declaration of 'struct T'}}
struct U
{
  // A special new, to verify that the global version isn't used.
  void* operator new(size_t, S*);
};
struct V : U
{
};

void* operator new(size_t); // expected-note {{candidate}}
void* operator new(size_t, int*); // expected-note {{candidate}}
void* operator new(size_t, float*); // expected-note {{candidate}}

void good_news()
{
  int *pi = new int;
  float *pf = new (pi) float();
  pi = new int(1);
  pi = new int('c');
  const int *pci = new const int();
  S *ps = new S(1, 2, 3.4);
  ps = new (pf) (S)(1, 2, 3.4);
  S *(*paps)[2] = new S*[*pi][2];
  ps = new (S[3])(1, 2, 3.4);
  typedef int ia4[4];
  ia4 *pai = new (int[3][4]);
  pi = ::new int;
  U *pu = new (ps) U;
  // This is xfail. Inherited functions are not looked up currently.
  //V *pv = new (ps) V;
}

void bad_news(int *ip)
{
  int i = 1;
  (void)new; // expected-error {{missing type specifier}}
  (void)new 4; // expected-error {{missing type specifier}}
  (void)new () int; // expected-error {{expected expression}}
  (void)new int[1.1]; // expected-error {{array size expression must have integral or enumerated type, not 'double'}}
  (void)new int[1][i]; // expected-error {{only the first dimension}}
  (void)new (int[1][i]); // expected-error {{only the first dimension}}
  (void)new int(*(S*)0); // expected-error {{incompatible type initializing}}
  (void)new int(1, 2); // expected-error {{initializer of a builtin type can only take one argument}}
  (void)new S(1); // expected-error {{no matching constructor}}
  (void)new S(1, 1); // expected-error {{call to constructor of 'S' is ambiguous}}
  (void)new const int; // expected-error {{must provide an initializer}}
  (void)new float*(ip); // expected-error {{incompatible type initializing 'int *', expected 'float *'}}
  // Undefined, but clang should reject it directly.
  (void)new int[-1]; // expected-error {{array size is negative}}
  (void)new int[*(S*)0]; // expected-error {{array size expression must have integral or enumerated type, not 'struct S'}}
  (void)::S::new int; // expected-error {{expected unqualified-id}}
  (void)new (0, 0) int; // expected-error {{no matching function for call to 'operator new'}}
  (void)new (0L) int; // expected-error {{call to 'operator new' is ambiguous}}
  // This must fail, because the member version shouldn't be found.
  (void)::new ((S*)0) U; // expected-error {{no matching function for call to 'operator new'}}
  // Some lacking cases due to lack of sema support.
}

void good_deletes()
{
  delete (int*)0;
  delete [](int*)0;
  delete (S*)0;
  ::delete (int*)0;
}

void bad_deletes()
{
  delete 0; // expected-error {{cannot delete expression of type 'int'}}
  delete [0] (int*)0; // expected-error {{expected ']'}} \
                      // expected-note {{to match this '['}}
  delete (void*)0; // expected-error {{cannot delete expression}}
  delete (T*)0; // expected-warning {{deleting pointer to incomplete type}}
  ::S::delete (int*)0; // expected-error {{expected unqualified-id}}
}
