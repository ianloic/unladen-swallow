// RUN: clang %s -fsyntax-only -verify -fms-extensions

void f() throw(...) { }
