//===- StackSafety.cpp - Safe Stack Insertion -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/StackSafetyAnalysis.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "stack-safety"

static cl::opt<int> StackSafetyMaxIterations("stack-safety-max-iterations",
                                             cl::init(5), cl::Hidden);

namespace {

/// Rewrite an SCEV expression for a memory access address to an expression that
/// represents offset from the given alloca.
///
/// The implementation simply replaces all mentions of the alloca with zero.
class AllocaOffsetRewriter : public SCEVRewriteVisitor<AllocaOffsetRewriter> {
  const Value *AllocaPtr;

public:
  AllocaOffsetRewriter(ScalarEvolution &SE, const Value *AllocaPtr)
      : SCEVRewriteVisitor(SE), AllocaPtr(AllocaPtr) {}

  const SCEV *visitUnknown(const SCEVUnknown *Expr) {
    // FIXME: look through one or several levels of definitions?
    // This can be inttoptr(AllocaPtr) and SCEV would not unwrap
    // it for us.
    if (Expr->getValue() == AllocaPtr)
      return SE.getZero(Expr->getType());
    return Expr;
  }
};

struct UseSummary {
  ConstantRange Range;
  ConstantRange LocalRange;
  const Instruction *BadI;
  const char *Reason;

  struct CallSummary {
    std::string Callee;
    unsigned ParamNo;
    ConstantRange Range;
    CallSummary(std::string Callee, unsigned ParamNo)
        : Callee(Callee), ParamNo(ParamNo), Range(64, false) {}
    CallSummary(std::string Callee, unsigned ParamNo, ConstantRange Range)
        : Callee(Callee), ParamNo(ParamNo), Range(Range) {}
  };
  SmallVector<CallSummary, 4> Calls;

  UseSummary() : Range(64, false), LocalRange(64, false), BadI(nullptr), Reason(nullptr) {}
  void dump() {
    dbgs() << Range;
    for (auto &Call : Calls)
      dbgs() << ", " << Call.Callee << "[#" << Call.ParamNo << ", offset " << Call.Range << "]";
    dbgs() << "\n";
  }
};

struct AllocaSummary {
  AllocaInst *AI;
  uint64_t Size;
  UseSummary Summary;

  AllocaSummary(AllocaInst *AI, uint64_t Size) : AI(AI), Size(Size) {}
  void dump() {
    dbgs() << "    alloca %" << AI->getName() << " [" << Size << " bytes]\n      ";
    Summary.dump();
  }
};

struct ParamSummary {
  UseSummary Summary;

  void dump(unsigned ParamNo) {
    dbgs() << "    arg #" << ParamNo << "\n      ";
    Summary.dump();
  }
};

// FunctionSummary could also describe return value as depending on one or more of its arguments.
struct FunctionSummary {
  SmallVector<AllocaSummary, 4> Allocas;
  SmallVector<ParamSummary, 4> Params;
  void dump(StringRef Name) {
    dbgs() << "  @" << Name << "\n";
    for (unsigned i = 0; i < Params.size(); ++i)
      Params[i].dump(i);
    for (auto &AS : Allocas)
      AS.dump();
  }
};

class StackSafety {
  Function &F;
  const DataLayout &DL;
  ScalarEvolution &SE;
  
  Type *StackPtrTy;
  Type *IntPtrTy;
  Type *Int32Ty;
  Type *Int8Ty;

  uint64_t getStaticAllocaAllocationSize(const AllocaInst* AI);

  ConstantRange OffsetFromAlloca(Value *Addr, const Value *AllocaPtr);

  ConstantRange GetAccessRange(Value *Addr, const Value *AllocaPtr, uint64_t AccessSize);
  ConstantRange GetMemIntrinsicAccessRange(const MemIntrinsic *MI, const Use &U,
                                           const Value *AllocaPtr);

  bool analyzeAllUses(Value *Ptr, UseSummary &AS);

public:
  StackSafety(Function &F, const DataLayout &DL, ScalarEvolution &SE)
      : F(F), DL(DL), SE(SE), StackPtrTy(Type::getInt8PtrTy(F.getContext())),
        IntPtrTy(DL.getIntPtrType(F.getContext())),
        Int32Ty(Type::getInt32Ty(F.getContext())),
        Int8Ty(Type::getInt8Ty(F.getContext())) {}

  // Run the transformation on the associated function.
  // Returns whether the function was changed.
  bool run(FunctionSummary&);
};

uint64_t StackSafety::getStaticAllocaAllocationSize(const AllocaInst* AI) {
  uint64_t Size = DL.getTypeAllocSize(AI->getAllocatedType());
  if (AI->isArrayAllocation()) {
    auto C = dyn_cast<ConstantInt>(AI->getArraySize());
    if (!C)
      return 0;
    Size *= C->getZExtValue();
  }
  return Size;
}

ConstantRange StackSafety::OffsetFromAlloca(Value *Addr,
                                            const Value *AllocaPtr) {
  if (!SE.isSCEVable(Addr->getType()))
    return ConstantRange(64);

  AllocaOffsetRewriter Rewriter(SE, AllocaPtr);
  const SCEV *Expr = Rewriter.visit(SE.getSCEV(Addr));
  ConstantRange OffsetRange = SE.getUnsignedRange(Expr).zextOrTrunc(64);
  return OffsetRange;
  // if (!OffsetRange.isSingleElement())
  //   return None;

  // return OffsetRange.getSingleElement()->getZExtValue();
}

ConstantRange StackSafety::GetAccessRange(Value *Addr, const Value *AllocaPtr,
                                          uint64_t AccessSize) {
  if (!SE.isSCEVable(Addr->getType()))
    return ConstantRange(64);

  AllocaOffsetRewriter Rewriter(SE, AllocaPtr);
  const SCEV *Expr = Rewriter.visit(SE.getSCEV(Addr));

  // uint64_t BitWidth = SE.getTypeSizeInBits(Expr->getType());
  ConstantRange AccessStartRange = SE.getUnsignedRange(Expr).zextOrTrunc(64);
  ConstantRange SizeRange =
      ConstantRange(APInt(64, 0), APInt(64, AccessSize));
  ConstantRange AccessRange = AccessStartRange.add(SizeRange);
  return AccessRange;
}

ConstantRange StackSafety::GetMemIntrinsicAccessRange(const MemIntrinsic *MI,
                                                      const Use &U,
                                                      const Value *AllocaPtr) {
  if (auto MTI = dyn_cast<MemTransferInst>(MI)) {
    if (MTI->getRawSource() != U && MTI->getRawDest() != U)
      return ConstantRange(APInt(64, 0), APInt(64, 1));
  } else {
    if (MI->getRawDest() != U)
      return ConstantRange(APInt(64, 0), APInt(64, 1));
  }
  const auto *Len = dyn_cast<ConstantInt>(MI->getLength());
  // Non-constant size => unsafe. FIXME: try SCEV getRange.
  if (!Len)
    return ConstantRange(64);
  ConstantRange AccessRange = GetAccessRange(U, AllocaPtr, Len->getZExtValue());
  // errs() << "memintrinsic " << (Safe ? "safe" : "unsafe") << *MI << "\n";
  return AccessRange;
}

/// Check whether a given allocation must be put on the safe
/// stack or not. The function analyzes all uses of AI and checks whether it is
/// only accessed in a memory safe way (as decided statically).
bool StackSafety::analyzeAllUses(Value *Ptr, UseSummary &US) {
  // const Value *AllocaPtr = AS.AI;
  // uint64_t AllocaSize = AS.Size;
  // ConstantRange AllocaRange =
  //     ConstantRange(APInt(64, 0), APInt(64, AllocaSize));

  // Go through all uses of this alloca and check whether all accesses to the
  // allocated object are statically known to be memory safe and, hence, the
  // object can be placed on the safe stack.
  SmallPtrSet<const Value *, 16> Visited;
  SmallVector<const Value *, 8> WorkList;
  WorkList.push_back(Ptr);

  // A DFS search through all uses of the alloca in bitcasts/PHI/GEPs/etc.
  while (!WorkList.empty()) {
    const Value *V = WorkList.pop_back_val();
    for (const Use &UI : V->uses()) {
      auto I = cast<const Instruction>(UI.getUser());
      assert(V == UI.get());

      switch (I->getOpcode()) {
      case Instruction::Load: {
        ConstantRange AccessRange =
            GetAccessRange(UI, Ptr, DL.getTypeStoreSize(I->getType()));
        // LLVM_DEBUG(dbgs() << *I << "\n    load with range " << AccessRange << "\n");
        if (!US.Range.contains(AccessRange)) {
          US.BadI = I;
          US.Reason = "load oob";
        }
        US.Range = US.Range.unionWith(AccessRange);
        break;
      }

      case Instruction::VAArg:
        // "va-arg" from a pointer is safe.
        break;
      case Instruction::Store: {
        if (V == I->getOperand(0)) {
          // Stored the pointer - conservatively assume it may be unsafe.
          US.Range = ConstantRange(64);
          US.BadI = I;
          US.Reason = "store leak";

          // LLVM_DEBUG(dbgs() << "[StackSafety] Unsafe alloca: " << *Ptr
          //              << "\n            store of address: " << *I << "\n");
          return false;
        }

        ConstantRange AccessRange = GetAccessRange(
            UI, Ptr, DL.getTypeStoreSize(I->getOperand(0)->getType()));
        // LLVM_DEBUG(dbgs() << *I << "\n    store with range " << AccessRange << "\n");
        if (!US.Range.contains(AccessRange)) {
          US.BadI = I;
          US.Reason = "store oob";
        }
        US.Range = US.Range.unionWith(AccessRange);
        break;
      }

      case Instruction::Ret:
        // Information leak.
        US.Range = ConstantRange(64);
        US.BadI = I;
        US.Reason = "ret leak";
        return false;

      case Instruction::Call:
      case Instruction::Invoke: {
        ImmutableCallSite CS(I);

        if (const IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
          if (II->getIntrinsicID() == Intrinsic::lifetime_start ||
              II->getIntrinsicID() == Intrinsic::lifetime_end)
            break;
        }

        if (const MemIntrinsic *MI = dyn_cast<MemIntrinsic>(I)) {
          ConstantRange AccessRange = GetMemIntrinsicAccessRange(MI, UI, Ptr);
          // LLVM_DEBUG(dbgs() << *I << "\n    memintrinsic with range " << AccessRange << "\n");
          if (!US.Range.contains(AccessRange)) {
            US.BadI = I;
            US.Reason = "memintrinsic oob";
          }
          US.Range = US.Range.unionWith(AccessRange);
          break;
        }

        // FIXME: consult devirt?
        const Function *Callee =
            dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts());
        if (!Callee) {
          US.BadI = I;
          US.Reason = "indirect call";
          US.Range = ConstantRange(64);
          return false;
        }
        if (!Callee->isDSOLocal()) {
          US.BadI = I;
          US.Reason = "dso_preemptable symbol";
          US.Range = ConstantRange(64);
          return false;
        }
        ImmutableCallSite::arg_iterator B = CS.arg_begin(), E = CS.arg_end();
        for (ImmutableCallSite::arg_iterator A = B; A != E; ++A) {
          if (A->get() == V) {
            ConstantRange OffsetRange = OffsetFromAlloca(UI, Ptr);
            US.Calls.push_back(
                UseSummary::CallSummary(Callee->getName(), A - B, OffsetRange));
          }
        }
        // Don't visit function return value: if it depends on the alloca, then
        // argument range would be full-set.

        // if (Visited.insert(I).second)
        //   WorkList.push_back(cast<const Instruction>(I));
        break;
      }

      default:
        if (Visited.insert(I).second)
          WorkList.push_back(cast<const Instruction>(I));
      }
    }
  }

  return true;
}

bool StackSafety::run(FunctionSummary &FS) {
  assert(!F.isDeclaration() && "Can't run StackSafety on a function declaration");

  LLVM_DEBUG(dbgs() << "[StackSafety] " << F.getName() << "\n");

  for (Instruction &I : instructions(&F)) {
    if (auto AI = dyn_cast<AllocaInst>(&I)) {
      uint64_t Size = getStaticAllocaAllocationSize(AI);
      FS.Allocas.push_back(AllocaSummary(AI, Size));
      AllocaSummary &AS = FS.Allocas.back();
      analyzeAllUses(AI, AS.Summary);
      AS.Summary.LocalRange = AS.Summary.Range;
    }
  }

  unsigned ArgNo = 0;
  for (Function::arg_iterator FAI = F.arg_begin(), FAE = F.arg_end();
       FAI != FAE; ++FAI, ++ArgNo) {
    Argument &A = *FAI;
    FS.Params.push_back(ParamSummary());
    ParamSummary &PS = FS.Params.back();
    analyzeAllUses(&A, PS.Summary);
    PS.Summary.LocalRange = PS.Summary.Range;
  }

  LLVM_DEBUG(dbgs() << "[StackSafety] done\n");
  return true;
}

class StackSafetyAnalysis {
  StringMap<FunctionSummary> Functions;
  
public:

  // void getAnalysisUsage(AnalysisUsage &AU) const override {
  //   AU.addRequired<TargetPassConfig>();
  //   AU.addRequired<TargetLibraryInfoWrapperPass>();
  //   AU.addRequired<AssumptionCacheTracker>();
  // }

  // bool analyzeFunction(Function &F, FunctionSummary &Summary,
  //                      ScalarEvolution *SE) {
  //   if (F.isDeclaration()) {
  //     LLVM_DEBUG(dbgs() << "[StackSafety]     function definition"
  //                     " is not available\n");
  //     return false;
  //   }

  //   LLVM_DEBUG(dbgs() << "[StackSafety] Function: " << F.getName() << "\n");

  //   auto *DL = &F.getParent()->getDataLayout();
  //   auto &TLI = getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();
  //   auto &ACT = getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);

  //   // Compute DT and LI only for functions that have the attribute.
  //   // This is only useful because the legacy pass manager doesn't let us
  //   // compute analyzes lazily.
  //   // In the backend pipeline, nothing preserves DT before SafeStack, so we
  //   // would otherwise always compute it wastefully, even if there is no
  //   // function with the safestack attribute.
  //   DominatorTree DT(F);
  //   LoopInfo LI(DT);

  //   ScalarEvolution SE(F, TLI, ACT, DT, LI);

  //   StackSafety SS(&F.getParent()->getDataLayout(), *DL, SE);
  //   SS.run(Summary);
  //   return true;
  // }

  ConstantRange getArgumentAccessRange(StringRef Name, unsigned ParamNo, bool Local = false) {
    auto IT = Functions.find(Name);
    // Unknown callee (outside of LTO domain, dso_preemptable, or an indirect call).
    if (IT == Functions.end())
      return ConstantRange(64);
    FunctionSummary &FS = IT->getValue();
    if (ParamNo >= FS.Params.size()) // possibly vararg
      return ConstantRange(64);
    return Local ? FS.Params[ParamNo].Summary.LocalRange : FS.Params[ParamNo].Summary.Range;
  }

  void printCallWithOffset(StringRef Callee, unsigned ParamNo,
                           ConstantRange Offset, StringRef Indent) {
    dbgs() << Indent << "=> " << Callee << "(#" << ParamNo << ", +"
           << Offset << ")\n";
  }

  void describeCallIfUnsafe(ConstantRange AllocaRange, ConstantRange PtrRange,
                            UseSummary::CallSummary &CS,
                            std::string Indent, StringSet<> &Visited) {
    ConstantRange ParamRange = PtrRange.add(CS.Range);

    if (Visited.count(CS.Callee)) {
      printCallWithOffset(CS.Callee, CS.ParamNo, ParamRange, Indent);
      dbgs() << Indent << "  <recursion>\n";
      return;
    }
    Visited.insert(CS.Callee);

    auto IT = Functions.find(CS.Callee);
    // Unknown callee (outside of LTO domain, dso_preemptable, or an indirect call).
    if (IT == Functions.end()) {
      printCallWithOffset(CS.Callee, CS.ParamNo, ParamRange, Indent);
      dbgs() << Indent << "  external call\n";
      return;
    }

    FunctionSummary &FS = IT->getValue();
    if (CS.ParamNo >= FS.Params.size()) {
      printCallWithOffset(CS.Callee, CS.ParamNo, ParamRange, Indent);
      dbgs() << Indent << "  unknown argument\n";
      return;
    }

    ParamSummary &PS = FS.Params[CS.ParamNo];
    ConstantRange CalleeRange = ParamRange.add(PS.Summary.Range);
    bool Safe = AllocaRange.contains(CalleeRange);
    if (Safe)
      return;

    ConstantRange CalleeLocalRange = ParamRange.add(PS.Summary.LocalRange);
    bool LocalSafe = AllocaRange.contains(CalleeLocalRange);
    if (!LocalSafe) {
      printCallWithOffset(CS.Callee, CS.ParamNo, ParamRange, Indent);
      if (PS.Summary.BadI) {
        dbgs() << Indent << "  " << PS.Summary.Reason << ": " << *PS.Summary.BadI << "\n";
      } else {
        dbgs() << Indent << "  unsafe local access (unknown)\n";
      }
      return;
    }

    for (auto &OtherCS : PS.Summary.Calls) {
      printCallWithOffset(CS.Callee, CS.ParamNo, ParamRange, Indent);
      describeCallIfUnsafe(AllocaRange, ParamRange.add(OtherCS.Range), OtherCS,
                           Indent + "  ", Visited);
    }
  }

  bool describeAlloca(AllocaSummary &AS) {
    dbgs() << "    alloca %" << AS.AI->getName() << " [" << AS.Size << " bytes]\n";
    ConstantRange AllocaRange{APInt(64, 0), APInt(64, AS.Size)};
    bool Safe = AllocaRange.contains(AS.Summary.Range);
    if (Safe) {
      dbgs() << "      safe\n";
      return true;
    }
    bool LocalSafe = AllocaRange.contains(AS.Summary.LocalRange);
    if (!LocalSafe) {
      if (AS.Summary.BadI) {
        dbgs() << "      " << AS.Summary.Reason << ": " << *AS.Summary.BadI << "\n";
      } else {
        dbgs() << "      unsafe local access (unknown)\n";
      }
      return false;
    }

    StringSet<> Visited;
    for (auto &CS : AS.Summary.Calls) {
      describeCallIfUnsafe(AllocaRange,
                           ConstantRange(APInt(64, 0), APInt(64, 1)), CS,
                           "      ", Visited);
    }
    return false;
  }

  void describeFunction(StringRef Name, FunctionSummary &FS) {
    dbgs() << "  @" << Name << "\n";
    bool Safe = true;
    for (auto &AS : FS.Allocas) {
      Safe &= describeAlloca(AS);
    }
    if (Safe)
      dbgs() << "    function-safe\n";
  }

  bool addMetadata(Function &F, FunctionSummary &Summary) {
    bool Changed = false;
    for (auto &AS : Summary.Allocas) {
      ConstantRange AllocaRange{APInt(64, 0), APInt(64, AS.Size)};
      bool Safe = AllocaRange.contains(AS.Summary.Range);
      if (!Safe)
        continue;
      Changed = true;
      Module *M = F.getParent();
      AS.AI->setMetadata(M->getMDKindID("stack-safe"),
                         MDNode::get(M->getContext(), None));
    }
    return Changed;
  }

  bool updateOneValue(UseSummary &US, bool UpdateToFullSet) {
    bool Changed = false;
    for (auto &CS : US.Calls) {
      ConstantRange CalleeRange = getArgumentAccessRange(CS.Callee, CS.ParamNo);
      CalleeRange = CalleeRange.add(CS.Range);
      if (!US.Range.contains(CalleeRange)) {
        Changed = true;
        if (UpdateToFullSet)
          US.Range = ConstantRange(64, true);
        else
          US.Range = US.Range.unionWith(CalleeRange);
      }
    }
    return Changed;
  }

  bool runOneIteration(int IterNo, bool UpdateToFullSet) {
    bool Changed = false;
    // FIXME: depth-first?
    for (auto &FN : Functions) {
      FunctionSummary &FP = FN.getValue();
      for (auto &AS : FP.Allocas)
        Changed |= updateOneValue(AS.Summary, UpdateToFullSet);
      for (auto &PS : FP.Params)
        Changed |= updateOneValue(PS.Summary, UpdateToFullSet);
    }
    LLVM_DEBUG(dbgs() << "=== iteration " << IterNo << " "
                 << (UpdateToFullSet ? "(full-set)" : "")
                 << (Changed ? "(changed)" : "") << "\n");
    LLVM_DEBUG(for (auto &FN : Functions) FN.getValue().dump(FN.getKey()));
    return Changed;
  }

  bool runDataFlow() {
    for (int IterNo = 0; IterNo < StackSafetyMaxIterations; ++IterNo)
      if (!runOneIteration(IterNo, false))
        return true;

    for (int IterNo = 0; IterNo < StackSafetyMaxIterations; ++IterNo)
      if (!runOneIteration(IterNo, true))
        return true;

    return false;
  }

  bool run(Module &M,
           std::function<ScalarEvolution *(const Function &F)> GetSECallback) {
    for (auto &F : M.functions()) {
      if (!F.isDeclaration()) {
        StackSafety SS(F, F.getParent()->getDataLayout(), *GetSECallback(F));
        SS.run(Functions[F.getName()]);
      }
    }

    LLVM_DEBUG(for (auto &FN : Functions) FN.getValue().dump(FN.getKey()));

    if (!runDataFlow()) {
      LLVM_DEBUG(dbgs() << "[stack-safety] Could not reach fixed point!\n");
      return false;
    }

    LLVM_DEBUG(dbgs() << "============!!!\n");
    LLVM_DEBUG(for (auto &FN : Functions) describeFunction(FN.getKey(), FN.getValue()));
    return true;
  }

  bool addAllMetadata(Module &M) {
    bool Changed = false;
    for (auto &F : M.functions())
      if (!F.isDeclaration())
        Changed |= addMetadata(F, Functions[F.getName()]);

    return Changed;
  }
};
} // end anonymous namespace

namespace llvm {

StackSafetyWrapperPass::StackSafetyWrapperPass() : ModulePass(ID) {
  initializeStackSafetyWrapperPassPass(*PassRegistry::getPassRegistry());
}

void StackSafetyWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<ScalarEvolutionWrapperPass>();
}

bool StackSafetyWrapperPass::runOnModule(Module &M) {
  StackSafetyAnalysis SSA;
  bool Success = SSA.run(M, [this](const Function &F) {
    return &this->getAnalysis<ScalarEvolutionWrapperPass>(
                    *const_cast<Function *>(&F))
                .getSE();
  });
  if (!Success)
    return false;
  return SSA.addAllMetadata(M);
}

char StackSafetyWrapperPass::ID = 0;

} // namespace llvm

INITIALIZE_PASS_BEGIN(StackSafetyWrapperPass, DEBUG_TYPE,
                      "Stack safety analysis pass", false, false)
INITIALIZE_PASS_END(StackSafetyWrapperPass, DEBUG_TYPE,
                    "Stack safety analysis pass", false, false)

ModulePass *llvm::createStackSafetyWrapperPass() { return new StackSafetyWrapperPass(); }
