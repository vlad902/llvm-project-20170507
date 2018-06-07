; RUN: opt -S -stack-safety < %s | FileCheck %s

; !!! Missing tests:
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

; access range [2, 6)
define void @Rec0(i8* %p) {
entry:
  %p1 = getelementptr i8, i8* %p, i64 2
  call void @Write4(i8* %p1)
  ret void
}

; access range [3, 7)
define void @Rec1(i8* %p) {
entry:
  %p1 = getelementptr i8, i8* %p, i64 1
  call void @Rec0(i8* %p1)
  ret void
}

; access range [-2, 2)
define void @Rec2(i8* %p) {
entry:
  %p1 = getelementptr i8, i8* %p, i64 -5
  call void @Rec1(i8* %p1)
  ret void
}

define void @f8left() {
; CHECK-LABEL: define void @f8
entry:
  %x = alloca i64, align 4
  %x1 = bitcast i64* %x to i8*
  %x2 = getelementptr i8, i8* %x1, i64 2
; 2 + [-2, 2) = [0, 4) => OK
; CHECK: %x = alloca i64, align 4, !stack-safe
  call void @Rec2(i8* %x2)
  ret void
}

define void @f8right() {
; CHECK-LABEL: define void @f8
entry:
  %x = alloca i64, align 4
  %x1 = bitcast i64* %x to i8*
  %x2 = getelementptr i8, i8* %x1, i64 6
; 6 + [-2, 2) = [4, 8) => OK
; CHECK: %x = alloca i64, align 4, !stack-safe
  call void @Rec2(i8* %x2)
  ret void
}

define void @f8oobleft() {
; CHECK-LABEL: define void @f8oobleft
entry:
  %x = alloca i64, align 4
  %x1 = bitcast i64* %x to i8*
  %x2 = getelementptr i8, i8* %x1, i64 1
; 1 + [-2, 2) = [-1, 3) => NOT OK
; CHECK: %x = alloca i64, align 4{{$}}
  call void @Rec2(i8* %x2)
  ret void
}

define void @f8oobright() {
; CHECK-LABEL: define void @f8oobright
entry:
  %x = alloca i64, align 4
  %x1 = bitcast i64* %x to i8*
  %x2 = getelementptr i8, i8* %x1, i64 7
; 7 + [-2, 2) = [5, 9) => NOT OK
; CHECK: %x = alloca i64, align 4{{$}}
  call void @Rec2(i8* %x2)
  ret void
}
