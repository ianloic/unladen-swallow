// RUN: clang -analyze -checker-cfref -analyzer-store-basic -verify %s &&
// RUN: clang -analyze -checker-cfref -analyzer-store-region -verify %s

//===----------------------------------------------------------------------===//
// The following code is reduced using delta-debugging from
// Foundation.h (Mac OS X).
//
// It includes the basic definitions for the test cases below.
// Not directly including Foundation.h directly makes this test case 
// both svelte and portable to non-Mac platforms.
//===----------------------------------------------------------------------===//

typedef const void * CFTypeRef;
typedef const struct __CFString * CFStringRef;
typedef const struct __CFAllocator * CFAllocatorRef;
extern const CFAllocatorRef kCFAllocatorDefault;
extern CFTypeRef CFRetain(CFTypeRef cf);
typedef const struct __CFDictionary * CFDictionaryRef;
const void *CFDictionaryGetValue(CFDictionaryRef theDict, const void *key);
extern CFStringRef CFStringCreateWithFormat(CFAllocatorRef alloc, CFDictionaryRef formatOptions, CFStringRef format, ...);
typedef signed char BOOL;
typedef int NSInteger;
typedef unsigned int NSUInteger;
@class NSString, Protocol;
extern void NSLog(NSString *format, ...) __attribute__((format(__NSString__, 1, 2)));
typedef NSInteger NSComparisonResult;
typedef struct _NSZone NSZone;
@class NSInvocation, NSMethodSignature, NSCoder, NSString, NSEnumerator;
@protocol NSObject
- (BOOL)isEqual:(id)object;
- (oneway void)release;
- (id)retain;
@end
@protocol NSCopying
- (id)copyWithZone:(NSZone *)zone;
@end
@protocol NSMutableCopying
- (id)mutableCopyWithZone:(NSZone *)zone;
@end
@protocol NSCoding
- (void)encodeWithCoder:(NSCoder *)aCoder;
@end
@interface NSObject <NSObject> {}
- (id)init;
+ (id)alloc;
@end
extern id NSAllocateObject(Class aClass, NSUInteger extraBytes, NSZone *zone);
typedef struct {} NSFastEnumerationState;
@protocol NSFastEnumeration
- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState *)state objects:(id *)stackbuf count:(NSUInteger)len;
@end
@class NSString;
typedef struct _NSRange {} NSRange;
@interface NSArray : NSObject <NSCopying, NSMutableCopying, NSCoding, NSFastEnumeration>
- (NSUInteger)count;
@end
@interface NSMutableArray : NSArray
- (void)addObject:(id)anObject;
- (id)initWithCapacity:(NSUInteger)numItems;
@end
typedef unsigned short unichar;
@class NSData, NSArray, NSDictionary, NSCharacterSet, NSData, NSURL, NSError, NSLocale;
typedef NSUInteger NSStringCompareOptions;
@interface NSString : NSObject <NSCopying, NSMutableCopying, NSCoding>    - (NSUInteger)length;
- (NSComparisonResult)compare:(NSString *)string;
- (NSComparisonResult)compare:(NSString *)string options:(NSStringCompareOptions)mask;
- (NSComparisonResult)compare:(NSString *)string options:(NSStringCompareOptions)mask range:(NSRange)compareRange;
- (NSComparisonResult)compare:(NSString *)string options:(NSStringCompareOptions)mask range:(NSRange)compareRange locale:(id)locale;
- (NSComparisonResult)caseInsensitiveCompare:(NSString *)string;
- (NSArray *)componentsSeparatedByCharactersInSet:(NSCharacterSet *)separator;
@end
@interface NSSimpleCString : NSString {} @end
@interface NSConstantString : NSSimpleCString @end
extern void *_NSConstantStringClassReference;

//===----------------------------------------------------------------------===//
// Test cases.
//===----------------------------------------------------------------------===//

NSComparisonResult f1(NSString* s) {
  NSString *aString = 0;
  return [s compare:aString]; // expected-warning {{Argument to 'NSString' method 'compare:' cannot be nil.}}
}

NSComparisonResult f2(NSString* s) {
  NSString *aString = 0;
  return [s caseInsensitiveCompare:aString]; // expected-warning {{Argument to 'NSString' method 'caseInsensitiveCompare:' cannot be nil.}}
}

NSComparisonResult f3(NSString* s, NSStringCompareOptions op) {
  NSString *aString = 0;
  return [s compare:aString options:op]; // expected-warning {{Argument to 'NSString' method 'compare:options:' cannot be nil.}}
}

NSComparisonResult f4(NSString* s, NSStringCompareOptions op, NSRange R) {
  NSString *aString = 0;
  return [s compare:aString options:op range:R]; // expected-warning {{Argument to 'NSString' method 'compare:options:range:' cannot be nil.}}
}

NSComparisonResult f5(NSString* s, NSStringCompareOptions op, NSRange R) {
  NSString *aString = 0;
  return [s compare:aString options:op range:R locale:0]; // expected-warning {{Argument to 'NSString' method 'compare:options:range:locale:' cannot be nil.}}
}

NSArray *f6(NSString* s) {
  return [s componentsSeparatedByCharactersInSet:0]; // expected-warning {{Argument to 'NSString' method 'componentsSeparatedByCharactersInSet:' cannot be nil.}}
}

NSString* f7(NSString* s1, NSString* s2, NSString* s3) {

  NSString* s4 = (NSString*)
    CFStringCreateWithFormat(kCFAllocatorDefault, 0,
                             (CFStringRef) __builtin___CFStringMakeConstantString("%@ %@ (%@)"), 
                             s1, s2, s3);

  CFRetain(s4);
  return s4; // expected-warning{{leak}}
}

NSMutableArray* f8() {
  
  NSString* s = [[NSString alloc] init];
  NSMutableArray* a = [[NSMutableArray alloc] initWithCapacity:2];
  [a addObject:s];
  [s release]; // no-warning
  return a;
}

void f9() {
  
  NSString* s = [[NSString alloc] init];
  NSString* q = s;
  [s release];
  [q release]; // expected-warning {{used after it is released}}
}

NSString* f10() {
  static NSString* s = 0;
  if (!s) s = [[NSString alloc] init];
  return s; // no-warning
}

// Test case for regression reported in <rdar://problem/6452745>.
// Essentially 's' should not be considered allocated on the false branch.
// This exercises the 'EvalAssume' logic in GRTransferFuncs (CFRefCount.cpp).
NSString* f11(CFDictionaryRef dict, const char* key) {
  NSString* s = (NSString*) CFDictionaryGetValue(dict, key);
  [s retain];
  if (s) {
    [s release];
  }
}

// Test case for passing a tracked object by-reference to a function we
// don't undersand.
void unknown_function_f12(NSString** s);
void f12() {
  NSString *string = [[NSString alloc] init];
  unknown_function_f12(&string); // no-warning
}


@interface C1 : NSObject {}
- (NSString*) getShared;
+ (C1*) sharedInstance;
@end
@implementation C1 : NSObject {}
- (NSString*) getShared {
  static NSString* s = 0;
  if (!s) s = [[NSString alloc] init];    
  return s; // no-warning  
}
+ (C1 *)sharedInstance {
  static C1 *sharedInstance = 0;
  if (!sharedInstance) {
    sharedInstance = [[C1 alloc] init];
  }
  return sharedInstance; // no-warning
}
@end

@interface SharedClass : NSObject
+ (id)sharedInstance;
- (id)notShared;
@end

@implementation SharedClass

- (id)_init {
    if ((self = [super init])) {
        NSLog(@"Bar");
    }
    return self;
}

- (id)notShared {
  return [[SharedClass alloc] _init]; // expected-warning{{[naming convention] leak of returned object}}
}

+ (id)sharedInstance {
    static SharedClass *_sharedInstance = 0;
    if (!_sharedInstance) {
        _sharedInstance = [[SharedClass alloc] _init];
    }
    return _sharedInstance; // no-warning
}
@end

id testSharedClassFromFunction() {
  return [[SharedClass alloc] _init]; // no-warning
}

