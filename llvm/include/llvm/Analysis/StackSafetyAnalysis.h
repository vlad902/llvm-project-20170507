//===- StackSafetyAnalysis.h - Module summary index builder ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This is the interface to build a StackSafetyIndex for a module.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_STACKSAFETYANALYSIS_H
#define LLVM_ANALYSIS_STACKSAFETYANALYSIS_H

#include "llvm/ADT/Optional.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

namespace llvm {

class Function;
class Module;
class ScalarEvolution;

struct FunctionStackSummary;

// Stack analysis interface provided to other analysis consumers
class StackSafetyInfo {
  using Callback = std::function<ScalarEvolution *(const Function &F)>;
  Callback GetSECallback;

public:
  StackSafetyInfo(Callback GetSECallback) : GetSECallback(GetSECallback) {}
  void run(Function &F, FunctionStackSummary &FS) const;
};

// Analysis pass for the legacy pass manager
class StackSafetyWrapperPass : public ModulePass {
  std::unique_ptr<StackSafetyInfo> SSI;

public:
  static char ID;

  StackSafetyWrapperPass();

  StackSafetyInfo &getSSI() { return *SSI; }
  bool doInitialization(Module &M);
  bool doFinalization(Module &M);

  bool runOnModule(Module &M) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

//===--------------------------------------------------------------------===//
//
// createStackSafetyIndexWrapperPass - This pass builds a StackSafetyIndex
// object for the module, to be written to bitcode or LLVM assembly.
//
ModulePass *createStackSafetyWrapperPass();

// Analysis pass for the new pass manager
class StackSafetyAnalysis : public AnalysisInfoMixin<StackSafetyAnalysis> {
  static AnalysisKey Key;
  friend AnalysisInfoMixin<StackSafetyAnalysis>;

public:
  using Result = StackSafetyInfo;

  Result run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_STACKSAFETYANALYSIS_H
