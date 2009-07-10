; RUN: llvm-as < %s | llc -march=thumb -mattr=+thumb2 | grep moveq | count 1
; RUN: llvm-as < %s | llc -march=thumb -mattr=+thumb2 | grep movgt | count 1
; RUN: llvm-as < %s | llc -march=thumb -mattr=+thumb2 | grep movlt | count 1
; RUN: llvm-as < %s | llc -march=thumb -mattr=+thumb2 | grep movle | count 1
; RUN: llvm-as < %s | llc -march=thumb -mattr=+thumb2 | grep movls | count 1
; RUN: llvm-as < %s | llc -march=thumb -mattr=+thumb2 | grep movhi | count 1

define i32 @f1(i32 %a.s) {
entry:
    %tmp = icmp eq i32 %a.s, 4
    %tmp1.s = select i1 %tmp, i32 2, i32 3
    ret i32 %tmp1.s
}

define i32 @f2(i32 %a.s) {
entry:
    %tmp = icmp sgt i32 %a.s, 4
    %tmp1.s = select i1 %tmp, i32 2, i32 3
    ret i32 %tmp1.s
}

define i32 @f3(i32 %a.s, i32 %b.s) {
entry:
    %tmp = icmp slt i32 %a.s, %b.s
    %tmp1.s = select i1 %tmp, i32 2, i32 3
    ret i32 %tmp1.s
}

define i32 @f4(i32 %a.s, i32 %b.s) {
entry:
    %tmp = icmp sle i32 %a.s, %b.s
    %tmp1.s = select i1 %tmp, i32 2, i32 3
    ret i32 %tmp1.s
}

define i32 @f5(i32 %a.u, i32 %b.u) {
entry:
    %tmp = icmp ule i32 %a.u, %b.u
    %tmp1.s = select i1 %tmp, i32 2, i32 3
    ret i32 %tmp1.s
}

define i32 @f6(i32 %a.u, i32 %b.u) {
entry:
    %tmp = icmp ugt i32 %a.u, %b.u
    %tmp1.s = select i1 %tmp, i32 2, i32 3
    ret i32 %tmp1.s
}
