// RUN: clang %s -verify -fms-extensions

#define M __asm int 0x2c
#define M2 int

void t1(void) { M }
void t2(void) { __asm int 0x2c }
void t3(void) { __asm M2 0x2c } 

