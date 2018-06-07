; RUN: opt -S -stack-safety < %s | FileCheck %s

@sink = global i8* null, align 8


; !!! Missing tests:
; * va_arg
; * ret
; * call virtual
; * ptr-to-int-to-ptr
; * weird address arithmetic
; * non-constant offset
; * non-constant memset/memcpy size
; * array alloca
; * dynamic alloca
; * some kind of loop (though that would be more of a SCEV test)

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
