; Test IPA over a single combined file
; RUN: llvm-as %s -o %t0.bc
; RUN: llvm-as %S/Inputs/ipa.ll -o %t1.bc
; RUN: llvm-link %t0.bc %t1.bc -o %t.combined.bc
; RUN: opt -S -stack-safety %t.combined.bc | FileCheck --check-prefixes=CHECK,WITHOUTLTO %s

; Do an end-to-test using the new LTO API
; TODO: Hideous llvm-lto2 invocation, add a --default-symbol-resolution to llvm-lto2?
; RUN: opt -module-summary %s -o %t.summ0.bc
; RUN: opt -module-summary %S/Inputs/ipa.ll -o %t.summ1.bc
; RUN: llvm-lto2 run %t.summ0.bc %t.summ1.bc -o %t.lto -save-temps -thinlto-threads 1 -O0 \
; RUN:  -r %t.summ0.bc,Write1, \
; RUN:  -r %t.summ0.bc,Write4, \
; RUN:  -r %t.summ0.bc,Write4_2, \
; RUN:  -r %t.summ0.bc,Write8, \
; RUN:  -r %t.summ0.bc,ExternalCall, \
; RUN:  -r %t.summ0.bc,PreemptableWrite1, \
; RUN:  -r %t.summ0.bc,InterposableWrite1, \
; RUN:  -r %t.summ0.bc,ReturnDependent, \
; RUN:  -r %t.summ0.bc,Rec2, \
; RUN:  -r %t.summ0.bc,RecursiveNoOffset, \
; RUN:  -r %t.summ0.bc,RecursiveWithOffset, \
; RUN:  -r %t.summ0.bc,f1,px \
; RUN:  -r %t.summ0.bc,f2,px \
; RUN:  -r %t.summ0.bc,f3,px \
; RUN:  -r %t.summ0.bc,f4,px \
; RUN:  -r %t.summ0.bc,f5,px \
; RUN:  -r %t.summ0.bc,f6,px \
; RUN:  -r %t.summ0.bc,PreemptableCall,px \
; RUN:  -r %t.summ0.bc,InterposableCall,px \
; RUN:  -r %t.summ0.bc,PrivateCall,px \
; RUN:  -r %t.summ0.bc,f7,px \
; RUN:  -r %t.summ0.bc,f8left,px \
; RUN:  -r %t.summ0.bc,f8right,px \
; RUN:  -r %t.summ0.bc,f8oobleft,px \
; RUN:  -r %t.summ0.bc,f8oobright,px \
; RUN:  -r %t.summ0.bc,TwoArguments,px \
; RUN:  -r %t.summ0.bc,TwoArgumentsOOBOne,px \
; RUN:  -r %t.summ0.bc,TwoArgumentsOOBOther,px \
; RUN:  -r %t.summ0.bc,TwoArgumentsOOBBoth,px \
; RUN:  -r %t.summ0.bc,TestRecursiveNoOffset,px \
; RUN:  -r %t.summ0.bc,TestRecursiveWithOffset,px \
; RUN:  -r %t.summ1.bc,Write1,px \
; RUN:  -r %t.summ1.bc,Write4,px \
; RUN:  -r %t.summ1.bc,Write4_2,px \
; RUN:  -r %t.summ1.bc,Write8,px \
; RUN:  -r %t.summ1.bc,ExternalCall,px \
; RUN:  -r %t.summ1.bc,PreemptableWrite1,px \
; RUN:  -r %t.summ1.bc,InterposableWrite1,px \
; RUN:  -r %t.summ1.bc,ReturnDependent,px \
; RUN:  -r %t.summ1.bc,Rec0,px \
; RUN:  -r %t.summ1.bc,Rec1,px \
; RUN:  -r %t.summ1.bc,Rec2,px \
; RUN:  -r %t.summ1.bc,RecursiveNoOffset,px \
; RUN:  -r %t.summ1.bc,RecursiveWithOffset,px

; RUN: llvm-dis %t.lto.1.4.opt.bc -o - | FileCheck --check-prefixes=CHECK,THINLTO %s

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

declare void @Write1(i8* %p)
declare void @Write4(i8* %p)
declare void @Write4_2(i8* %p, i8* %q)
declare void @Write8(i8* %p)
declare dso_local void @ExternalCall(i8* %p)
declare void @PreemptableWrite1(i8* %p)
declare void @InterposableWrite1(i8* %p)
declare i8* @ReturnDependent(i8* %p)
declare void @Rec2(i8* %p)
declare void @RecursiveNoOffset(i32* %p, i32 %size, i32* %acc)
declare void @RecursiveWithOffset(i32 %size, i32* %acc)

define private void @PrivateWrite1(i8* %p) {
entry:
  store i8 0, i8* %p, align 1
  ret void
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

; Call to function with interposable linkage
define void @InterposableCall() {
; CHECK-LABEL: define void @InterposableCall
entry:
  %x = alloca i32, align 4
  %x1 = bitcast i32* %x to i8*
; ThinLTO can resolve the prevailing implementation for interposable functions.
; WITHOUTLTO: %x = alloca i32, align 4{{$}}
; THINLTO: %x = alloca i32, align 4, !stack-safe
  call void @InterposableWrite1(i8* %x1)
  ret void
}

; Call to function with private linkage
define void @PrivateCall() {
; CHECK-LABEL: define void @PrivateCall
entry:
  %x = alloca i32, align 4
  %x1 = bitcast i32* %x to i8*
; CHECK: %x = alloca i32, align 4, !stack-safe
  call void @PrivateWrite1(i8* %x1)
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

define void @TestRecursiveWithOffset(i32 %size) {
; CHECK-LABEL: define void @TestRecursiveWithOffset
entry:
  %sum = alloca i32, i64 16, align 4
; CHECK: %sum = alloca i32, i64 16, align 4{{$}}
  call void @RecursiveWithOffset(i32 %size, i32* %sum)
  ret void
}
