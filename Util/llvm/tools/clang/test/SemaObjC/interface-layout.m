// RUN: clang-cc %s -fsyntax-only -verify  -triple i386-apple-darwin9
typedef struct objc_object {} *id;
typedef signed char BOOL;
typedef unsigned int NSUInteger;
typedef struct _NSZone NSZone;

@protocol NSObject
- (BOOL) isEqual:(id) object;
@end

@protocol NSCopying
- (id) copyWithZone:(NSZone *) zone;
@end

@interface NSObject < NSObject > {}
@end

extern id NSAllocateObject (Class aClass, NSUInteger extraBytes, NSZone * zone);

@interface MyClassBase : NSObject < NSCopying > {}
@end

@interface MyClassDirectNode : MyClassBase < NSCopying >
{
  @public NSUInteger attributeRuns[((1024 - 16 - sizeof (MyClassBase)) / (sizeof (NSUInteger) + sizeof (void *)))];
}
@end
