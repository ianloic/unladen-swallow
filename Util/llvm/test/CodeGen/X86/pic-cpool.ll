; RUN: llvm-as < %s | llc -mtriple=i686-pc-linux-gnu -relocation-model=pic \
; RUN:   -o %t
; RUN: grep _GLOBAL_OFFSET_TABLE_ %t
; RUN: grep piclabel %t | count 3
; RUN: grep GOTOFF %t | count 1
; RUN: grep CPI %t | count 2

define double @foo(i32 %a.u) nounwind {
entry:
    %tmp = icmp eq i32 %a.u,0
    %retval = select i1 %tmp, double 4.561230e+02, double 1.234560e+02
    ret double %retval
}

