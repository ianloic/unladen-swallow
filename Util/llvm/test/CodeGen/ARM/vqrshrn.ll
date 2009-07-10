; RUN: llvm-as < %s | llc -march=arm -mattr=+neon > %t
; RUN: grep {vqrshrn\\.s16} %t | count 1
; RUN: grep {vqrshrn\\.s32} %t | count 1
; RUN: grep {vqrshrn\\.s64} %t | count 1
; RUN: grep {vqrshrn\\.u16} %t | count 1
; RUN: grep {vqrshrn\\.u32} %t | count 1
; RUN: grep {vqrshrn\\.u64} %t | count 1
; RUN: grep {vqrshrun\\.s16} %t | count 1
; RUN: grep {vqrshrun\\.s32} %t | count 1
; RUN: grep {vqrshrun\\.s64} %t | count 1

define <8 x i8> @vqrshrns8(<8 x i16>* %A) nounwind {
	%tmp1 = load <8 x i16>* %A
	%tmp2 = call <8 x i8> @llvm.arm.neon.vqrshiftns.v8i8(<8 x i16> %tmp1, <8 x i16> < i16 -8, i16 -8, i16 -8, i16 -8, i16 -8, i16 -8, i16 -8, i16 -8 >)
	ret <8 x i8> %tmp2
}

define <4 x i16> @vqrshrns16(<4 x i32>* %A) nounwind {
	%tmp1 = load <4 x i32>* %A
	%tmp2 = call <4 x i16> @llvm.arm.neon.vqrshiftns.v4i16(<4 x i32> %tmp1, <4 x i32> < i32 -16, i32 -16, i32 -16, i32 -16 >)
	ret <4 x i16> %tmp2
}

define <2 x i32> @vqrshrns32(<2 x i64>* %A) nounwind {
	%tmp1 = load <2 x i64>* %A
	%tmp2 = call <2 x i32> @llvm.arm.neon.vqrshiftns.v2i32(<2 x i64> %tmp1, <2 x i64> < i64 -32, i64 -32 >)
	ret <2 x i32> %tmp2
}

define <8 x i8> @vqrshrnu8(<8 x i16>* %A) nounwind {
	%tmp1 = load <8 x i16>* %A
	%tmp2 = call <8 x i8> @llvm.arm.neon.vqrshiftnu.v8i8(<8 x i16> %tmp1, <8 x i16> < i16 -8, i16 -8, i16 -8, i16 -8, i16 -8, i16 -8, i16 -8, i16 -8 >)
	ret <8 x i8> %tmp2
}

define <4 x i16> @vqrshrnu16(<4 x i32>* %A) nounwind {
	%tmp1 = load <4 x i32>* %A
	%tmp2 = call <4 x i16> @llvm.arm.neon.vqrshiftnu.v4i16(<4 x i32> %tmp1, <4 x i32> < i32 -16, i32 -16, i32 -16, i32 -16 >)
	ret <4 x i16> %tmp2
}

define <2 x i32> @vqrshrnu32(<2 x i64>* %A) nounwind {
	%tmp1 = load <2 x i64>* %A
	%tmp2 = call <2 x i32> @llvm.arm.neon.vqrshiftnu.v2i32(<2 x i64> %tmp1, <2 x i64> < i64 -32, i64 -32 >)
	ret <2 x i32> %tmp2
}

define <8 x i8> @vqrshruns8(<8 x i16>* %A) nounwind {
	%tmp1 = load <8 x i16>* %A
	%tmp2 = call <8 x i8> @llvm.arm.neon.vqrshiftnsu.v8i8(<8 x i16> %tmp1, <8 x i16> < i16 -8, i16 -8, i16 -8, i16 -8, i16 -8, i16 -8, i16 -8, i16 -8 >)
	ret <8 x i8> %tmp2
}

define <4 x i16> @vqrshruns16(<4 x i32>* %A) nounwind {
	%tmp1 = load <4 x i32>* %A
	%tmp2 = call <4 x i16> @llvm.arm.neon.vqrshiftnsu.v4i16(<4 x i32> %tmp1, <4 x i32> < i32 -16, i32 -16, i32 -16, i32 -16 >)
	ret <4 x i16> %tmp2
}

define <2 x i32> @vqrshruns32(<2 x i64>* %A) nounwind {
	%tmp1 = load <2 x i64>* %A
	%tmp2 = call <2 x i32> @llvm.arm.neon.vqrshiftnsu.v2i32(<2 x i64> %tmp1, <2 x i64> < i64 -32, i64 -32 >)
	ret <2 x i32> %tmp2
}

declare <8 x i8>  @llvm.arm.neon.vqrshiftns.v8i8(<8 x i16>, <8 x i16>) nounwind readnone
declare <4 x i16> @llvm.arm.neon.vqrshiftns.v4i16(<4 x i32>, <4 x i32>) nounwind readnone
declare <2 x i32> @llvm.arm.neon.vqrshiftns.v2i32(<2 x i64>, <2 x i64>) nounwind readnone

declare <8 x i8>  @llvm.arm.neon.vqrshiftnu.v8i8(<8 x i16>, <8 x i16>) nounwind readnone
declare <4 x i16> @llvm.arm.neon.vqrshiftnu.v4i16(<4 x i32>, <4 x i32>) nounwind readnone
declare <2 x i32> @llvm.arm.neon.vqrshiftnu.v2i32(<2 x i64>, <2 x i64>) nounwind readnone

declare <8 x i8>  @llvm.arm.neon.vqrshiftnsu.v8i8(<8 x i16>, <8 x i16>) nounwind readnone
declare <4 x i16> @llvm.arm.neon.vqrshiftnsu.v4i16(<4 x i32>, <4 x i32>) nounwind readnone
declare <2 x i32> @llvm.arm.neon.vqrshiftnsu.v2i32(<2 x i64>, <2 x i64>) nounwind readnone
