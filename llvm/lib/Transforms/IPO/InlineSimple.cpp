//===- MethodInlining.cpp - Code to perform method inlining ---------------===//
//
// This file implements inlining of methods.
//
// Specifically, this:
//   * Exports functionality to inline any method call
//   * Inlines methods that consist of a single basic block
//   * Is able to inline ANY method call
//   . Has a smart heuristic for when to inline a method
//
// Notice that:
//   * This pass has a habit of introducing duplicated constant pool entries, 
//     and also opens up a lot of opportunities for constant propogation.  It is
//     a good idea to to run a constant propogation pass, then a DCE pass 
//     sometime after running this pass.
//
// TODO: Currently this throws away all of the symbol names in the method being
//       inlined to try to avoid name clashes.  Use a name if it's not taken
//
//===----------------------------------------------------------------------===//

#include "llvm/Optimizations/MethodInlining.h"
#include "llvm/Module.h"
#include "llvm/Method.h"
#include "llvm/iTerminators.h"
#include "llvm/iOther.h"
#include <algorithm>
#include <map>

#include "llvm/Assembly/Writer.h"

using namespace opt;

// RemapInstruction - Convert the instruction operands from referencing the 
// current values into those specified by ValueMap.
//
static inline void RemapInstruction(Instruction *I, 
				    map<const Value *, Value*> &ValueMap) {

  for (unsigned op = 0, E = I->getNumOperands(); op != E; ++op) {
    const Value *Op = I->getOperand(op);
    Value *V = ValueMap[Op];
    if (!V && Op->isMethod()) 
      continue;  // Methods don't get relocated

    if (!V) {
      cerr << "Val = " << endl << Op << "Addr = " << (void*)Op << endl;
      cerr << "Inst = " << I;
    }
    assert(V && "Referenced value not in value map!");
    I->setOperand(op, V);
  }
}

// InlineMethod - This function forcibly inlines the called method into the
// basic block of the caller.  This returns false if it is not possible to
// inline this call.  The program is still in a well defined state if this 
// occurs though.
//
// Note that this only does one level of inlining.  For example, if the 
// instruction 'call B' is inlined, and 'B' calls 'C', then the call to 'C' now 
// exists in the instruction stream.  Similiarly this will inline a recursive
// method by one level.
//
bool opt::InlineMethod(BasicBlock::iterator CIIt) {
  assert((*CIIt)->getOpcode() == Instruction::Call && 
	 "InlineMethod only works on CallInst nodes!");
  assert((*CIIt)->getParent() && "Instruction not embedded in basic block!");
  assert((*CIIt)->getParent()->getParent() && "Instruction not in method!");

  CallInst *CI = (CallInst*)*CIIt;
  const Method *CalledMeth = CI->getCalledMethod();
  Method *CurrentMeth = CI->getParent()->getParent();

  //cerr << "Inlining " << CalledMeth->getName() << " into " 
  //     << CurrentMeth->getName() << endl;

  BasicBlock *OrigBB = CI->getParent();

  // Call splitBasicBlock - The original basic block now ends at the instruction
  // immediately before the call.  The original basic block now ends with an
  // unconditional branch to NewBB, and NewBB starts with the call instruction.
  //
  BasicBlock *NewBB = OrigBB->splitBasicBlock(CIIt);

  // Remove (unlink) the CallInst from the start of the new basic block.  
  NewBB->getInstList().remove(CI);

  // If we have a return value generated by this call, convert it into a PHI 
  // node that gets values from each of the old RET instructions in the original
  // method.
  //
  PHINode *PHI = 0;
  if (CalledMeth->getReturnType() != Type::VoidTy) {
    PHI = new PHINode(CalledMeth->getReturnType(), CI->getName());

    // The PHI node should go at the front of the new basic block to merge all 
    // possible incoming values.
    //
    NewBB->getInstList().push_front(PHI);

    // Anything that used the result of the function call should now use the PHI
    // node as their operand.
    //
    CI->replaceAllUsesWith(PHI);
  }

  // Keep a mapping between the original method's values and the new duplicated
  // code's values.  This includes all of: Method arguments, instruction values,
  // constant pool entries, and basic blocks.
  //
  map<const Value *, Value*> ValueMap;

  // Add the method arguments to the mapping: (start counting at 1 to skip the
  // method reference itself)
  //
  Method::ArgumentListType::const_iterator PTI = 
    CalledMeth->getArgumentList().begin();
  for (unsigned a = 1, E = CI->getNumOperands(); a != E; ++a, ++PTI)
    ValueMap[*PTI] = CI->getOperand(a);
  
  ValueMap[NewBB] = NewBB;  // Returns get converted to reference NewBB

  // Loop over all of the basic blocks in the method, inlining them as 
  // appropriate.  Keep track of the first basic block of the method...
  //
  for (Method::const_iterator BI = CalledMeth->begin(); 
       BI != CalledMeth->end(); ++BI) {
    const BasicBlock *BB = *BI;
    assert(BB->getTerminator() && "BasicBlock doesn't have terminator!?!?");
    
    // Create a new basic block to copy instructions into!
    BasicBlock *IBB = new BasicBlock("", NewBB->getParent());

    ValueMap[*BI] = IBB;                       // Add basic block mapping.

    // Make sure to capture the mapping that a return will use...
    // TODO: This assumes that the RET is returning a value computed in the same
    //       basic block as the return was issued from!
    //
    const TerminatorInst *TI = BB->getTerminator();
   
    // Loop over all instructions copying them over...
    Instruction *NewInst;
    for (BasicBlock::const_iterator II = BB->begin();
	 II != (BB->end()-1); ++II) {
      IBB->getInstList().push_back((NewInst = (*II)->clone()));
      ValueMap[*II] = NewInst;                  // Add instruction map to value.
    }

    // Copy over the terminator now...
    switch (TI->getOpcode()) {
    case Instruction::Ret: {
      const ReturnInst *RI = (const ReturnInst*)TI;

      if (PHI) {   // The PHI node should include this value!
	assert(RI->getReturnValue() && "Ret should have value!");
	assert(RI->getReturnValue()->getType() == PHI->getType() && 
	       "Ret value not consistent in method!");
	PHI->addIncoming((Value*)RI->getReturnValue(), (BasicBlock*)BB);
      }

      // Add a branch to the code that was after the original Call.
      IBB->getInstList().push_back(new BranchInst(NewBB));
      break;
    }
    case Instruction::Br:
      IBB->getInstList().push_back(TI->clone());
      break;

    default:
      cerr << "MethodInlining: Don't know how to handle terminator: " << TI;
      abort();
    }
  }


  // Copy over the constant pool...
  //
  const ConstantPool &CP = CalledMeth->getConstantPool();
  ConstantPool    &NewCP = CurrentMeth->getConstantPool();
  for (ConstantPool::plane_const_iterator PI = CP.begin(); PI != CP.end(); ++PI){
    ConstantPool::PlaneType &Plane = **PI;
    for (ConstantPool::PlaneType::const_iterator I = Plane.begin(); 
	 I != Plane.end(); ++I) {
      ConstPoolVal *NewVal = (*I)->clone(); // Copy existing constant
      NewCP.insert(NewVal);         // Insert the new copy into local const pool
      ValueMap[*I] = NewVal;        // Keep track of constant value mappings
    }
  }

  // Loop over all of the instructions in the method, fixing up operand 
  // references as we go.  This uses ValueMap to do all the hard work.
  //
  for (Method::const_iterator BI = CalledMeth->begin(); 
       BI != CalledMeth->end(); ++BI) {
    const BasicBlock *BB = *BI;
    BasicBlock *NBB = (BasicBlock*)ValueMap[BB];

    // Loop over all instructions, fixing each one as we find it...
    //
    for (BasicBlock::iterator II = NBB->begin(); II != NBB->end(); II++)
      RemapInstruction(*II, ValueMap);
  }

  if (PHI) RemapInstruction(PHI, ValueMap);  // Fix the PHI node also...

  // Change the branch that used to go to NewBB to branch to the first basic 
  // block of the inlined method.
  //
  TerminatorInst *Br = OrigBB->getTerminator();
  assert(Br && Br->getOpcode() == Instruction::Br && 
	 "splitBasicBlock broken!");
  Br->setOperand(0, ValueMap[CalledMeth->front()]);

  // Since we are now done with the CallInst, we can finally delete it.
  delete CI;
  return true;
}

bool opt::InlineMethod(CallInst *CI) {
  assert(CI->getParent() && "CallInst not embeded in BasicBlock!");
  BasicBlock *PBB = CI->getParent();

  BasicBlock::iterator CallIt = find(PBB->begin(), PBB->end(), CI);

  assert(CallIt != PBB->end() && 
	 "CallInst has parent that doesn't contain CallInst?!?");
  return InlineMethod(CallIt);
}

static inline bool ShouldInlineMethod(const CallInst *CI, const Method *M) {
  assert(CI->getParent() && CI->getParent()->getParent() && 
	 "Call not embedded into a method!");

  // Don't inline a recursive call.
  if (CI->getParent()->getParent() == M) return false;

  // Don't inline something too big.  This is a really crappy heuristic
  if (M->size() > 3) return false;

  // Don't inline into something too big. This is a **really** crappy heuristic
  if (CI->getParent()->getParent()->size() > 10) return false;

  // Go ahead and try just about anything else.
  return true;
}


static inline bool DoMethodInlining(BasicBlock *BB) {
  for (BasicBlock::iterator I = BB->begin(); I != BB->end(); ++I) {
    if ((*I)->getOpcode() == Instruction::Call) {
      // Check to see if we should inline this method
      CallInst *CI = (CallInst*)*I;
      Method *M = CI->getCalledMethod();
      if (ShouldInlineMethod(CI, M))
	return InlineMethod(I);
    }
  }
  return false;
}

bool opt::DoMethodInlining(Method *M) {
  bool Changed = false;

  // Loop through now and inline instructions a basic block at a time...
  for (Method::iterator I = M->begin(); I != M->end(); )
    if (DoMethodInlining(*I)) {
      Changed = true;
      // Iterator is now invalidated by new basic blocks inserted
      I = M->begin();
    } else {
      ++I;
    }

  return Changed;
}
