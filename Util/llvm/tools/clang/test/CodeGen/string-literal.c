// RUN: clang-cc -emit-llvm %s -o -

int main() {
  char a[10] = "abc";

  void *foo = L"AB";
}
