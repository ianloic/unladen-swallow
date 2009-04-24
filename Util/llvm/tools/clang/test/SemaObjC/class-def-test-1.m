// RUN: clang -fsyntax-only -verify %s

@protocol SUPER;

@interface SUPER <SUPER> @end // expected-warning {{cannot find protocol definition for 'SUPER'}}

typedef int INTF; //  expected-note {{previous definition is here}}

@interface INTF @end // expected-error {{redefinition of 'INTF' as different kind of symbol}}

@interface OBJECT @end	// expected-note {{previous definition is here}}

@interface INTF1 : OBJECT @end // expected-note {{previous definition is here}}

@interface INTF1 : OBJECT @end // expected-error {{duplicate interface definition for class 'INTF1'}}

typedef int OBJECT; // expected-note {{previous definition is here}}  \
		       expected-error {{redefinition of 'OBJECT' as different kind of symbol}}

@interface INTF2 : OBJECT @end // expected-error {{redefinition of 'OBJECT' as different kind of symbol}}


@protocol PROTO;

@interface INTF3 : PROTO @end // expected-error {{cannot find interface declaration for 'PROTO', superclass of 'INTF3'}}

