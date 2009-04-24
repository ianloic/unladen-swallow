// RUN: clang %s -fsyntax-only -verify

typedef __attribute__(( ext_vector_type(4) ))  float float4;
//typedef float float4 __attribute__((vector_size(16)));

float4 foo = (float4){ 1.0, 2.0, 3.0, 4.0 };

float4 array[] = { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0};
int array_sizecheck[(sizeof(array) / sizeof(float4)) == 3? 1 : -1];

float4 array2[2] = { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 
                     9.0 }; // expected-error {{excess elements in array initializer}}

float4 array3[2] = { {1.0, 2.0, 3.0}, 5.0, 6.0, 7.0, 8.0,
                     9.0 }; // expected-error {{excess elements in array initializer}}
