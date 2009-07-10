; RUN: llvm-as < %s | llc -march=arm -mattr=+neon > %t
; RUN: grep {vaba\\.s8} %t | count 2
; RUN: grep {vaba\\.s16} %t | count 2
; RUN: grep {vaba\\.s32} %t | count 2
; RUN: grep {vaba\\.u8} %t | count 2
; RUN: grep {vaba\\.u16} %t | count 2
; RUN: grep {vaba\\.u32} %t | count 2

define <8 x i8> @vabas8(<8 x i8>* %A, <8 x i8>* %B, <8 x i8>* %C) nounwind {
	%tmp1 = load <8 x i8>* %A
	%tmp2 = load <8 x i8>* %B
	%tmp3 = load <8 x i8>* %C
	%tmp4 = call <8 x i8> @llvm.arm.neon.vabas.v8i8(<8 x i8> %tmp1, <8 x i8> %tmp2, <8 x i8> %tmp3)
	ret <8 x i8> %tmp4
}

define <4 x i16> @vabas16(<4 x i16>* %A, <4 x i16>* %B, <4 x i16>* %C) nounwind {
	%tmp1 = load <4 x i16>* %A
	%tmp2 = load <4 x i16>* %B
	%tmp3 = load <4 x i16>* %C
	%tmp4 = call <4 x i16> @llvm.arm.neon.vabas.v4i16(<4 x i16> %tmp1, <4 x i16> %tmp2, <4 x i16> %tmp3)
	ret <4 x i16> %tmp4
}

define <2 x i32> @vabas32(<2 x i32>* %A, <2 x i32>* %B, <2 x i32>* %C) nounwind {
	%tmp1 = load <2 x i32>* %A
	%tmp2 = load <2 x i32>* %B
	%tmp3 = load <2 x i32>* %C
	%tmp4 = call <2 x i32> @llvm.arm.neon.vabas.v2i32(<2 x i32> %tmp1, <2 x i32> %tmp2, <2 x i32> %tmp3)
	ret <2 x i32> %tmp4
}

define <8 x i8> @vabau8(<8 x i8>* %A, <8 x i8>* %B, <8 x i8>* %C) nounwind {
	%tmp1 = load <8 x i8>* %A
	%tmp2 = load <8 x i8>* %B
	%tmp3 = load <8 x i8>* %C
	%tmp4 = call <8 x i8> @llvm.arm.neon.vabau.v8i8(<8 x i8> %tmp1, <8 x i8> %tmp2, <8 x i8> %tmp3)
	ret <8 x i8> %tmp4
}

define <4 x i16> @vabau16(<4 x i16>* %A, <4 x i16>* %B, <4 x i16>* %C) nounwind {
	%tmp1 = load <4 x i16>* %A
	%tmp2 = load <4 x i16>* %B
	%tmp3 = load <4 x i16>* %C
	%tmp4 = call <4 x i16> @llvm.arm.neon.vabau.v4i16(<4 x i16> %tmp1, <4 x i16> %tmp2, <4 x i16> %tmp3)
	ret <4 x i16> %tmp4
}

define <2 x i32> @vabau32(<2 x i32>* %A, <2 x i32>* %B, <2 x i32>* %C) nounwind {
	%tmp1 = load <2 x i32>* %A
	%tmp2 = load <2 x i32>* %B
	%tmp3 = load <2 x i32>* %C
	%tmp4 = call <2 x i32> @llvm.arm.neon.vabau.v2i32(<2 x i32> %tmp1, <2 x i32> %tmp2, <2 x i32> %tmp3)
	ret <2 x i32> %tmp4
}

define <16 x i8> @vabaQs8(<16 x i8>* %A, <16 x i8>* %B, <16 x i8>* %C) nounwind {
	%tmp1 = load <16 x i8>* %A
	%tmp2 = load <16 x i8>* %B
	%tmp3 = load <16 x i8>* %C
	%tmp4 = call <16 x i8> @llvm.arm.neon.vabas.v16i8(<16 x i8> %tmp1, <16 x i8> %tmp2, <16 x i8> %tmp3)
	ret <16 x i8> %tmp4
}

define <8 x i16> @vabaQs16(<8 x i16>* %A, <8 x i16>* %B, <8 x i16>* %C) nounwind {
	%tmp1 = load <8 x i16>* %A
	%tmp2 = load <8 x i16>* %B
	%tmp3 = load <8 x i16>* %C
	%tmp4 = call <8 x i16> @llvm.arm.neon.vabas.v8i16(<8 x i16> %tmp1, <8 x i16> %tmp2, <8 x i16> %tmp3)
	ret <8 x i16> %tmp4
}

define <4 x i32> @vabaQs32(<4 x i32>* %A, <4 x i32>* %B, <4 x i32>* %C) nounwind {
	%tmp1 = load <4 x i32>* %A
	%tmp2 = load <4 x i32>* %B
	%tmp3 = load <4 x i32>* %C
	%tmp4 = call <4 x i32> @llvm.arm.neon.vabas.v4i32(<4 x i32> %tmp1, <4 x i32> %tmp2, <4 x i32> %tmp3)
	ret <4 x i32> %tmp4
}

define <16 x i8> @vabaQu8(<16 x i8>* %A, <16 x i8>* %B, <16 x i8>* %C) nounwind {
	%tmp1 = load <16 x i8>* %A
	%tmp2 = load <16 x i8>* %B
	%tmp3 = load <16 x i8>* %C
	%tmp4 = call <16 x i8> @llvm.arm.neon.vabau.v16i8(<16 x i8> %tmp1, <16 x i8> %tmp2, <16 x i8> %tmp3)
	ret <16 x i8> %tmp4
}

define <8 x i16> @vabaQu16(<8 x i16>* %A, <8 x i16>* %B, <8 x i16>* %C) nounwind {
	%tmp1 = load <8 x i16>* %A
	%tmp2 = load <8 x i16>* %B
	%tmp3 = load <8 x i16>* %C
	%tmp4 = call <8 x i16> @llvm.arm.neon.vabau.v8i16(<8 x i16> %tmp1, <8 x i16> %tmp2, <8 x i16> %tmp3)
	ret <8 x i16> %tmp4
}

define <4 x i32> @vabaQu32(<4 x i32>* %A, <4 x i32>* %B, <4 x i32>* %C) nounwind {
	%tmp1 = load <4 x i32>* %A
	%tmp2 = load <4 x i32>* %B
	%tmp3 = load <4 x i32>* %C
	%tmp4 = call <4 x i32> @llvm.arm.neon.vabau.v4i32(<4 x i32> %tmp1, <4 x i32> %tmp2, <4 x i32> %tmp3)
	ret <4 x i32> %tmp4
}

declare <8 x i8>  @llvm.arm.neon.vabas.v8i8(<8 x i8>, <8 x i8>, <8 x i8>) nounwind readnone
declare <4 x i16> @llvm.arm.neon.vabas.v4i16(<4 x i16>, <4 x i16>, <4 x i16>) nounwind readnone
declare <2 x i32> @llvm.arm.neon.vabas.v2i32(<2 x i32>, <2 x i32>, <2 x i32>) nounwind readnone

declare <8 x i8>  @llvm.arm.neon.vabau.v8i8(<8 x i8>, <8 x i8>, <8 x i8>) nounwind readnone
declare <4 x i16> @llvm.arm.neon.vabau.v4i16(<4 x i16>, <4 x i16>, <4 x i16>) nounwind readnone
declare <2 x i32> @llvm.arm.neon.vabau.v2i32(<2 x i32>, <2 x i32>, <2 x i32>) nounwind readnone

declare <16 x i8> @llvm.arm.neon.vabas.v16i8(<16 x i8>, <16 x i8>, <16 x i8>) nounwind readnone
declare <8 x i16> @llvm.arm.neon.vabas.v8i16(<8 x i16>, <8 x i16>, <8 x i16>) nounwind readnone
declare <4 x i32> @llvm.arm.neon.vabas.v4i32(<4 x i32>, <4 x i32>, <4 x i32>) nounwind readnone

declare <16 x i8> @llvm.arm.neon.vabau.v16i8(<16 x i8>, <16 x i8>, <16 x i8>) nounwind readnone
declare <8 x i16> @llvm.arm.neon.vabau.v8i16(<8 x i16>, <8 x i16>, <8 x i16>) nounwind readnone
declare <4 x i32> @llvm.arm.neon.vabau.v4i32(<4 x i32>, <4 x i32>, <4 x i32>) nounwind readnone
