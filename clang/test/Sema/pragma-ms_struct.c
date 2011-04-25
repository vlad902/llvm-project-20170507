// RUN: %clang_cc1 -fsyntax-only -verify -triple x86_64-apple-darwin9 %s

#pragma ms_struct on

#pragma ms_struct off

#pragma ms_struct reset

#pragma ms_struct // expected-warning {{incorrect use of '#pragma ms_struct on|off' - ignored}}

#pragma ms_struct on top of spaghetti  // expected-warning {{extra tokens at end of '#pragma ms_struct' - ignored}}

struct foo
{
  int a;
  int b;
  char c;
};

