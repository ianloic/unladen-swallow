// RUN: clang-cc -fsyntax-only -verify %s
// C++0x N2914.

struct A {
  template<class T> void f(T);
  template<class T> struct X { };
};

struct B : A {
  using A::f<double>; // expected-error{{using declaration can not refer to template specialization}}
  using A::X<int>; // expected-error{{using declaration can not refer to template specialization}}
};