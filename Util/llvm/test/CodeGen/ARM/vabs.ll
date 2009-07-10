; RUN: llvm-as < %s | llc -march=arm -mattr=+neon > %t
; RUN: grep {vabs\\.s8} %t | count 2
; RUN: grep {vabs\\.s16} %t | count 2
; RUN: grep {vabs\\.s32} %t | count 2
; RUN: grep {vabs\\.f32} %t | count 2

define <8 x i8> @vabss8(<8 x i8>* %A) nounwind {
	%tmp1 = load <8 x i8>* %A
	%tmp2 = call <8 x i8> @llvm.arm.neon.vabs.v8i8(<8 x i8> %tmp1)
	ret <8 x i8> %tmp2
}

define <4 x i16> @vabss16(<4 x i16>* %A) nounwind {
	%tmp1 = load <4 x i16>* %A
	%tmp2 = call <4 x i16> @llvm.arm.neon.vabs.v4i16(<4 x i16> %tmp1)
	ret <4 x i16> %tmp2
}

define <2 x i32> @vabss32(<2 x i32>* %A) nounwind {
	%tmp1 = load <2 x i32>* %A
	%tmp2 = call <2 x i32> @llvm.arm.neon.vabs.v2i32(<2 x i32> %tmp1)
	ret <2 x i32> %tmp2
}

define <2 x float> @vabsf32(<2 x float>* %A) nounwind {
	%tmp1 = load <2 x float>* %A
	%tmp2 = call <2 x float> @llvm.arm.neon.vabsf.v2f32(<2 x float> %tmp1)
	ret <2 x float> %tmp2
}

define <16 x i8> @vabsQs8(<16 x i8>* %A) nounwind {
	%tmp1 = load <16 x i8>* %A
	%tmp2 = call <16 x i8> @llvm.arm.neon.vabs.v16i8(<16 x i8> %tmp1)
	ret <16 x i8> %tmp2
}

define <8 x i16> @vabsQs16(<8 x i16>* %A) nounwind {
	%tmp1 = load <8 x i16>* %A
	%tmp2 = call <8 x i16> @llvm.arm.neon.vabs.v8i16(<8 x i16> %tmp1)
	ret <8 x i16> %tmp2
}

define <4 x i32> @vabsQs32(<4 x i32>* %A) nounwind {
	%tmp1 = load <4 x i32>* %A
	%tmp2 = call <4 x i32> @llvm.arm.neon.vabs.v4i32(<4 x i32> %tmp1)
	ret <4 x i32> %tmp2
}

define <4 x float> @vabsQf32(<4 x float>* %A) nounwind {
	%tmp1 = load <4 x float>* %A
	%tmp2 = call <4 x float> @llvm.arm.neon.vabsf.v4f32(<4 x float> %tmp1)
	ret <4 x float> %tmp2
}

declare <8 x i8>  @llvm.arm.neon.vabs.v8i8(<8 x i8>) nounwind readnone
declare <4 x i16> @llvm.arm.neon.vabs.v4i16(<4 x i16>) nounwind readnone
declare <2 x i32> @llvm.arm.neon.vabs.v2i32(<2 x i32>) nounwind readnone
declare <2 x float> @llvm.arm.neon.vabsf.v2f32(<2 x float>) nounwind readnone

declare <16 x i8> @llvm.arm.neon.vabs.v16i8(<16 x i8>) nounwind readnone
declare <8 x i16> @llvm.arm.neon.vabs.v8i16(<8 x i16>) nounwind readnone
declare <4 x i32> @llvm.arm.neon.vabs.v4i32(<4 x i32>) nounwind readnone
declare <4 x float> @llvm.arm.neon.vabsf.v4f32(<4 x float>) nounwind readnone

