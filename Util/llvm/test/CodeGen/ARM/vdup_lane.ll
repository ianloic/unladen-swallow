; RUN: llvm-as < %s | llc -march=arm -mattr=+neon > %t
; RUN: grep vdup.8 %t | count 2
; RUN: grep vdup.16 %t | count 2
; RUN: grep vdup.32 %t | count 4

define <8 x i8> @vduplane8(<8 x i8>* %A) nounwind {
	%tmp1 = load <8 x i8>* %A
	%tmp2 = shufflevector <8 x i8> %tmp1, <8 x i8> undef, <8 x i32> < i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1 >
	ret <8 x i8> %tmp2
}

define <4 x i16> @vduplane16(<4 x i16>* %A) nounwind {
	%tmp1 = load <4 x i16>* %A
	%tmp2 = shufflevector <4 x i16> %tmp1, <4 x i16> undef, <4 x i32> < i32 1, i32 1, i32 1, i32 1 >
	ret <4 x i16> %tmp2
}

define <2 x i32> @vduplane32(<2 x i32>* %A) nounwind {
	%tmp1 = load <2 x i32>* %A
	%tmp2 = shufflevector <2 x i32> %tmp1, <2 x i32> undef, <2 x i32> < i32 1, i32 1 >
	ret <2 x i32> %tmp2
}

define <2 x float> @vduplanefloat(<2 x float>* %A) nounwind {
	%tmp1 = load <2 x float>* %A
	%tmp2 = shufflevector <2 x float> %tmp1, <2 x float> undef, <2 x i32> < i32 1, i32 1 >
	ret <2 x float> %tmp2
}

define <16 x i8> @vduplaneQ8(<8 x i8>* %A) nounwind {
	%tmp1 = load <8 x i8>* %A
	%tmp2 = shufflevector <8 x i8> %tmp1, <8 x i8> undef, <16 x i32> < i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1 >
	ret <16 x i8> %tmp2
}

define <8 x i16> @vduplaneQ16(<4 x i16>* %A) nounwind {
	%tmp1 = load <4 x i16>* %A
	%tmp2 = shufflevector <4 x i16> %tmp1, <4 x i16> undef, <8 x i32> < i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1 >
	ret <8 x i16> %tmp2
}

define <4 x i32> @vduplaneQ32(<2 x i32>* %A) nounwind {
	%tmp1 = load <2 x i32>* %A
	%tmp2 = shufflevector <2 x i32> %tmp1, <2 x i32> undef, <4 x i32> < i32 1, i32 1, i32 1, i32 1 >
	ret <4 x i32> %tmp2
}

define <4 x float> @vduplaneQfloat(<2 x float>* %A) nounwind {
	%tmp1 = load <2 x float>* %A
	%tmp2 = shufflevector <2 x float> %tmp1, <2 x float> undef, <4 x i32> < i32 1, i32 1, i32 1, i32 1 >
	ret <4 x float> %tmp2
}
