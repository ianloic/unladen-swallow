; RUN: llvm-as < %s | llc -march=thumb -mattr=+thumb2,+v7a | grep {rev\\W*r\[0-9\]*,\\W*r\[0-9\]*} | count 1

define i32 @f1(i32 %a) {
    %tmp = tail call i32 @llvm.bswap.i32(i32 %a)
    ret i32 %tmp
}

declare i32 @llvm.bswap.i32(i32) nounwind readnone
