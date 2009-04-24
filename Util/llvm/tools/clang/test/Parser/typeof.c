// RUN: clang -fsyntax-only -verify %s

typedef int TInt;

static void test() {
  int *pi;

  int typeof (int) aIntInt; // expected-error{{cannot combine with previous 'int' declaration specifier}}
  short typeof (int) aShortInt; // expected-error{{'short typeof' is invalid}} 
  int int ttt; // expected-error{{cannot combine with previous 'int' declaration specifier}}
  typeof(TInt) anInt; 
  short TInt eee; // expected-error{{parse error}}
  void ary[7] fff; // expected-error{{array has incomplete element type 'void'}} expected-error{{parse error}}
  typeof(void ary[7]) anIntError; // expected-error{{expected ')'}} expected-note {{to match this '('}}
  typeof(const int) aci; 
  const typeof (*pi) aConstInt; 
  int xx;
  int *i;
}
