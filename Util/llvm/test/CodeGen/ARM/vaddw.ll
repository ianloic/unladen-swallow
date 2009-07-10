; RUN: llvm-as < %s | llc -march=arm -mattr=+neon > %t
; RUN: grep {vaddw\\.s8} %t | count 1
; RUN: grep {vaddw\\.s16} %t | count 1
; RUN: grep {vaddw\\.s32} %t | count 1
; RUN: grep {vaddw\\.u8} %t | count 1
; RUN: grep {vaddw\\.u16} %t | count 1
; RUN: grep {vaddw\\.u32} %t | count 1

define <8 x i16> @vaddws8(<8 x i16>* %A, <8 x i8>* %B) nounwind {
	%tmp1 = load <8 x i16>* %A
	%tmp2 = load <8 x i8>* %B
	%tmp3 = call <8 x i16> @llvm.arm.neon.vaddws.v8i16(<8 x i16> %tmp1, <8 x i8> %tmp2)
	ret <8 x i16> %tmp3
}

define <4 x i32> @vaddws16(<4 x i32>* %A, <4 x i16>* %B) nounwind {
	%tmp1 = load <4 x i32>* %A
	%tmp2 = load <4 x i16>* %B
	%tmp3 = call <4 x i32> @llvm.arm.neon.vaddws.v4i32(<4 x i32> %tmp1, <4 x i16> %tmp2)
	ret <4 x i32> %tmp3
}

define <2 x i64> @vaddws32(<2 x i64>* %A, <2 x i32>* %B) nounwind {
	%tmp1 = load <2 x i64>* %A
	%tmp2 = load <2 x i32>* %B
	%tmp3 = call <2 x i64> @llvm.arm.neon.vaddws.v2i64(<2 x i64> %tmp1, <2 x i32> %tmp2)
	ret <2 x i64> %tmp3
}

define <8 x i16> @vaddwu8(<8 x i16>* %A, <8 x i8>* %B) nounwind {
	%tmp1 = load <8 x i16>* %A
	%tmp2 = load <8 x i8>* %B
	%tmp3 = call <8 x i16> @llvm.arm.neon.vaddwu.v8i16(<8 x i16> %tmp1, <8 x i8> %tmp2)
	ret <8 x i16> %tmp3
}

define <4 x i32> @vaddwu16(<4 x i32>* %A, <4 x i16>* %B) nounwind {
	%tmp1 = load <4 x i32>* %A
	%tmp2 = load <4 x i16>* %B
	%tmp3 = call <4 x i32> @llvm.arm.neon.vaddwu.v4i32(<4 x i32> %tmp1, <4 x i16> %tmp2)
	ret <4 x i32> %tmp3
}

define <2 x i64> @vaddwu32(<2 x i64>* %A, <2 x i32>* %B) nounwind {
	%tmp1 = load <2 x i64>* %A
	%tmp2 = load <2 x i32>* %B
	%tmp3 = call <2 x i64> @llvm.arm.neon.vaddwu.v2i64(<2 x i64> %tmp1, <2 x i32> %tmp2)
	ret <2 x i64> %tmp3
}

declare <8 x i16> @llvm.arm.neon.vaddws.v8i16(<8 x i16>, <8 x i8>) nounwind readnone
declare <4 x i32> @llvm.arm.neon.vaddws.v4i32(<4 x i32>, <4 x i16>) nounwind readnone
declare <2 x i64> @llvm.arm.neon.vaddws.v2i64(<2 x i64>, <2 x i32>) nounwind readnone

declare <8 x i16> @llvm.arm.neon.vaddwu.v8i16(<8 x i16>, <8 x i8>) nounwind readnone
declare <4 x i32> @llvm.arm.neon.vaddwu.v4i32(<4 x i32>, <4 x i16>) nounwind readnone
declare <2 x i64> @llvm.arm.neon.vaddwu.v2i64(<2 x i64>, <2 x i32>) nounwind readnone
