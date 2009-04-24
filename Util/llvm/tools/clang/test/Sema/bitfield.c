// RUN: clang %s -fsyntax-only -verify 

struct a {
  int a : -1; // expected-error{{bit-field 'a' has negative width}}

  // rdar://6081627
  int b : 33; // expected-error{{size of bit-field 'b' exceeds size of its type (32 bits)}}

  int c : (1 + 0.25); // expected-error{{expression is not an integer constant expression}}
  int d : (int)(1 + 0.25); 

  // rdar://6138816
  int e : 0;  // expected-error {{bit-field 'e' has zero width}}
};
