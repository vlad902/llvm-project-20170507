; RUN: opt -S -stack-safety < %s | FileCheck %s

@sink = global i8* null, align 8

declare void @llvm.memset.p0i8.i32(i8* %dest, i8 %val, i32 %len, i1 %isvolatile)
declare void @llvm.memcpy.p0i8.p0i8.i32(i8* %dest, i8* %src, i32 %len, i1 %isvolatile)
declare void @llvm.memmove.p0i8.p0i8.i32(i8* %dest, i8* %src, i32 %len, i1 %isvolatile)

; Address leaked.
define void @LeakAddress() {
; CHECK-LABEL: define void @LeakAddress
entry:
  %x = alloca i32, align 4
; CHECK: %x = alloca i32, align 4{{$}}
  %x1 = bitcast i32* %x to i8*
  store i8* %x1, i8** @sink, align 8
  ret void
}

define void @StoreInBounds() {
; CHECK-LABEL: define void @StoreInBounds
entry:
  %x = alloca i32, align 4
; CHECK: %x = alloca i32, align 4, !stack-safe
  %x1 = bitcast i32* %x to i8*
  store i8 0, i8* %x1, align 1
  ret void
}

define void @StoreInBounds2() {
; CHECK-LABEL: define void @StoreInBounds2
entry:
  %x = alloca i32, align 4
; CHECK: %x = alloca i32, align 4, !stack-safe
  store i32 0, i32* %x, align 4
  ret void
}

define void @StoreInBounds3() {
; CHECK-LABEL: define void @StoreInBounds3
entry:
  %x = alloca i32, align 4
  %x1 = bitcast i32* %x to i8*
  %x2 = getelementptr i8, i8* %x1, i64 2
; CHECK: %x = alloca i32, align 4, !stack-safe
  store i8 0, i8* %x2, align 1
  ret void
}

; FIXME: ScalarEvolution does not look through ptrtoint/inttoptr.
define void @StoreInBounds4() {
; CHECK-LABEL: define void @StoreInBounds4
entry:
  %x = alloca i32, align 4
  %x1 = ptrtoint i32* %x to i64
  %x2 = add i64 %x1, 2
  %x3 = inttoptr i64 %x2 to i8*
; CHECK: %x = alloca i32, align 4{{$}}
  store i8 0, i8* %x3, align 1
  ret void
}

define void @StoreOutOfBounds() {
; CHECK-LABEL: define void @StoreOutOfBounds
entry:
  %x = alloca i32, align 4
  %x1 = bitcast i32* %x to i8*
  %x2 = getelementptr i8, i8* %x1, i64 2
  %x3 = bitcast i8* %x2 to i32*
; CHECK: %x = alloca i32, align 4{{$}}
  store i32 0, i32* %x3, align 1
  ret void
}

; There is no difference in load vs store handling.
define void @LoadInBounds() {
; CHECK-LABEL: define void @LoadInBounds
entry:
  %x = alloca i32, align 4
; CHECK: %x = alloca i32, align 4, !stack-safe
  %x1 = bitcast i32* %x to i8*
  %v = load i8, i8* %x1, align 1
  ret void
}

define void @LoadOutOfBounds() {
; CHECK-LABEL: define void @LoadOutOfBounds
entry:
  %x = alloca i32, align 4
; CHECK: %x = alloca i32, align 4{{$}}
  %x1 = bitcast i32* %x to i8*
  %x2 = getelementptr i8, i8* %x1, i64 2
  %x3 = bitcast i8* %x2 to i32*
  %v = load i32, i32* %x3, align 1
  ret void
}

; Leak through ret.
define i8* @Ret() {
; CHECK-LABEL: define i8* @Ret
entry:
  %x = alloca i32, align 4
; CHECK: %x = alloca i32, align 4{{$}}
  %x1 = bitcast i32* %x to i8*
  %x2 = getelementptr i8, i8* %x1, i64 2
  ret i8* %x2
}

; Indirect calls can not be analyzed (yet).
define void @IndirectCall(void (i8*)* %p) {
; CHECK-LABEL: define void @IndirectCall
entry:
  %x = alloca i32, align 4
; CHECK: %x = alloca i32, align 4{{$}}
  %x1 = bitcast i32* %x to i8*
  call void %p(i8* %x1);
  ret void
}

define void @NonConstantOffset(i1 zeroext %z) {
; CHECK-LABEL: define void @NonConstantOffset
entry:
  %x = alloca i32, align 4
  %x1 = bitcast i32* %x to i8*
  %idx = select i1 %z, i64 1, i64 2
  %x2 = getelementptr i8, i8* %x1, i64 %idx
; CHECK: %x = alloca i32, align 4, !stack-safe
  store i8 0, i8* %x2, align 1
  ret void
}

define void @NonConstantOffsetOOB(i1 zeroext %z) {
; CHECK-LABEL: define void @NonConstantOffsetOOB
entry:
  %x = alloca i32, align 4
  %x1 = bitcast i32* %x to i8*
  %idx = select i1 %z, i64 1, i64 4
  %x2 = getelementptr i8, i8* %x1, i64 %idx
; CHECK: %x = alloca i32, align 4{{$}}
  store i8 0, i8* %x2, align 1
  ret void
}

define void @ArrayAlloca() {
; CHECK-LABEL: define void @ArrayAlloca
entry:
  %x = alloca i32, i32 10, align 4
; CHECK: %x = alloca i32, i32 10, align 4, !stack-safe
  %x1 = bitcast i32* %x to i8*
  %x2 = getelementptr i8, i8* %x1, i64 36
  %x3 = bitcast i8* %x2 to i32*
  store i32 0, i32* %x3, align 1
  ret void
}

define void @ArrayAllocaOOB() {
; CHECK-LABEL: define void @ArrayAllocaOOB
entry:
  %x = alloca i32, i32 10, align 4
; CHECK: %x = alloca i32, i32 10, align 4{{$}}
  %x1 = bitcast i32* %x to i8*
  %x2 = getelementptr i8, i8* %x1, i64 37
  %x3 = bitcast i8* %x2 to i32*
  store i32 0, i32* %x3, align 1
  ret void
}

define void @DynamicAllocaUnused(i64 %size) {
; CHECK-LABEL: define void @DynamicAllocaUnused
entry:
  %x = alloca i32, i64 %size, align 16
; CHECK: %x = alloca i32, i64 %size, align 16, !stack-safe
  ret void
}

; Dynamic alloca with unknown size.
define void @DynamicAlloca(i64 %size) {
; CHECK-LABEL: define void @DynamicAlloca
entry:
  %x = alloca i32, i64 %size, align 16
; CHECK: %x = alloca i32, i64 %size, align 16{{$}}
  store i32 0, i32* %x, align 1
  ret void
}

; Dynamic alloca with limited size.
; FIXME: could be proved safe. Implement.
define void @DynamicAllocaFiniteSizeRange(i1 zeroext %z) {
; CHECK-LABEL: define void @DynamicAllocaFiniteSizeRange
entry:
  %size = select i1 %z, i64 3, i64 5
  %x = alloca i32, i64 %size, align 16
; CHECK: %x = alloca i32, i64 %size, align 16{{$}}
  store i32 0, i32* %x, align 1
  ret void
}

define signext i8 @SimpleLoop() {
entry:
  %x = alloca [10 x i8], align 1
; CHECK: %x = alloca [10 x i8], align 1, !stack-safe
  %0 = getelementptr inbounds [10 x i8], [10 x i8]* %x, i64 0, i64 0
  %lftr.limit = getelementptr inbounds [10 x i8], [10 x i8]* %x, i64 0, i64 10
  br label %for.body

for.body:
  %sum.010 = phi i8 [ 0, %entry ], [ %add, %for.body ]
  %p.09 = phi i8* [ %0, %entry ], [ %incdec.ptr, %for.body ]
  %incdec.ptr = getelementptr inbounds i8, i8* %p.09, i64 1
  %1 = load volatile i8, i8* %p.09, align 1
  %add = add i8 %1, %sum.010
  %exitcond = icmp eq i8* %incdec.ptr, %lftr.limit
  br i1 %exitcond, label %for.cond.cleanup, label %for.body

for.cond.cleanup:
  ret i8 %add
}

; OOB in a loop.
define signext i8 @SimpleLoopOOB() {
entry:
  %x = alloca [10 x i8], align 1
; CHECK: %x = alloca [10 x i8], align 1{{$}}
  %0 = getelementptr inbounds [10 x i8], [10 x i8]* %x, i64 0, i64 0
 ; 11 iterations
  %lftr.limit = getelementptr inbounds [10 x i8], [10 x i8]* %x, i64 0, i64 11
  br label %for.body

for.body:
  %sum.010 = phi i8 [ 0, %entry ], [ %add, %for.body ]
  %p.09 = phi i8* [ %0, %entry ], [ %incdec.ptr, %for.body ]
  %incdec.ptr = getelementptr inbounds i8, i8* %p.09, i64 1
  %1 = load volatile i8, i8* %p.09, align 1
  %add = add i8 %1, %sum.010
  %exitcond = icmp eq i8* %incdec.ptr, %lftr.limit
  br i1 %exitcond, label %for.cond.cleanup, label %for.body

for.cond.cleanup:
  ret i8 %add
}

; FIXME: we don't understand that %sz in the memset call is limited to 128 by the preceding check.
define dso_local void @SizeCheck(i32 %sz) {
entry:
  %x1 = alloca [128 x i8], align 16
; CHECK: %x1 = alloca [128 x i8], align 16{{$}}
  %x1.sub = getelementptr inbounds [128 x i8], [128 x i8]* %x1, i64 0, i64 0
  %cmp = icmp slt i32 %sz, 129
  br i1 %cmp, label %if.then, label %if.end

if.then:
  call void @llvm.memset.p0i8.i32(i8* nonnull align 16 %x1.sub, i8 0, i32 %sz, i1 false)
  br label %if.end

if.end:
  ret void
}
