; RUN: opt -S -stack-safety < %s | FileCheck %s

declare void @llvm.memset.p0i8.i32(i8* %dest, i8 %val, i32 %len, i1 %isvolatile)
declare void @llvm.memcpy.p0i8.p0i8.i32(i8* %dest, i8* %src, i32 %len, i1 %isvolatile)
declare void @llvm.memmove.p0i8.p0i8.i32(i8* %dest, i8* %src, i32 %len, i1 %isvolatile)

define void @MemsetInBounds() {
; CHECK-LABEL: define void @MemsetInBounds
entry:
  %x = alloca i32, align 4
; CHECK: %x = alloca i32, align 4, !stack-safe
  %x1 = bitcast i32* %x to i8*
  call void @llvm.memset.p0i8.i32(i8* %x1, i8 42, i32 4, i1 false)
  ret void
}

; Volatile does not matter for access bounds.
define void @VolatileMemsetInBounds() {
; CHECK-LABEL: define void @VolatileMemsetInBounds
entry:
  %x = alloca i32, align 4
; CHECK: %x = alloca i32, align 4, !stack-safe
  %x1 = bitcast i32* %x to i8*
  call void @llvm.memset.p0i8.i32(i8* %x1, i8 42, i32 4, i1 true)
  ret void
}

define void @MemsetOutOfBounds() {
; CHECK-LABEL: define void @MemsetOutOfBounds
entry:
  %x = alloca i32, align 4
; CHECK: %x = alloca i32, align 4{{$}}
  %x1 = bitcast i32* %x to i8*
  call void @llvm.memset.p0i8.i32(i8* %x1, i8 42, i32 5, i1 false)
  ret void
}

define void @MemsetNonConst(i32 %size) {
; CHECK-LABEL: define void @MemsetNonConst
entry:
  %x = alloca i32, align 4
; CHECK: %x = alloca i32, align 4{{$}}
  %x1 = bitcast i32* %x to i8*
  call void @llvm.memset.p0i8.i32(i8* %x1, i8 42, i32 %size, i1 false)
  ret void
}

; FIXME: memintrinsics should look at size range when possible
; Right now we refuse any non-constant size.
define void @MemsetNonConstInBounds(i1 zeroext %z) {
; CHECK-LABEL: define void @MemsetNonConstInBounds
entry:
  %x = alloca i32, align 4
; CHECK: %x = alloca i32, align 4{{$}}
  %x1 = bitcast i32* %x to i8*
  %size = select i1 %z, i32 3, i32 4
  call void @llvm.memset.p0i8.i32(i8* %x1, i8 42, i32 %size, i1 false)
  ret void
}

define void @MemcpyInBounds() {
; CHECK-LABEL: define void @MemcpyInBounds
entry:
  %x = alloca i32, align 4
  %y = alloca i32, align 4
; CHECK: %x = alloca i32, align 4, !stack-safe
; CHECK: %y = alloca i32, align 4, !stack-safe
  %x1 = bitcast i32* %x to i8*
  %y1 = bitcast i32* %y to i8*
  call void @llvm.memcpy.p0i8.p0i8.i32(i8* %x1, i8* %y1, i32 4, i1 false)
  ret void
}

define void @MemcpySrcOutOfBounds() {
; CHECK-LABEL: define void @MemcpySrcOutOfBounds
entry:
  %x = alloca i64, align 4
  %y = alloca i32, align 4
; CHECK: %x = alloca i64, align 4, !stack-safe
; CHECK: %y = alloca i32, align 4{{$}}
  %x1 = bitcast i64* %x to i8*
  %y1 = bitcast i32* %y to i8*
  call void @llvm.memcpy.p0i8.p0i8.i32(i8* %x1, i8* %y1, i32 5, i1 false)
  ret void
}

define void @MemcpyDstOutOfBounds() {
; CHECK-LABEL: define void @MemcpyDstOutOfBounds
entry:
  %x = alloca i32, align 4
  %y = alloca i64, align 4
; CHECK: %x = alloca i32, align 4{{$}}
; CHECK: %y = alloca i64, align 4, !stack-safe
  %x1 = bitcast i32* %x to i8*
  %y1 = bitcast i64* %y to i8*
  call void @llvm.memcpy.p0i8.p0i8.i32(i8* %x1, i8* %y1, i32 5, i1 false)
  ret void
}

define void @MemcpyBothOutOfBounds() {
; CHECK-LABEL: define void @MemcpyBothOutOfBounds
entry:
  %x = alloca i32, align 4
  %y = alloca i64, align 4
; CHECK: %x = alloca i32, align 4{{$}}
; CHECK: %y = alloca i64, align 4{{$}}
  %x1 = bitcast i32* %x to i8*
  %y1 = bitcast i64* %y to i8*
  call void @llvm.memcpy.p0i8.p0i8.i32(i8* %x1, i8* %y1, i32 9, i1 false)
  ret void
}

define void @MemcpySelfInBounds() {
; CHECK-LABEL: define void @MemcpySelfInBounds
entry:
  %x = alloca i64, align 4
; CHECK: %x = alloca i64, align 4, !stack-safe
  %x1 = bitcast i64* %x to i8*
  %x2 = getelementptr i8, i8* %x1, i64 5
  call void @llvm.memcpy.p0i8.p0i8.i32(i8* %x1, i8* %x2, i32 3, i1 false)
  ret void
}

define void @MemcpySelfSrcOutOfBounds() {
; CHECK-LABEL: define void @MemcpySelfSrcOutOfBounds
entry:
  %x = alloca i64, align 4
; CHECK: %x = alloca i64, align 4{{$}}
  %x1 = bitcast i64* %x to i8*
  %x2 = getelementptr i8, i8* %x1, i64 5
  call void @llvm.memcpy.p0i8.p0i8.i32(i8* %x1, i8* %x2, i32 4, i1 false)
  ret void
}

define void @MemcpySelfDstOutOfBounds() {
; CHECK-LABEL: define void @MemcpySelfDstOutOfBounds
entry:
  %x = alloca i64, align 4
; CHECK: %x = alloca i64, align 4{{$}}
  %x1 = bitcast i64* %x to i8*
  %x2 = getelementptr i8, i8* %x1, i64 5
  call void @llvm.memcpy.p0i8.p0i8.i32(i8* %x2, i8* %x1, i32 4, i1 false)
  ret void
}

define void @MemmoveSelfBothOutOfBounds() {
; CHECK-LABEL: define void @MemmoveSelfBothOutOfBounds
entry:
  %x = alloca i64, align 4
; CHECK: %x = alloca i64, align 4{{$}}
  %x1 = bitcast i64* %x to i8*
  %x2 = getelementptr i8, i8* %x1, i64 5
  call void @llvm.memmove.p0i8.p0i8.i32(i8* %x1, i8* %x2, i32 9, i1 false)
  ret void
}
