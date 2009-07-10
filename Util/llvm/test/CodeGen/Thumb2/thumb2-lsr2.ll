; RUN: llvm-as < %s | llc -march=thumb -mattr=+thumb2 | grep {lsr\\W*r\[0-9\],\\W*r\[0-9\],\\W*r\[0-9\]} | count 1

define i32 @f1(i32 %a, i32 %b) {
    %tmp = lshr i32 %a, %b
    ret i32 %tmp
}
