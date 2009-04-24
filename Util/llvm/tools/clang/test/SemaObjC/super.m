// RUN: clang -fsyntax-only -verify %s

@interface Foo
- iMethod;
+ cMethod;
@end

@interface A
@end

@interface B : A
- (void)instanceMethod;
+ classMethod;
@end

@implementation B

- (void)instanceMethod {
  [super iMethod]; // expected-warning{{method '-iMethod' not found (return type defaults to 'id')}}
}

+ classMethod {
  [super cMethod]; // expected-warning{{method '+cMethod' not found (return type defaults to 'id')}}
}
@end

@interface XX
- m;
@end

void f(id super) {
  [super m];
}
void f0(int super) {
  [super m]; // expected-error{{bad receiver type 'int'}}
}
void f1(int puper) {
  [super m]; // expected-error{{use of undeclared identifier 'super'}}
}
