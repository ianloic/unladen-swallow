; RUN: llvm-as < %s | \
; RUN:   llc -mtriple=x86_64-pc-linux -relocation-model=pic -o %t1
; RUN: grep {call	f} %t1
; RUN: not grep {call	f@PLT} %t1

define void @g() {
entry:
	call void @f( )
	ret void
}

declare hidden void @f()
