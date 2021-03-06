// RUN: clang-cc -fsyntax-only -verify %s
struct A; // expected-note 14 {{forward declaration of 'struct A'}}

A f(); // expected-note {{note: 'f' declared here}}

struct B {
  A f(); // expected-note {{'f' declared here}}
  A operator()(); // expected-note 2 {{'operator()' declared here}}
  operator A(); // expected-note {{'operator A' declared here}}
  A operator!(); // expected-note 2 {{'operator!' declared here}}
  A operator++(int); // expected-note {{'operator++' declared here}}
  A operator[](int); // expected-note {{'operator[]' declared here}}
  A operator+(int); // expected-note {{'operator+' declared here}}
  A operator->(); // expected-note {{'operator->' declared here}}
};

void g() {
  f(); // expected-error {{calling 'f' with incomplete return type 'struct A'}}

  typedef A (*Func)();
  Func fp;
  fp(); // expected-error {{calling function with incomplete return type 'struct A'}}
  ((Func)0)();  // expected-error {{calling function with incomplete return type 'struct A'}}  
  
  B b;
  b.f(); // expected-error {{calling 'f' with incomplete return type 'struct A'}}
  
  b.operator()(); // expected-error {{calling 'operator()' with incomplete return type 'struct A'}}
  b.operator A(); // expected-error {{calling 'operator A' with incomplete return type 'struct A'}}
  b.operator!(); // expected-error {{calling 'operator!' with incomplete return type 'struct A'}}
  
  !b; // expected-error {{calling 'operator!' with incomplete return type 'struct A'}}
  b(); // expected-error {{calling 'operator()' with incomplete return type 'struct A'}}
  b++; // expected-error {{calling 'operator++' with incomplete return type 'struct A'}}
  b[0]; // expected-error {{calling 'operator[]' with incomplete return type 'struct A'}}
  b + 1; // expected-error {{calling 'operator+' with incomplete return type 'struct A'}}
  b->f(); // expected-error {{calling 'operator->' with incomplete return type 'struct A'}}
  
  A (B::*mfp)() = 0;
  (b.*mfp)(); // expected-error {{calling function with incomplete return type 'struct A'}}
  
}


struct C; // expected-note{{forward declaration}}

void test_incomplete_object_call(C& c) {
  c(); // expected-error{{incomplete type in call to object of type}}
}
