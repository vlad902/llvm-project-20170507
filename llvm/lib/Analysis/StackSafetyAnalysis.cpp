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
                                             cl::init(20), cl::Hidden);

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

using FunctionID = GlobalValue::GUID;

struct SSUseSummary {
  ConstantRange Range;
  ConstantRange LocalRange;
  const Instruction *BadI;
  const char *Reason;

  struct SSCallSummary {
    Function *F;
    FunctionID Callee;
    unsigned ParamNo;
    ConstantRange Range;
    SSCallSummary(Function *F, unsigned ParamNo)
        : F(F), Callee(F->getGUID()), ParamNo(ParamNo), Range(64, false) {}
    SSCallSummary(Function *F, unsigned ParamNo, ConstantRange Range)
        : F(F), Callee(F->getGUID()), ParamNo(ParamNo), Range(Range) {}
    SSCallSummary(FunctionID Callee, unsigned ParamNo, ConstantRange Range)
        : F(nullptr), Callee(Callee), ParamNo(ParamNo), Range(Range) {}
    std::string name() {
      if (F)
        return "@" + F->getName().str();
      return "#" + utostr(Callee);
    }
  };
  SmallVector<SSCallSummary, 4> Calls;

  SSUseSummary()
      : Range(64, false), LocalRange(64, false), BadI(nullptr),
        Reason(nullptr) {}
  void dump() {
    dbgs() << Range;
    for (auto &Call : Calls) {
      dbgs() << ", " << Call.name() << "[#" << Call.ParamNo << ", offset "
             << Call.Range << "]";
    }
    dbgs() << "\n";
  }
};

struct SSAllocaSummary {
  AllocaInst *AI;
  uint64_t Size;
  SSUseSummary Summary;

  SSAllocaSummary(AllocaInst *AI, uint64_t Size) : AI(AI), Size(Size) {}
  void dump() {
    dbgs() << "    alloca [" << Size << " bytes]";
    if (AI)
      dbgs() << " %" << AI->getName();
    dbgs() << "\n      ";
    Summary.dump();
  }
};

struct SSParamSummary {
  SSUseSummary Summary;

  void dump(unsigned ParamNo) {
    dbgs() << "    arg #" << ParamNo << "\n      ";
    Summary.dump();
  }
};

} // end anonymous namespace

namespace llvm {

// FunctionStackSummary could also describe return value as depending on one or
// more of its arguments.
struct SSFunctionSummary {
  Function *F;
  SmallVector<SSAllocaSummary, 4> Allocas;
  SmallVector<SSParamSummary, 4> Params;
  unsigned DSOLocal : 1;
  unsigned Interposable : 1;

  std::string name(FunctionID ID) {
    if (F)
      return "@" + F->getName().str();

    std::string result = "#" + utostr(ID);
    if (FS)
      result += ("[" + FS->modulePath() + "]").str();
    return result;
  }
  void dump(FunctionID ID) {
    dbgs() << "  " << name(ID) << "\n";
    for (unsigned i = 0; i < Params.size(); ++i)
      Params[i].dump(i);
    for (auto &AS : Allocas)
      AS.dump();
  }
};

StackSafetyResults::StackSafetyResults(
    std::unique_ptr<SSFunctionSummary> Summary)
    : Summary(std::move(Summary)) {}
StackSafetyResults::~StackSafetyResults() {}

} // end namespace llvm

namespace {

class StackSafetyLocalAnalysis {
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

  bool analyzeAllUses(Value *Ptr, SSUseSummary &AS);

public:
  StackSafetyLocalAnalysis(Function &F, const DataLayout &DL,
                           ScalarEvolution &SE)
      : F(F), DL(DL), SE(SE), StackPtrTy(Type::getInt8PtrTy(F.getContext())),
        IntPtrTy(DL.getIntPtrType(F.getContext())),
        Int32Ty(Type::getInt32Ty(F.getContext())),
        Int8Ty(Type::getInt8Ty(F.getContext())) {}

  // Run the transformation on the associated function.
  // Returns whether the function was changed.
  bool run(SSFunctionSummary &);
};

uint64_t
StackSafetyLocalAnalysis::getStaticAllocaAllocationSize(const AllocaInst *AI) {
  uint64_t Size = DL.getTypeAllocSize(AI->getAllocatedType());
  if (AI->isArrayAllocation()) {
    auto C = dyn_cast<ConstantInt>(AI->getArraySize());
    if (!C)
      return 0;
    Size *= C->getZExtValue();
  }
  return Size;
}

ConstantRange
StackSafetyLocalAnalysis::OffsetFromAlloca(Value *Addr,
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

ConstantRange StackSafetyLocalAnalysis::GetAccessRange(Value *Addr,
                                                       const Value *AllocaPtr,
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

ConstantRange StackSafetyLocalAnalysis::GetMemIntrinsicAccessRange(
    const MemIntrinsic *MI, const Use &U, const Value *AllocaPtr) {
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
bool StackSafetyLocalAnalysis::analyzeAllUses(Value *Ptr, SSUseSummary &US) {
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

        ImmutableCallSite::arg_iterator B = CS.arg_begin(), E = CS.arg_end();
        for (ImmutableCallSite::arg_iterator A = B; A != E; ++A) {
          if (A->get() == V) {
            ConstantRange OffsetRange = OffsetFromAlloca(UI, Ptr);
            US.Calls.push_back(SSUseSummary::SSCallSummary(
                const_cast<Function*>(Callee), A - B, OffsetRange));
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

bool StackSafetyLocalAnalysis::run(SSFunctionSummary &FS) {
  assert(!F.isDeclaration() &&
         "Can't run StackSafety on a function declaration");

  LLVM_DEBUG(dbgs() << "[StackSafety] " << F.getName() << "\n");
  FS.F = &F;
  FS.DSOLocal = F.isDSOLocal();
  FS.Interposable = F.isInterposable();

  for (Instruction &I : instructions(&F)) {
    if (auto AI = dyn_cast<AllocaInst>(&I)) {
      uint64_t Size = getStaticAllocaAllocationSize(AI);
      FS.Allocas.push_back(SSAllocaSummary(AI, Size));
      SSAllocaSummary &AS = FS.Allocas.back();
      analyzeAllUses(AI, AS.Summary);
      AS.Summary.LocalRange = AS.Summary.Range;
    }
  }

  unsigned ArgNo = 0;
  for (Function::arg_iterator FAI = F.arg_begin(), FAE = F.arg_end();
       FAI != FAE; ++FAI, ++ArgNo) {
    Argument &A = *FAI;
    FS.Params.push_back(SSParamSummary());
    SSParamSummary &PS = FS.Params.back();
    analyzeAllUses(&A, PS.Summary);
    PS.Summary.LocalRange = PS.Summary.Range;
  }

  LLVM_DEBUG(dbgs() << "[StackSafety] done\n");
  return true;
}

class StackSafetyDataFlowAnalysis {
public:
  using FunctionMap = DenseMap<FunctionID, std::unique_ptr<SSFunctionSummary>>;
  StackSafetyDataFlowAnalysis(FunctionMap &Functions) : Functions(Functions) {}

  bool run();
  bool addAllMetadata(Module &M);

private:
  ConstantRange getArgumentAccessRange(FunctionID ID, unsigned ParamNo,
                                       bool Local);
  void printCallWithOffset(FunctionID Callee, unsigned ParamNo,
                           ConstantRange Offset, StringRef Indent);
  void describeCallIfUnsafe(ConstantRange AllocaRange, ConstantRange PtrRange,
                            SSUseSummary::SSCallSummary &CS, std::string Indent,
                            DenseSet<FunctionID> &Visited);
  bool describeAlloca(SSAllocaSummary &AS);
  void describeFunction(FunctionID ID, SSFunctionSummary &FS);
  bool addMetadata(Function &F, SSFunctionSummary &Summary);
  bool updateOneUse(SSUseSummary &US, bool UpdateToFullSet);
  void updateOneNode(FunctionID ID, SSFunctionSummary &FS);
  void runDataFlow();
  void verifyFixedPoint();

  FunctionMap &Functions;
  // Callee-to-Caller multimap.
  DenseMap<FunctionID, SmallVector<FunctionID, 4>> Callers;
  SmallVector<FunctionID, 64> WorkList;
  DenseMap<FunctionID, int> UpdateCount;
};

ConstantRange StackSafetyDataFlowAnalysis::getArgumentAccessRange(
    FunctionID ID, unsigned ParamNo, bool Local = false) {
  auto IT = Functions.find(ID);
  // Unknown callee (outside of LTO domain or an indirect call).
  if (IT == Functions.end())
    return ConstantRange(64);
  SSFunctionSummary &FS = *IT->second;
  // The definition of this symbol may not be the definition in this linkage
  // unit.
  if (!FS.DSOLocal || FS.Interposable)
    return ConstantRange(64);
  if (ParamNo >= FS.Params.size()) // possibly vararg
    return ConstantRange(64);
  return Local ? FS.Params[ParamNo].Summary.LocalRange
               : FS.Params[ParamNo].Summary.Range;
}

void StackSafetyDataFlowAnalysis::printCallWithOffset(FunctionID Callee,
                                                      unsigned ParamNo,
                                                      ConstantRange Offset,
                                                      StringRef Indent) {
  if (Functions.count(Callee))
    dbgs() << Indent << "=> " << Functions[Callee]->name(Callee);
  else
    dbgs() << Indent << "=> #" << Callee;

  dbgs() << "(#" << ParamNo << ", +" << Offset << ")\n";
}

void StackSafetyDataFlowAnalysis::describeCallIfUnsafe(
    ConstantRange AllocaRange, ConstantRange PtrRange,
    SSUseSummary::SSCallSummary &CS, std::string Indent,
    DenseSet<FunctionID> &Visited) {
  ConstantRange ParamRange = PtrRange.add(CS.Range);

  if (Visited.count(CS.Callee)) {
    printCallWithOffset(CS.Callee, CS.ParamNo, ParamRange, Indent);
    dbgs() << Indent << "  <recursion>\n";
    return;
  }
  Visited.insert(CS.Callee);

  auto IT = Functions.find(CS.Callee);
  // Unknown callee (outside of LTO domain or an indirect call).
  if (IT == Functions.end()) {
    printCallWithOffset(CS.Callee, CS.ParamNo, ParamRange, Indent);
    dbgs() << Indent << "  external call\n";
    return;
  }

  SSFunctionSummary &FS = *IT->second;
  // The definition of this symbol may not be the definition in this linkage
  // unit.
  if (!FS.DSOLocal || FS.Interposable) {
    printCallWithOffset(CS.Callee, CS.ParamNo, ParamRange, Indent);
    dbgs() << Indent << "  " << (FS.DSOLocal ? "" : "dso_preemptable ")
           << (FS.Interposable ? "interposable" : "") << "\n";
    return;
  }
  if (CS.ParamNo >= FS.Params.size()) {
    printCallWithOffset(CS.Callee, CS.ParamNo, ParamRange, Indent);
    dbgs() << Indent << "  unknown argument\n";
    return;
  }

  SSParamSummary &PS = FS.Params[CS.ParamNo];
  ConstantRange CalleeRange = ParamRange.add(PS.Summary.Range);
  bool Safe = AllocaRange.contains(CalleeRange);
  if (Safe)
    return;

  ConstantRange CalleeLocalRange = ParamRange.add(PS.Summary.LocalRange);
  bool LocalSafe = AllocaRange.contains(CalleeLocalRange);
  if (!LocalSafe) {
    printCallWithOffset(CS.Callee, CS.ParamNo, ParamRange, Indent);
    if (PS.Summary.BadI) {
      dbgs() << Indent << "  " << PS.Summary.Reason << ": " << *PS.Summary.BadI
             << "\n";
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

bool StackSafetyDataFlowAnalysis::describeAlloca(SSAllocaSummary &AS) {
  dbgs() << "    alloca [" << AS.Size << " bytes]";
  if (AS.AI)
    dbgs() << " %" << AS.AI->getName();
  dbgs() << "\n";
  ConstantRange AllocaRange{APInt(64, 0), APInt(64, AS.Size)};
  bool Safe = AllocaRange.contains(AS.Summary.Range);
  if (Safe) {
    dbgs() << "      safe\n";
    return true;
  }
  bool LocalSafe = AllocaRange.contains(AS.Summary.LocalRange);
  if (!LocalSafe) {
    if (AS.Summary.BadI) {
      dbgs() << "      " << AS.Summary.Reason << ": " << *AS.Summary.BadI
             << "\n";
    } else {
      dbgs() << "      unsafe local access (unknown)\n";
    }
    return false;
  }

  DenseSet<FunctionID> Visited;
  for (auto &CS : AS.Summary.Calls) {
    describeCallIfUnsafe(AllocaRange, ConstantRange(APInt(64, 0), APInt(64, 1)),
                         CS, "      ", Visited);
  }
  return false;
}

void StackSafetyDataFlowAnalysis::describeFunction(FunctionID ID,
                                                   SSFunctionSummary &FS) {
  dbgs() << "  " << Functions[ID]->name(ID) << "\n";
  bool Safe = true;
  for (auto &AS : FS.Allocas) {
    Safe &= describeAlloca(AS);
  }
  if (Safe)
    dbgs() << "    function-safe\n";
}

bool StackSafetyDataFlowAnalysis::addMetadata(Function &F,
                                              SSFunctionSummary &Summary) {
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

bool StackSafetyDataFlowAnalysis::updateOneUse(SSUseSummary &US,
                                                 bool UpdateToFullSet) {
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

void StackSafetyDataFlowAnalysis::updateOneNode(FunctionID ID,
                                                SSFunctionSummary &FS) {
  auto IT = UpdateCount.find(ID);
  bool UpdateToFullSet =
      IT != UpdateCount.end() && IT->second > StackSafetyMaxIterations;
  bool Changed = false;
  for (auto &AS : FS.Allocas)
    Changed |= updateOneUse(AS.Summary, UpdateToFullSet);
  for (auto &PS : FS.Params)
    Changed |= updateOneUse(PS.Summary, UpdateToFullSet);

  if (Changed) {
    LLVM_DEBUG(dbgs() << "=== update [" << UpdateCount[ID]
                      << (UpdateToFullSet ? ", full-set" : "") << "] "
                      << Functions[ID]->name(ID) << "\n");
    // Callers of this function may need updating.
    WorkList.append(Callers[ID].begin(), Callers[ID].end());
    UpdateCount[ID]++;
  }
}

void StackSafetyDataFlowAnalysis::runDataFlow() {
  Callers.clear();
  WorkList.clear();

  for (auto &FN : Functions) {
    FunctionID Caller = FN.first;
    SSFunctionSummary &FS = *FN.second;
    for (auto &AS : FS.Allocas)
      for (auto &CS  : AS.Summary.Calls)
	Callers[CS.Callee].push_back(Caller);
    for (auto &PS : FS.Params)
      for (auto &CS  : PS.Summary.Calls)
	Callers[CS.Callee].push_back(Caller);
  }

  for (auto &FN : Functions)
    updateOneNode(FN.first, *FN.second);

  while (!WorkList.empty()) {
    FunctionID ID = WorkList.back();
    WorkList.pop_back();
    updateOneNode(ID, *Functions[ID]);
  }
}

void StackSafetyDataFlowAnalysis::verifyFixedPoint() {
  WorkList.clear();
  for (auto &FN : Functions)
    updateOneNode(FN.first, *FN.second);
  assert(WorkList.empty());
}

bool StackSafetyDataFlowAnalysis::run() {
  LLVM_DEBUG(for (auto &FN : Functions) FN.second->dump(FN.first));

  runDataFlow();
  verifyFixedPoint(); // Only in Release+Asserts?

  LLVM_DEBUG(dbgs() << "============!!!\n");
  LLVM_DEBUG(for (auto &FN : Functions) describeFunction(FN.first, *FN.second));
  return true;
}

bool StackSafetyDataFlowAnalysis::addAllMetadata(Module &M) {
  bool Changed = false;
  for (auto &F : M.functions())
    if (!F.isDeclaration())
      Changed |= addMetadata(F, *Functions[F.getGUID()]);

  return Changed;
}

  return Changed;
}

} // end anonymous namespace

namespace llvm {

StackSafetyResults StackSafetyInfo::run(Function &F) const {
  StackSafetyLocalAnalysis SSLA(F, F.getParent()->getDataLayout(),
                                *GetSECallback(F));
  std::unique_ptr<SSFunctionSummary> Summary =
      llvm::make_unique<SSFunctionSummary>();
  SSLA.run(*Summary);
  LLVM_DEBUG(Summary->dump(F.getGUID()));
  return StackSafetyResults(std::move(Summary));
}

StackSafetyInfoWrapperPass::StackSafetyInfoWrapperPass() : ModulePass(ID) {
  initializeStackSafetyInfoWrapperPassPass(*PassRegistry::getPassRegistry());
}

void StackSafetyInfoWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<ScalarEvolutionWrapperPass>();
  AU.setPreservesAll();
}

bool StackSafetyInfoWrapperPass::runOnModule(Module &M) { return false; }

bool StackSafetyInfoWrapperPass::doInitialization(Module &M) {
  SSI.reset(new StackSafetyInfo([this](const Function &F) {
    return &this->getAnalysis<ScalarEvolutionWrapperPass>(
                    *const_cast<Function *>(&F))
                .getSE();
  }));
  return false;
}

bool StackSafetyInfoWrapperPass::doFinalization(Module &M) {
  SSI.reset();
  return false;
}

char StackSafetyInfoWrapperPass::ID = 0;

AnalysisKey StackSafetyAnalysis::Key;

StackSafetyInfo StackSafetyAnalysis::run(Module &M, ModuleAnalysisManager &AM) {
  auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  return StackSafetyInfo([&FAM](const Function &F) {
    return &FAM.getResult<ScalarEvolutionAnalysis>(*const_cast<Function *>(&F));
  });
}

StackSafetyGlobalAnalysis::StackSafetyGlobalAnalysis() : ModulePass(ID) {
  initializeStackSafetyGlobalAnalysisPass(*PassRegistry::getPassRegistry());
}

void StackSafetyGlobalAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<StackSafetyInfoWrapperPass>();
}

bool StackSafetyGlobalAnalysis::runOnModule(Module &M) {
  StackSafetyInfo &SSI = getAnalysis<StackSafetyInfoWrapperPass>().getSSI();
  StackSafetyDataFlowAnalysis::FunctionMap Functions;
  for (auto &F : M.functions())
    if (!F.isDeclaration())
      Functions[F.getGUID()] = std::move(SSI.run(F).Summary);

  StackSafetyDataFlowAnalysis SSDFA(Functions);
  if (!SSDFA.run())
    return false;

  return SSDFA.addAllMetadata(M);
}

char StackSafetyGlobalAnalysis::ID = 0;

} // namespace llvm

INITIALIZE_PASS_BEGIN(StackSafetyInfoWrapperPass, "stack-safety-local",
                      "Stack safety local analysis pass", false, true)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
INITIALIZE_PASS_END(StackSafetyInfoWrapperPass, "stack-safety-local",
                    "Stack safety local analysis pass", false, true)

INITIALIZE_PASS_BEGIN(StackSafetyGlobalAnalysis, "stack-safety",
                      "Stack safety global analysis pass", false, false)
INITIALIZE_PASS_DEPENDENCY(StackSafetyInfoWrapperPass)
INITIALIZE_PASS_END(StackSafetyGlobalAnalysis, "stack-safety",
                    "Stack safety global analysis pass", false, false)

ModulePass *llvm::createStackSafetyInfoWrapperPass() {
  return new StackSafetyInfoWrapperPass();
}
ModulePass *llvm::createStackSafetyGlobalAnalysis() {
  return new StackSafetyGlobalAnalysis();
}
