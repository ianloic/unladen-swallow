// RUN: clang -fsyntax-only -verify %s 
class Z { };

class Y { 
public:
  Y(const Z&);
};

class X {
public:
  X(int);
  X(const Y&);
};

void f(X);

void g(short s, Y y, Z z) {
  f(s);
  f(1.0f);
  f(y);
  f(z); // expected-error{{incompatible type passing 'class Z', expected 'class X'}}
}


class FromShort {
public:
  FromShort(short s);
};

class FromShortExplicitly {
public:
  explicit FromShortExplicitly(short s);
};

void explicit_constructor(short s) {
  FromShort fs1(s);
  FromShort fs2 = s;
  FromShortExplicitly fse1(s);
  FromShortExplicitly fse2 = s; // expected-error{{error: cannot initialize 'fse2' with an lvalue of type 'short'}}
}
