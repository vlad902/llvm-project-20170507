; RUN: opt -S -stack-safety < %s | FileCheck %s

define dso_local void @Write1(i8* %p) {
entry:
  store i8 0, i8* %p, align 1
  ret void
}

define dso_local void @Write4(i8* %p) {
entry:
  %0 = bitcast i8* %p to i32*
  store i32 0, i32* %0, align 1
  ret void
}

define dso_local void @Write4_2(i8* %p, i8* %q) {
entry:
  %0 = bitcast i8* %p to i32*
  store i32 0, i32* %0, align 1
  %1 = bitcast i8* %q to i32*
  store i32 0, i32* %1, align 1
  ret void
}

define dso_local void @Write8(i8* %p) {
entry:
  %0 = bitcast i8* %p to i64*
  store i64 0, i64* %0, align 1
  ret void
}

declare dso_local void @ExternalCall(i8* %p)

define dso_preemptable void @PreemptableWrite1(i8* %p) {
entry:
  store i8 0, i8* %p, align 1
  ret void
}

define dso_local i8* @ReturnDependent(i8* %p) {
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

; Call to dso_preemptable function
define void @PreemptableCall() {
; CHECK-LABEL: define void @PreemptableCall
entry:
  %x = alloca i32, align 4
  %x1 = bitcast i32* %x to i8*
; CHECK: %x = alloca i32, align 4{{$}}
  call void @PreemptableWrite1(i8* %x1)
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
define dso_local void @Rec0(i8* %p) {
entry:
  %p1 = getelementptr i8, i8* %p, i64 2
  call void @Write4(i8* %p1)
  ret void
}

; access range [3, 7)
define dso_local void @Rec1(i8* %p) {
entry:
  %p1 = getelementptr i8, i8* %p, i64 1
  call void @Rec0(i8* %p1)
  ret void
}

; access range [-2, 2)
define dso_local void @Rec2(i8* %p) {
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

define void @TwoArguments() {
; CHECK-LABEL: define void @TwoArguments
entry:
  %x = alloca i64, align 4
  %x1 = bitcast i64* %x to i8*
  %x2 = getelementptr i8, i8* %x1, i64 4
; CHECK: %x = alloca i64, align 4, !stack-safe
  call void @Write4_2(i8* %x2, i8* %x1)
  ret void
}

define void @TwoArgumentsOOBOne() {
; CHECK-LABEL: define void @TwoArgumentsOOBOne
entry:
  %x = alloca i64, align 4
  %x1 = bitcast i64* %x to i8*
  %x2 = getelementptr i8, i8* %x1, i64 5
; CHECK: %x = alloca i64, align 4{{$}}
  call void @Write4_2(i8* %x2, i8* %x1)
  ret void
}

define void @TwoArgumentsOOBOther() {
; CHECK-LABEL: define void @TwoArgumentsOOBOther
entry:
  %x = alloca i64, align 4
  %x0 = bitcast i64* %x to i8*
  %x1 = getelementptr i8, i8* %x0, i64 -1
  %x2 = getelementptr i8, i8* %x0, i64 4
; CHECK: %x = alloca i64, align 4{{$}}
  call void @Write4_2(i8* %x2, i8* %x1)
  ret void
}

define void @TwoArgumentsOOBBoth() {
; CHECK-LABEL: define void @TwoArgumentsOOBBoth
entry:
  %x = alloca i64, align 4
  %x0 = bitcast i64* %x to i8*
  %x1 = getelementptr i8, i8* %x0, i64 -1
  %x2 = getelementptr i8, i8* %x0, i64 5
; CHECK: %x = alloca i64, align 4{{$}}
  call void @Write4_2(i8* %x2, i8* %x1)
  ret void
}

; Recursive function that passes %acc unchanged => access range [0, 4).
define dso_local void @RecursiveNoOffset(i32* %p, i32 %size, i32* %acc) {
entry:
  %cmp = icmp eq i32 %size, 0
  br i1 %cmp, label %return, label %if.end

if.end:
  %0 = load i32, i32* %p, align 4
  %1 = load i32, i32* %acc, align 4
  %add = add nsw i32 %1, %0
  store i32 %add, i32* %acc, align 4
  %add.ptr = getelementptr inbounds i32, i32* %p, i64 1
  %sub = add nsw i32 %size, -1
  tail call void @RecursiveNoOffset(i32* %add.ptr, i32 %sub, i32* %acc)
  ret void

return:
  ret void
}

define i32 @TestRecursiveNoOffset(i32* %p, i32 %size) {
; CHECK-LABEL: define i32 @TestRecursiveNoOffset
entry:
  %sum = alloca i32, align 4
; CHECK: %sum = alloca i32, align 4, !stack-safe
  %0 = bitcast i32* %sum to i8*
  store i32 0, i32* %sum, align 4
  call void @RecursiveNoOffset(i32* %p, i32 %size, i32* %sum)
  %1 = load i32, i32* %sum, align 4
  ret i32 %1
}

; Recursive function that advances %acc on each iteration => access range unlimited.
define dso_local void @RecursiveWithOffset(i32 %size, i32* %acc) {
entry:
  %cmp = icmp eq i32 %size, 0
  br i1 %cmp, label %return, label %if.end

if.end:
  store i32 0, i32* %acc, align 4
  %acc2 = getelementptr inbounds i32, i32* %acc, i64 1
  %sub = add nsw i32 %size, -1
  tail call void @RecursiveWithOffset(i32 %sub, i32* %acc2)
  ret void

return:
  ret void
}

define void @TestRecursiveWithOffset(i32 %size) {
; CHECK-LABEL: define void @TestRecursiveWithOffset
entry:
  %sum = alloca i32, i64 16, align 4
; CHECK: %sum = alloca i32, i64 16, align 4{{$}}
  call void @RecursiveWithOffset(i32 %size, i32* %sum)
  ret void
}
