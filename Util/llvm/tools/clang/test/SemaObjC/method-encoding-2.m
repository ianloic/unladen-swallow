// RUN: clang %s
// TODO: We don't support rewrite of method definitions

@interface Intf 
- (in out bycopy id) address:(byref inout void *)location with:(out oneway unsigned **)arg2;
- (id) another:(void *)location with:(unsigned **)arg2;
@end

@implementation Intf
- (in out bycopy id) address:(byref inout void *)location with:(out oneway unsigned **)arg2{}
- (id) another:(void *)location with:(unsigned **)arg2 {}
@end
