; RUN: opt -S -stack-safety %s | FileCheck %s

; Regression test that exercises a case when a AllocaOffsetRewritten SCEV
; could return an empty-set range. This could occur with udiv SCEVs where the
; RHS was re-written to 0.

declare void @ExternalFn(i64)

define void @Test1() {
; CHECK-LABEL: define void @Test1
  %x = alloca i8
; CHECK: %x = alloca i8{{$$}}
  %int = ptrtoint i8* %x to i64
  call void @Divide1(i64 %int)
  ret void
}

define dso_local void @Divide1(i64 %arg) {
  %quotient = udiv i64 undef, %arg
  call void @ExternalFn(i64 %quotient)
  unreachable
}

define void @Test2(i64 %arg) {
; CHECK-LABEL: define void @Test2
  %x = alloca i8
; CHECK: %x = alloca i8{{$$}}
  %int = ptrtoint i8* %x to i64
  call void @Divide2(i64 %int)
  ret void
}

define dso_local void @Divide2(i64 %arg) {
  %x = inttoptr i64 %arg to i8*
  %quotient = udiv i64 undef, %arg
  %arrayidx = getelementptr i8, i8* %x, i64 %quotient
  load i8, i8* %arrayidx
  unreachable
}
