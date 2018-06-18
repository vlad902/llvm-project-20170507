; RUN: opt -module-summary %s -o %t.bc

; RUN: llvm-lto2 run %t.bc -o %t.o -save-temps \
; RUN:     -r %t.bc,AssemblyFnCall,px \
; RUN:     -r %t.bc,foo,pl

; RUN: llvm-dis %t.o.1.4.opt.bc -o - | FileCheck %s

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

module asm "\09.text"
module asm "\09.align\0916, 0x90"
module asm "\09.type\09foo,@function"
module asm "foo:"
module asm "\09ret "
module asm "\09.size\09foo, .-foo"
module asm ""

declare void @foo(i8* %p) #0

; CHECK-LABEL: define void @AssemblyFnCall
define void @AssemblyFnCall() #1 {
  %x = alloca i32, align 4
  %x1 = bitcast i32* %x to i8*
; CHECK: %x = alloca i32, align 4{{$}}
  call void @foo(i8* %x1)
  ret void
}
