// RUN: clang-cc -fsyntax-only -verify %s

@interface Sprite {
  int sprite, spree;
  int UseGlobalBar;
}
+ (void)setFoo:(int)foo;
+ (void)setSprite:(int)sprite;
- (void)setFoo:(int)foo;
- (void)setSprite:(int)sprite;
@end

int spree = 23;
int UseGlobalBar;

@implementation Sprite
+ (void)setFoo:(int)foo {
  sprite = foo;   // expected-error {{use of undeclared identifier 'sprite'}}
  spree = foo;
  Xsprite = foo; // expected-error {{use of undeclared identifier 'Xsprite'}} 
  UseGlobalBar = 10;
}
+ (void)setSprite:(int)sprite {
  int spree;
  sprite = 15;
  spree = 17;
  ((Sprite *)self)->sprite = 16;   /* NB: This is how one _should_ access */
  ((Sprite *)self)->spree = 18;    /* ivars from within class methods!    */
}
- (void)setFoo:(int)foo {
  sprite = foo;
  spree = foo;
}
- (void)setSprite:(int)sprite {
  int spree;
  sprite = 15;  // expected-warning {{local declaration of 'sprite' hides instance variable}}
  self->sprite = 16;
  spree = 17;  // expected-warning {{local declaration of 'spree' hides instance variable}}
  self->spree = 18;
}   
@end
