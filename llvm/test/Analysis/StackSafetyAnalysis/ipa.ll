; RUN: opt -S -stack-safety < %s | FileCheck %s

; !!! Missing tests:
; * more than 1 call depth
; * offset-from-alloca
; * offset-from-param
; * alloca passed through 2 params in the same call
; * recursion
; * "bad" recursion (+1 offset on each step)

define void @Write1(i8* %p) {
entry:
  store i8 0, i8* %p, align 1
  ret void
}

define void @Write4(i8* %p) {
entry:
  %0 = bitcast i8* %p to i32*
  store i32 0, i32* %0, align 1
  ret void
}

define void @Write8(i8* %p) {
entry:
  %0 = bitcast i8* %p to i64*
  store i64 0, i64* %0, align 1
  ret void
}

declare void @ExternalCall(i8* %p)

define i8* @ReturnDependent(i8* %p) {
entry:
  %p2 = getelementptr i8, i8* %p, i64 2
  ret i8* %p2
}

; Basic out-of-bounds.
define void @f1() {
; CHECK-LABEL: define void @f1
entry:
  %x = alloca i32, align 4
  %x1 = bitcast i32* %x to i8*
; CHECK: %x = alloca i32, align 4{{$}}
  call void @Write8(i8* %x1)
  ret void
}

; Basic in-bounds.
define void @f2() {
; CHECK-LABEL: define void @f2
entry:
  %x = alloca i32, align 4
  %x1 = bitcast i32* %x to i8*
; CHECK: %x = alloca i32, align 4, !stack-safe 
  call void @Write1(i8* %x1)
  ret void
}

; Another basic in-bounds.
define void @f3() {
; CHECK-LABEL: define void @f3
entry:
  %x = alloca i32, align 4
  %x1 = bitcast i32* %x to i8*
; CHECK: %x = alloca i32, align 4, !stack-safe 
  call void @Write4(i8* %x1)
  ret void
}

; In-bounds with offset.
define void @f4() {
; CHECK-LABEL: define void @f4
entry:
  %x = alloca i32, align 4
  %x1 = bitcast i32* %x to i8*
  %x2 = getelementptr i8, i8* %x1, i64 1
; CHECK: %x = alloca i32, align 4, !stack-safe 
  call void @Write1(i8* %x2)
  ret void
}

; Out-of-bounds with offset.
define void @f5() {
; CHECK-LABEL: define void @f5
entry:
  %x = alloca i32, align 4
  %x1 = bitcast i32* %x to i8*
  %x2 = getelementptr i8, i8* %x1, i64 1
; CHECK: %x = alloca i32, align 4{{$}}
  call void @Write4(i8* %x2)
  ret void
}

; External call.
define void @f6() {
; CHECK-LABEL: define void @f6
entry:
  %x = alloca i32, align 4
  %x1 = bitcast i32* %x to i8*
; CHECK: %x = alloca i32, align 4{{$}}
  call void @ExternalCall(i8* %x1)
  ret void
}

; Caller returns a dependent value.
; FIXME: alloca considered unsafe even if the return value is unused.
define void @f7() {
; CHECK-LABEL: define void @f7
entry:
  %x = alloca i32, align 4
  %x1 = bitcast i32* %x to i8*
; CHECK: %x = alloca i32, align 4{{$}}
  %x2 = call i8* @ReturnDependent(i8* %x1)
  ret void
}
