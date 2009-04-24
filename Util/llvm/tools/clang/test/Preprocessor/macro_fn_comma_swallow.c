// Test the GNU comma swallowing extension.
// RUN: clang %s -E | grep 'foo{A, }' && 
// RUN: clang %s -E | grep 'fo2{A,}' && 
// RUN: clang %s -E | grep '{foo}'

#define X(Y) foo{A, Y}
X()

#define X2(Y) fo2{A,##Y}
X2()

// should eat the comma.
#define X3(b, ...) {b, ## __VA_ARGS__}
X3(foo)


