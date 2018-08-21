; Test IPA over a single combined file
; RUN: llvm-as %s -o %t0.bc
; RUN: llvm-as %S/Inputs/ipa-alias.ll -o %t1.bc
; RUN: llvm-link %t0.bc %t1.bc -o %t.combined.bc
; RUN: opt -S -stack-safety %t.combined.bc | FileCheck --check-prefixes=CHECK,WITHOUTLTO %s

; Do an end-to-test using the new LTO API
; RUN: opt -module-summary %s -o %t.summ0.bc
; RUN: opt -module-summary %S/Inputs/ipa-alias.ll -o %t.summ1.bc
; RUN: llvm-lto2 run %t.summ0.bc %t.summ1.bc -o %t.lto -save-temps -thinlto-threads 1 -O0 \
; RUN:  -r %t.summ0.bc,PreemptableAliasWrite1, \
; RUN:  -r %t.summ0.bc,AliasToPreemptableAliasWrite1, \
; RUN:  -r %t.summ0.bc,InterposableAliasWrite1, \
; RUN:  -r %t.summ0.bc,AliasWrite1, \
; RUN:  -r %t.summ0.bc,BitcastAliasWrite1, \
; RUN:  -r %t.summ0.bc,AliasToBitcastAliasWrite1, \
; RUN:  -r %t.summ0.bc,PreemptableAliasCall,px \
; RUN:  -r %t.summ0.bc,InterposableAliasCall,px \
; RUN:  -r %t.summ0.bc,AliasCall,px \
; RUN:  -r %t.summ0.bc,BitcastAliasCall,px \
; RUN:  -r %t.summ1.bc,PreemptableAliasWrite1,px \
; RUN:  -r %t.summ1.bc,AliasToPreemptableAliasWrite1,px \
; RUN:  -r %t.summ1.bc,InterposableAliasWrite1,px \
; RUN:  -r %t.summ1.bc,AliasWrite1,px \
; RUN:  -r %t.summ1.bc,BitcastAliasWrite1,px \
; RUN:  -r %t.summ1.bc,AliasToBitcastAliasWrite1,px \
; RUN:  -r %t.summ1.bc,Write1,px

; RUN: llvm-dis %t.lto.1.4.opt.bc -o - | FileCheck --check-prefixes=CHECK,THINLTO %s

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

declare void @PreemptableAliasWrite1(i8* %p)
declare void @AliasToPreemptableAliasWrite1(i8* %p)

declare void @InterposableAliasWrite1(i8* %p)
; Aliases to interposable aliases are not allowed

declare void @AliasWrite1(i8* %p)

declare void @BitcastAliasWrite1(i32* %p)
declare void @AliasToBitcastAliasWrite1(i8* %p)

; Call to dso_preemptable alias to a dso_local aliasee
define void @PreemptableAliasCall() {
; CHECK-LABEL: define void @PreemptableAliasCall
entry:
  %x1 = alloca i8
; CHECK: %x1 = alloca i8{{$}}
  call void @PreemptableAliasWrite1(i8* %x1)

  %x2 = alloca i8
; TODO: This should work for ThinLTO but doesn't due to https://bugs.llvm.org/show_bug.cgi?id=37884
; WITHOUTLTO: %x2 = alloca i8{{$}}
; THINLTO: %x2 = alloca i8, !stack-safe
  call void @AliasToPreemptableAliasWrite1(i8* %x2)
  ret void
}

; Call to an interposable alias to a non-interposable aliasee
define void @InterposableAliasCall() {
; CHECK-LABEL: define void @InterposableAliasCall
entry:
  %x = alloca i8
; ThinLTO can resolve the prevailing implementation for interposable definitions.
; WITHOUTLTO: %x = alloca i8{{$}}
; THINLTO: %x = alloca i8, !stack-safe
  call void @InterposableAliasWrite1(i8* %x)
  ret void
}

; Call to a dso_local/non-inteprosable alias/aliasee
define void @AliasCall() {
; CHECK-LABEL: define void @AliasCall
entry:
  %x = alloca i8
; CHECK: %x = alloca i8, !stack-safe
  call void @AliasWrite1(i8* %x)
  ret void
}

; Call to a bitcasted dso_local/non-inteprosable alias/aliasee
define void @BitcastAliasCall() {
; CHECK-LABEL: define void @BitcastAliasCall
entry:
  %x1 = alloca i32
; CHECK: %x1 = alloca i32, !stack-safe
  call void @BitcastAliasWrite1(i32* %x1)

  %x2 = alloca i8
; CHECK: %x2 = alloca i8, !stack-safe
  call void @AliasToBitcastAliasWrite1(i8* %x2)
  ret void
}
