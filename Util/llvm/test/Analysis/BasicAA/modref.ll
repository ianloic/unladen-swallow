; RUN: opt < %s -basicaa -gvn -dse -S | FileCheck %s

declare void @llvm.memset.i32(i8*, i8, i32, i32)
declare void @llvm.memset.i8(i8*, i8, i8, i32)
declare void @llvm.memcpy.i8(i8*, i8*, i8, i32)
declare void @llvm.lifetime.end(i64, i8* nocapture)

declare void @external(i32*) 

define i32 @test0(i8* %P) {
  %A = alloca i32
  call void @external(i32* %A)
  
  store i32 0, i32* %A
  
  call void @llvm.memset.i32(i8* %P, i8 0, i32 42, i32 1)
  
  %B = load i32* %A
  ret i32 %B
  
; CHECK: @test0
; CHECK: ret i32 0
}

declare void @llvm.memcpy.i8(i8*, i8*, i8, i32)

define i8 @test1() {
; CHECK: @test1
  %A = alloca i8
  %B = alloca i8

  store i8 2, i8* %B  ;; Not written to by memcpy

  call void @llvm.memcpy.i8(i8* %A, i8* %B, i8 -1, i32 0)

  %C = load i8* %B
  ret i8 %C
; CHECK: ret i8 2
}

define i8 @test2(i8* %P) {
; CHECK: @test2
  %P2 = getelementptr i8* %P, i32 127
  store i8 1, i8* %P2  ;; Not dead across memset
  call void @llvm.memset.i8(i8* %P, i8 2, i8 127, i32 0)
  %A = load i8* %P2
  ret i8 %A
; CHECK: ret i8 1
}

define i8 @test2a(i8* %P) {
; CHECK: @test2
  %P2 = getelementptr i8* %P, i32 126
  
  ;; FIXME: DSE isn't zapping this dead store.
  store i8 1, i8* %P2  ;; Dead, clobbered by memset.
  
  call void @llvm.memset.i8(i8* %P, i8 2, i8 127, i32 0)
  %A = load i8* %P2
  ret i8 %A
; CHECK: %A = load i8* %P2
; CHECK: ret i8 %A
}

define void @test3(i8* %P, i8 %X) {
; CHECK: @test3
; CHECK-NOT: store
; CHECK-NOT: %Y
  %Y = add i8 %X, 1     ;; Dead, because the only use (the store) is dead.
  
  %P2 = getelementptr i8* %P, i32 2
  store i8 %Y, i8* %P2  ;; Not read by lifetime.end, should be removed.
; CHECK: store i8 2, i8* %P2
  call void @llvm.lifetime.end(i64 1, i8* %P)
  store i8 2, i8* %P2
; CHECK-NOT: store
  ret void
; CHECK: ret void
}

define void @test3a(i8* %P, i8 %X) {
; CHECK: @test3a
  %Y = add i8 %X, 1     ;; Dead, because the only use (the store) is dead.
  
  %P2 = getelementptr i8* %P, i32 2
  store i8 %Y, i8* %P2  ;; FIXME: Killed by llvm.lifetime.end, should be zapped.
; CHECK: store i8 %Y, i8* %P2
  call void @llvm.lifetime.end(i64 10, i8* %P)
  ret void
; CHECK: ret void
}