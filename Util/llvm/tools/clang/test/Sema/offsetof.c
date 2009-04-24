// RUN: clang -fsyntax-only -verify %s

#define offsetof(TYPE, MEMBER) __builtin_offsetof (TYPE, MEMBER)

typedef struct P { int i; float f; } PT;
struct external_sun3_core
{
 unsigned c_regs; 

  PT  X[100];
  
};

void swap()
{
  int x;
  x = offsetof(struct external_sun3_core, c_regs);
  x = __builtin_offsetof(struct external_sun3_core, X[42].f);
  
  x = __builtin_offsetof(struct external_sun3_core, X[42].f2);  // expected-error {{no member named 'f2'}}
  x = __builtin_offsetof(int, X[42].f2);  // expected-error {{offsetof requires struct}}
  
  int a[__builtin_offsetof(struct external_sun3_core, X) == 4 ? 1 : -1];
  int b[__builtin_offsetof(struct external_sun3_core, X[42]) == 340 ? 1 : -1];
  int c[__builtin_offsetof(struct external_sun3_core, X[42].f2) == 344 ? 1 : -1];  // expected-error {{no member named 'f2'}}
}    

extern int f();

struct s1 { int a; }; 
int v1 = offsetof (struct s1, a) == 0 ? 0 : f();

struct s2 { int a; }; 
int v2 = (int)(&((struct s2 *) 0)->a) == 0 ? 0 : f();

struct s3 { int a; }; 
int v3 = __builtin_offsetof(struct s3, a) == 0 ? 0 : f();
