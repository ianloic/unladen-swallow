; RUN: opt < %s -globalopt -stats -disable-output |& grep "1 globalopt - Number of global vars shrunk to booleans"
; XFAIL: *

	type { }		; type %0
	%llvm.dbg.anchor.type = type { i32, i32 }
	%llvm.dbg.basictype.type = type { i32, %0*, i8*, %0*, i32, i64, i64, i64, i32, i32 }
	%llvm.dbg.compile_unit.type = type { i32, %0*, i32, i8*, i8*, i8*, i1, i1, i8*, i32 }
	%llvm.dbg.composite.type = type { i32, %0*, i8*, %0*, i32, i64, i64, i64, i32, %0*, %0*, i32 }
	%llvm.dbg.global_variable.type = type { i32, %0*, %0*, i8*, i8*, i8*, %0*, i32, %0*, i1, i1, %0* }
	%llvm.dbg.subprogram.type = type { i32, %0*, %0*, i8*, i8*, i8*, %0*, i32, %0*, i1, i1 }
	%llvm.dbg.variable.type = type { i32, %0*, i8*, %0*, i32, %0* }
@llvm.dbg.compile_units = linkonce constant %llvm.dbg.anchor.type { i32 458752, i32 17 }, section "llvm.metadata"		; <%llvm.dbg.anchor.type*> [#uses=1]
@.str = internal constant [5 x i8] c"gs.c\00", section "llvm.metadata"		; <[5 x i8]*> [#uses=1]
@.str1 = internal constant [6 x i8] c"/tmp/\00", section "llvm.metadata"		; <[6 x i8]*> [#uses=1]
@.str2 = internal constant [55 x i8] c"4.2.1 (Based on Apple Inc. build 5641) (LLVM build 00)\00", section "llvm.metadata"		; <[55 x i8]*> [#uses=1]
@llvm.dbg.compile_unit = internal constant %llvm.dbg.compile_unit.type { i32 458769, %0* bitcast (%llvm.dbg.anchor.type* @llvm.dbg.compile_units to %0*), i32 1, i8* getelementptr ([5 x i8]* @.str, i32 0, i32 0), i8* getelementptr ([6 x i8]* @.str1, i32 0, i32 0), i8* getelementptr ([55 x i8]* @.str2, i32 0, i32 0), i1 true, i1 false, i8* null, i32 0 }, section "llvm.metadata"		; <%llvm.dbg.compile_unit.type*> [#uses=1]
@.str3 = internal constant [4 x i8] c"int\00", section "llvm.metadata"		; <[4 x i8]*> [#uses=1]
@llvm.dbg.basictype = internal constant %llvm.dbg.basictype.type { i32 458788, %0* bitcast (%llvm.dbg.compile_unit.type* @llvm.dbg.compile_unit to %0*), i8* getelementptr ([4 x i8]* @.str3, i32 0, i32 0), %0* bitcast (%llvm.dbg.compile_unit.type* @llvm.dbg.compile_unit to %0*), i32 0, i64 32, i64 32, i64 0, i32 0, i32 5 }, section "llvm.metadata"		; <%llvm.dbg.basictype.type*> [#uses=1]
@llvm.dbg.array = internal constant [2 x %0*] [%0* bitcast (%llvm.dbg.basictype.type* @llvm.dbg.basictype to %0*), %0* bitcast (%llvm.dbg.basictype.type* @llvm.dbg.basictype to %0*)], section "llvm.metadata"		; <[2 x %0*]*> [#uses=1]
@llvm.dbg.composite = internal constant %llvm.dbg.composite.type { i32 458773, %0* bitcast (%llvm.dbg.compile_unit.type* @llvm.dbg.compile_unit to %0*), i8* null, %0* bitcast (%llvm.dbg.compile_unit.type* @llvm.dbg.compile_unit to %0*), i32 0, i64 0, i64 0, i64 0, i32 0, %0* null, %0* bitcast ([2 x %0*]* @llvm.dbg.array to %0*), i32 0 }, section "llvm.metadata"		; <%llvm.dbg.composite.type*> [#uses=1]
@llvm.dbg.subprograms = linkonce constant %llvm.dbg.anchor.type { i32 458752, i32 46 }, section "llvm.metadata"		; <%llvm.dbg.anchor.type*> [#uses=1]
@.str4 = internal constant [4 x i8] c"foo\00", section "llvm.metadata"		; <[4 x i8]*> [#uses=1]
@llvm.dbg.subprogram = internal constant %llvm.dbg.subprogram.type { i32 458798, %0* bitcast (%llvm.dbg.anchor.type* @llvm.dbg.subprograms to %0*), %0* bitcast (%llvm.dbg.compile_unit.type* @llvm.dbg.compile_unit to %0*), i8* getelementptr ([4 x i8]* @.str4, i32 0, i32 0), i8* getelementptr ([4 x i8]* @.str4, i32 0, i32 0), i8* null, %0* bitcast (%llvm.dbg.compile_unit.type* @llvm.dbg.compile_unit to %0*), i32 4, %0* bitcast (%llvm.dbg.composite.type* @llvm.dbg.composite to %0*), i1 false, i1 true }, section "llvm.metadata"		; <%llvm.dbg.subprogram.type*> [#uses=1]
@.str5 = internal constant [2 x i8] c"i\00", section "llvm.metadata"		; <[2 x i8]*> [#uses=1]
@llvm.dbg.variable = internal constant %llvm.dbg.variable.type { i32 459009, %0* bitcast (%llvm.dbg.subprogram.type* @llvm.dbg.subprogram to %0*), i8* getelementptr ([2 x i8]* @.str5, i32 0, i32 0), %0* bitcast (%llvm.dbg.compile_unit.type* @llvm.dbg.compile_unit to %0*), i32 4, %0* bitcast (%llvm.dbg.basictype.type* @llvm.dbg.basictype to %0*) }, section "llvm.metadata"		; <%llvm.dbg.variable.type*> [#uses=1]
@Stop = internal global i32 0		; <i32*> [#uses=4]
@llvm.dbg.global_variables = linkonce constant %llvm.dbg.anchor.type { i32 458752, i32 52 }, section "llvm.metadata"		; <%llvm.dbg.anchor.type*> [#uses=1]
@.str6 = internal constant [5 x i8] c"Stop\00", section "llvm.metadata"		; <[5 x i8]*> [#uses=1]
@llvm.dbg.global_variable = internal constant %llvm.dbg.global_variable.type { i32 458804, %0* bitcast (%llvm.dbg.anchor.type* @llvm.dbg.global_variables to %0*), %0* bitcast (%llvm.dbg.compile_unit.type* @llvm.dbg.compile_unit to %0*), i8* getelementptr ([5 x i8]* @.str6, i32 0, i32 0), i8* getelementptr ([5 x i8]* @.str6, i32 0, i32 0), i8* null, %0* bitcast (%llvm.dbg.compile_unit.type* @llvm.dbg.compile_unit to %0*), i32 2, %0* bitcast (%llvm.dbg.basictype.type* @llvm.dbg.basictype to %0*), i1 true, i1 true, %0* bitcast (i32* @Stop to %0*) }, section "llvm.metadata"		; <%llvm.dbg.global_variable.type*> [#uses=0]

define i32 @foo(i32 %i) nounwind {
entry:
	%"alloca point" = bitcast i32 0 to i32		; <i32> [#uses=0]
	call void @llvm.dbg.func.start(%0* bitcast (%llvm.dbg.subprogram.type* @llvm.dbg.subprogram to %0*))
	call void @llvm.dbg.stoppoint(i32 5, i32 0, %0* bitcast (%llvm.dbg.compile_unit.type* @llvm.dbg.compile_unit to %0*))
	%0 = load i32* @Stop, align 4		; <i32> [#uses=1]
	%1 = icmp eq i32 %0, 1		; <i1> [#uses=1]
	br i1 %1, label %bb, label %bb1

bb:		; preds = %entry
	call void @llvm.dbg.stoppoint(i32 6, i32 0, %0* bitcast (%llvm.dbg.compile_unit.type* @llvm.dbg.compile_unit to %0*))
	store i32 0, i32* @Stop, align 4
	call void @llvm.dbg.stoppoint(i32 7, i32 0, %0* bitcast (%llvm.dbg.compile_unit.type* @llvm.dbg.compile_unit to %0*))
	%2 = mul i32 %i, 42		; <i32> [#uses=1]
	br label %bb2

bb1:		; preds = %entry
	call void @llvm.dbg.stoppoint(i32 9, i32 0, %0* bitcast (%llvm.dbg.compile_unit.type* @llvm.dbg.compile_unit to %0*))
	store i32 1, i32* @Stop, align 4
	call void @llvm.dbg.stoppoint(i32 10, i32 0, %0* bitcast (%llvm.dbg.compile_unit.type* @llvm.dbg.compile_unit to %0*))
	br label %bb2

bb2:		; preds = %bb1, %bb
	%.0 = phi i32 [ %i, %bb1 ], [ %2, %bb ]		; <i32> [#uses=1]
	call void @llvm.dbg.stoppoint(i32 10, i32 0, %0* bitcast (%llvm.dbg.compile_unit.type* @llvm.dbg.compile_unit to %0*))
	call void @llvm.dbg.stoppoint(i32 10, i32 0, %0* bitcast (%llvm.dbg.compile_unit.type* @llvm.dbg.compile_unit to %0*))
	call void @llvm.dbg.region.end(%0* bitcast (%llvm.dbg.subprogram.type* @llvm.dbg.subprogram to %0*))
	ret i32 %.0
}

declare void @llvm.dbg.func.start(%0*) nounwind readnone

declare void @llvm.dbg.declare(%0*, %0*) nounwind readnone

declare void @llvm.dbg.stoppoint(i32, i32, %0*) nounwind readnone

declare void @llvm.dbg.region.end(%0*) nounwind readnone
