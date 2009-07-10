; RUN: llvm-as < %s | llc -march=arm -mattr=+neon > %t
; RUN: grep {vrecps\\.f32} %t | count 2

define <2 x float> @vrecpsf32(<2 x float>* %A, <2 x float>* %B) nounwind {
	%tmp1 = load <2 x float>* %A
	%tmp2 = load <2 x float>* %B
	%tmp3 = call <2 x float> @llvm.arm.neon.vrecps.v2f32(<2 x float> %tmp1, <2 x float> %tmp2)
	ret <2 x float> %tmp3
}

define <4 x float> @vrecpsQf32(<4 x float>* %A, <4 x float>* %B) nounwind {
	%tmp1 = load <4 x float>* %A
	%tmp2 = load <4 x float>* %B
	%tmp3 = call <4 x float> @llvm.arm.neon.vrecps.v4f32(<4 x float> %tmp1, <4 x float> %tmp2)
	ret <4 x float> %tmp3
}

declare <2 x float> @llvm.arm.neon.vrecps.v2f32(<2 x float>, <2 x float>) nounwind readnone
declare <4 x float> @llvm.arm.neon.vrecps.v4f32(<4 x float>, <4 x float>) nounwind readnone
