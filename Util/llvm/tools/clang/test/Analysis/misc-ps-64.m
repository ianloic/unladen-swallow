// RUN: clang-cc -triple x86_64-apple-darwin9 -analyze -checker-cfref --analyzer-store=basic -analyzer-constraints=basic --verify -fblocks %s &&
// RUN: clang-cc -triple x86_64-apple-darwin9 -analyze -checker-cfref --analyzer-store=basic -analyzer-constraints=range --verify -fblocks %s &&
// RUN: clang-cc -triple x86_64-apple-darwin9 -analyze -checker-cfref --analyzer-store=region -analyzer-constraints=basic --verify -fblocks %s &&
// RUN: clang-cc -triple x86_64-apple-darwin9 -analyze -checker-cfref --analyzer-store=region -analyzer-constraints=range --verify -fblocks %s

// <rdar://problem/6440393> - A bunch of misc. failures involving evaluating
//  these expressions and building CFGs.  These tests are here to prevent
//  regressions.
@class NSString, NSDictionary;
typedef long NSInteger;
typedef unsigned long NSUInteger;
typedef unsigned char Boolean;
typedef const struct __CFDictionary * CFDictionaryRef;

extern Boolean CFDictionaryGetValueIfPresent(CFDictionaryRef theDict, const void *key, const void **value);
static void shazam(NSUInteger i, unsigned char **out);

void rdar_6440393_1(NSDictionary *dict) {
  NSInteger x = 0;
  unsigned char buf[10], *bufptr = buf;
  if (!CFDictionaryGetValueIfPresent(0, dict, (void *)&x))
    return;
  shazam(x, &bufptr);
}

