; RUN: llvm-as < %s | llc -march=x86 -mtriple=i386-linux-gnu | \
; RUN:     grep {movl	%gs:i@NTPOFF, %eax}
; RUN: llvm-as < %s | llc -march=x86 -mtriple=i386-linux-gnu | \
; RUN:     grep {leal	i@NTPOFF(%eax), %eax}
; RUN: llvm-as < %s | llc -march=x86 -mtriple=i386-linux-gnu -relocation-model=pic | \
; RUN:     grep {leal	i@TLSGD(,%ebx,1), %eax}

@i = thread_local global i32 15		; <i32*> [#uses=2]

define i32 @f() {
entry:
	%tmp1 = load i32* @i		; <i32> [#uses=1]
	ret i32 %tmp1
}

define i32* @g() {
entry:
	ret i32* @i
}
