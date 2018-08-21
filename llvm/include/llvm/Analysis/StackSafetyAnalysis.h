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
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

namespace llvm {

class Function;
class Module;
class ScalarEvolution;
struct SSFunctionSummary;

/// Abstracts away the internal representation of stack safety results from
/// analysis consumers.
class StackSafetyResults {
public:
  StackSafetyResults() = delete;
  StackSafetyResults(std::unique_ptr<SSFunctionSummary> Summary);
  StackSafetyResults(StackSafetyResults &) = delete;
  StackSafetyResults(StackSafetyResults &&) = default;
  ~StackSafetyResults();

  /// Generate FunctionSummary initialization parameters from the results of the
  /// function-local stack safety analysis.
  void generateFunctionSummaryInfo(
      std::vector<FunctionSummary::Alloca> &Allocas,
      std::vector<FunctionSummary::LocalUse> &Params);

  std::unique_ptr<SSFunctionSummary> Summary;
};

/// Function-local stack safety analysis interface provided to other analysis
/// consumers like the ModuleSummaryAnalysis.
class StackSafetyInfo {
  using Callback = std::function<ScalarEvolution *(const Function &F)>;
  Callback GetSECallback;

public:
  StackSafetyInfo(Callback GetSECallback) : GetSECallback(GetSECallback) {}
  StackSafetyResults run(const Function &F) const;
};

/// StackSafetyInfo wrapper for the legacy pass manager
class StackSafetyInfoWrapperPass : public ModulePass {
  std::unique_ptr<StackSafetyInfo> SSI;

public:
  static char ID;

  StackSafetyInfoWrapperPass();

  StackSafetyInfo &getSSI() { return *SSI; }
  bool doInitialization(Module &M) override;
  bool doFinalization(Module &M) override;

  bool runOnModule(Module &M) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

/// StackSafetyInfo wrapper for the new pass manager
class StackSafetyAnalysis : public AnalysisInfoMixin<StackSafetyAnalysis> {
  static AnalysisKey Key;
  friend AnalysisInfoMixin<StackSafetyAnalysis>;

public:
  using Result = StackSafetyInfo;

  Result run(Module &M, ModuleAnalysisManager &AM);
};

/// This pass performs the global stack safety analysis and annotates stack-safe
/// allocations with !stack-safe metadata
class StackSafetyGlobalAnalysis : public ModulePass {
public:
  static char ID;

  StackSafetyGlobalAnalysis();

  bool runOnModule(Module &M) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

ModulePass *createStackSafetyInfoWrapperPass();
ModulePass *createStackSafetyGlobalAnalysis();

} // end namespace llvm

#endif // LLVM_ANALYSIS_STACKSAFETYANALYSIS_H
