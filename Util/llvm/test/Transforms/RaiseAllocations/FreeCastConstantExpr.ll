; This situation can occur due to the funcresolve pass.
;
; RUN: opt < %s -raiseallocs -S | not grep call

declare void @free(i8*)

define void @test(i32* %P) {
	call void bitcast (void (i8*)* @free to void (i32*)*)( i32* %P )
	ret void
}

