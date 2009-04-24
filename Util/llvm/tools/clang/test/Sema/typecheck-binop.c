/* RUN: clang %s -fsyntax-only -pedantic -verify
 */
struct incomplete;

int sub1(int *a, double *b) { 
  return a - b;    /* expected-error{{not pointers to compatible types}} */
}

void *sub2(struct incomplete *P) {
  return P-4;      /* expected-error{{subtraction of pointer 'struct incomplete *' requires pointee to be a complete object type}} */
}

void *sub3(void *P) {
  return P-4;      /* expected-warning{{GNU void* extension}} */
}

int sub4(void *P, void *Q) {
  return P-Q;      /* expected-warning{{GNU void* extension}} */
}

int logicaland1(int a) {
  return a && (void)a; /* expected-error{{invalid operands}} */
}
