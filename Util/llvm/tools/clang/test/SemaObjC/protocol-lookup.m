// RUN: clang -fsyntax-only -verify %s
@protocol NSObject
- retain;
- release;
@end

@interface NSObject
- init;
- dealloc;
@end

@protocol Foo <NSObject>
@end

@protocol Bar <Foo>
@end

@interface Baz : NSObject {
	id <Foo> _foo;
	id <Bar> _bar;
}
- (id)initWithFoo:(id <Foo>)foo bar:(id <Bar>)bar;
@end

@implementation Baz

- (id)init
{
	return [self initWithFoo:0 bar:0];
}

- (id)initWithFoo:(id <Foo>)foo bar:(id <Bar>)bar
{
	self = [super init];
	if (self != 0) {
		_foo = [foo retain];
		_bar = [bar retain];
	}
	return self;
}

- (void)dealloc
{
	[_foo release];
	[_bar release];
	[super dealloc];
}

@end

