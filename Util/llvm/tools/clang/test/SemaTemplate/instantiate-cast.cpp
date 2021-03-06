// RUN: clang-cc -fsyntax-only -verify %s

struct A { int x; }; 

class Base { 
public:
  virtual void f();
};

class Derived : public Base { };

struct ConvertibleToInt {
  operator int() const;
};

struct Constructible {
  Constructible(int, float);
};

// ---------------------------------------------------------------------
// C-style casts
// ---------------------------------------------------------------------
template<typename T, typename U>
struct CStyleCast0 {
  void f(T t) {
    (void)((U)t); // expected-error{{C-style cast from 'struct A' to 'int'}}
  }
};

template struct CStyleCast0<int, float>;
template struct CStyleCast0<A, int>; // expected-note{{instantiation}}

// ---------------------------------------------------------------------
// static_cast
// ---------------------------------------------------------------------
template<typename T, typename U>
struct StaticCast0 {
  void f(T t) {
    (void)static_cast<U>(t); // expected-error{{static_cast from 'int' to 'struct A' is not allowed}}
  }
};

template struct StaticCast0<ConvertibleToInt, bool>;
template struct StaticCast0<int, float>;
template struct StaticCast0<int, A>; // expected-note{{instantiation}}

// ---------------------------------------------------------------------
// dynamic_cast
// ---------------------------------------------------------------------
template<typename T, typename U>
struct DynamicCast0 {
  void f(T t) {
    (void)dynamic_cast<U>(t); // expected-error{{not a reference or pointer}}
  }
};

template struct DynamicCast0<Base*, Derived*>;
template struct DynamicCast0<Base*, A>; // expected-note{{instantiation}}

// ---------------------------------------------------------------------
// reinterpret_cast
// ---------------------------------------------------------------------
template<typename T, typename U>
struct ReinterpretCast0 {
  void f(T t) {
    (void)reinterpret_cast<U>(t); // expected-error{{constness}}
  }
};

template struct ReinterpretCast0<void (*)(int), void (*)(float)>;
template struct ReinterpretCast0<int const *, float *>; // expected-note{{instantiation}}

// ---------------------------------------------------------------------
// const_cast
// ---------------------------------------------------------------------
template<typename T, typename U>
struct ConstCast0 {
  void f(T t) {
    (void)const_cast<U>(t); // expected-error{{not allowed}}
  }
};

template struct ConstCast0<int const * *, int * *>;
template struct ConstCast0<int const *, float *>; // expected-note{{instantiation}}

// ---------------------------------------------------------------------
// C++ functional cast
// ---------------------------------------------------------------------
template<typename T, typename U>
struct FunctionalCast1 {
  void f(T t) {
    (void)U(t); // expected-error{{functional-style cast from 'struct A' to 'int'}}
  }
};

template struct FunctionalCast1<int, float>;
template struct FunctionalCast1<A, int>; // expected-note{{instantiation}}

// Generates temporaries, which we cannot handle yet.
template<int N, long M>
struct FunctionalCast2 {
  void f() {
    (void)Constructible(N, M);
  }
};

template struct FunctionalCast2<1, 3>;

// ---------------------------------------------------------------------
// implicit casting
// ---------------------------------------------------------------------
template<typename T>
struct Derived2 : public Base { };

void test_derived_to_base(Base *&bp, Derived2<int> *dp) {
  bp = dp;
}
