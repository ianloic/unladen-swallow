// RUN: clang -fsyntax-only -verify %s

typedef int i16_1 __attribute((mode(HI)));
int i16_1_test[sizeof(i16_1) == 2 ? 1 : -1];
typedef int i16_2 __attribute((__mode__(__HI__)));
int i16_2_test[sizeof(i16_1) == 2 ? 1 : -1];

typedef float f64 __attribute((mode(DF)));
int f64_test[sizeof(f64) == 8 ? 1 : -1];

typedef int invalid_1 __attribute((mode)); // expected-error{{attribute requires unquoted parameter}}
typedef int invalid_2 __attribute((mode())); // expected-error{{attribute requires unquoted parameter}}
typedef int invalid_3 __attribute((mode(II))); // expected-error{{unknown machine mode}}
typedef struct {int i,j,k;} invalid_4 __attribute((mode(SI))); // expected-error{{mode attribute only supported for integer and floating-point types}}
typedef float invalid_5 __attribute((mode(SI))); // expected-error{{type of machine mode does not match type of base type}}

int **__attribute((mode(QI)))* i32;  // expected-error{{mode attribute}}
