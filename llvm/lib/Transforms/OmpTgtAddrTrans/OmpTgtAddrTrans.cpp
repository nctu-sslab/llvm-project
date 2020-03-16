//===- AddrSpaceNvptx.cpp ---------------===//
//
//
//===----------------------------------------------------------------------===//
#include <map>

#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Transforms/IPO.h"

using namespace llvm;
using namespace std;

#ifdef DEBUG
#define DB(STMT) STMT
#else
#define DB(STMT)
#endif

#define DEBUG_TYPE "omp-at"

#define LLVM_MODULE

namespace {
  class OmpTgtAddrTrans : public ModulePass {

    typedef  map<Function*, Function*> FunctionMapTy;

    // Types
    IntegerType *IT;
    StructType *ST;
    PointerType *PT;

    // llvm Module
    Module *module;

    public:
    static char ID; // Pass identification, replacement for typeid
    // Functions
    OmpTgtAddrTrans() : ModulePass(ID) {
#ifndef LLVM_MODULE
      llvm::initializeOmpTgtAddrTransPass(*PassRegistry::getPassRegistry());
#endif
    }
bool runOnFunction(Function &F);
Function *cloneFuncWithATArg(Function *F);
void getCalledFunctions(FunctionMapTy &F, Function *T, CallGraph &CG);
void addEntryFunctionsAsKernel(FunctionMapTy &EntryFuncs);
void swapCallInst(FunctionMapTy &Functions, Function *F);
void eraseFunction(FunctionMapTy FunctionTrans, Function* F);
bool runOnModule(Module &M) override;
void getAnalysisUsage(AnalysisUsage &AU) const override;
  };
}

bool OmpTgtAddrTrans::runOnFunction(Function &F) {
  return true;
}

// Param
// TODO kernel need to have some attribute nvvm annotation
Function *OmpTgtAddrTrans::cloneFuncWithATArg(Function *F) {
  // TODO
  vector<Type*> ArgsType;
  // Assert if target function is va_arg
  assert(!F->getFunctionType()->isVarArg() && "AddrTrans should not be VA");

  ValueToValueMapTy VMap;
  for (auto &arg: F->args()) {
    //arg.dump();
    ArgsType.push_back(arg.getType());
  }
  ArgsType.push_back(PT);
  FunctionType *FT = FunctionType::get(F->getReturnType(), ArgsType, false);
  Twine FuncName(F->getName());

  // insert new Function
  Function *NewFunc = Function::Create(FT, F->getLinkage(),
      F->getAddressSpace(), FuncName.concat("_AT"), F->getParent());

  // ValueMap for args
  VMap[F] = NewFunc;
  Function::arg_iterator NewArgs = NewFunc->arg_begin();
  for (auto &arg: F->args()) {
    if (VMap.count(&arg) == 0) {
      NewArgs->setName(arg.getName());
      VMap[&arg] = &*NewArgs++;
    }
  }
  // Add name to new args
  NewArgs->setName("__ATtable");
  //NewArgs->setName("__table_size");
  SmallVector<ReturnInst *, 8> Returns; // Ignore returns cloned.

  // Clone body
  CloneFunctionInto(NewFunc, F, VMap, /*ModuleLevelChanges=*/true, Returns);
  errs() << "cloneFunc " << F->getName() << " to " << NewFunc->getName() << "\n";

  return NewFunc;
}

// Recursive
// Store func called by Target function
// TODO Only collet functions use the pointers
void OmpTgtAddrTrans::getCalledFunctions(FunctionMapTy &Functions,
    Function *Target, CallGraph &CG) {

  CallGraphNode *CGN = CG[Target];

  if (!CGN) {
    return;
  }

  // get CallGraph
  for (auto &CR : *CGN) {
    Function *F = CR.second->getFunction();
    if (!F) {
      continue;
    }
    getCalledFunctions(Functions, F, CG);
    Functions[F] = NULL;
  }
}

void OmpTgtAddrTrans::addEntryFunctionsAsKernel(FunctionMapTy &EntryFuncs) {
  // TODO
  // Does these two named metadata important??
  // !omp_offload.info
  // !nvvm.annotations

  // Prepare metadata
  vector<Metadata *> PreMetaList;
  PreMetaList.push_back(MDString::get(module->getContext(), "kernel"));
  IntegerType *IT32 = IntegerType::get(module->getContext(), 32);
  ConstantInt *Const = ConstantInt::get(IT32, 1, false);
  PreMetaList.push_back(ConstantAsMetadata::get(Const));

  // Append metadata of kernel entry to nvvm.annotations
  auto NvvmMeta = module->getNamedMetadata("nvvm.annotations");
  for (auto &E : EntryFuncs) {
    Function *F = E.second;
    vector<Metadata*> MetaList = PreMetaList;
    MetaList.insert(MetaList.begin(), ValueAsMetadata::get(F));
    MDTuple *node = MDNode::get(module->getContext(), MetaList);
    NvvmMeta->addOperand(node);
  }
}

void OmpTgtAddrTrans::swapCallInst(FunctionMapTy &Functions, Function *F) {
  // Get last two args
  auto arg_it = F->arg_end();
  Value *table_arg = --arg_it;

  vector<Instruction*> DeleteInst;

  // Find call inst
  // Iterate BB
  for (auto &BB : *F) {
    for (auto &Inst : BB) {
      if (CallInst *CI = dyn_cast<CallInst>(&Inst)) {
        Function *callee = CI->getCalledFunction();
        auto func = Functions.find(callee);
        if (func == Functions.end()) {
          errs() << "Untracked function is called\n";
          continue;
        }
        Function *NewFunc = func->second;
        // Get old arg
        vector<Value*> args;
        for (auto &operand : CI->args()) {
          args.push_back(operand);
        }
        args.push_back(table_arg);
        /*for (auto it: args) {
          it->dump();
        }*/
        // Name is not important
        CallInst *CINew = CallInst::Create (NewFunc->getFunctionType(), NewFunc,
           args, NewFunc->getName(), CI);
        CI->replaceAllUsesWith(CINew);
        CI->dropAllReferences();
        DeleteInst.push_back(CI);
      }
    }
  }
  // Erase replaced function
  for (auto Inst : DeleteInst) {
    Inst->eraseFromParent ();
  }
}

void OmpTgtAddrTrans::eraseFunction(FunctionMapTy FunctionTrans, Function* F) {
  // Erase function
  F->dropAllReferences();
  if (!F->use_empty()) {
    // Remove all use first
    for (auto &use : F->uses()) {
      if (Instruction *Inst = dyn_cast<Instruction>(use.getUser())) {
        Function *UserFunc = Inst->getFunction();
        if (FunctionTrans.find(UserFunc) != FunctionTrans.end()) {
          Inst->dropAllReferences();
        } else {
          assert(0 && "User of deleting function is not in old function");
        }
      } else {
        assert(0 && "User of deleting function is not a Instruction");
      }
    }
  }
  F->eraseFromParent();
}

bool OmpTgtAddrTrans::runOnModule(Module &M) {
  bool changed = false;
  errs() << "OmpTgtAddrTransPass is called\n";
  module = &M;

  // Use a metadata to avoid double application
  if (M.getNamedMetadata("omptgtaddrtrans")) {
    return changed;
  } else if (!M.getNamedMetadata("nvvm.annotations")) {
    errs() << "Error no nvvm.annotations metadata found!\n";
    return changed;
  } else {
    M.getOrInsertNamedMetadata("omptgtaddrtrans");
  }


  // TODO Use init function
  // Create TableTy
  DataLayout DL(&M);
  vector<Type*> StructMem;
  IT = IntegerType::get(M.getContext(), DL.getPointerSizeInBits());
  for (int i = 0; i < 4; i++) {
    StructMem.push_back(IT);
  }
  ST = StructType::create(M.getContext(), StructMem, "ATTableTy", false);
  // Change name to struct.xxx
  //ST = StructType::create(M.getContext(), StructMem, "ATTableTy", false);

  PT = PointerType::getUnqual(ST);

  FunctionMapTy FunctionTransEntry; // Entry Functions after Transform
  FunctionMapTy FunctionTrans; // Functions after Transform

  // Get entry funcs
  string EntryFuncName("__omp_offloading_");
  for (auto &F : M.functions()) {
    if (F.getName().contains(EntryFuncName)) {
      FunctionTransEntry[&F] = NULL;
      errs() << "Capture entry function: " << F.getName() << "\n";
      continue;
    }
  }

  if (FunctionTransEntry.size()) {
    changed = true;
  } else {
    return changed;
  }

  for (auto E : FunctionTransEntry) {
    Function *F = E.first;
    FunctionTransEntry[F] = cloneFuncWithATArg(F);
  }

  // Add Functions to metadata
  addEntryFunctionsAsKernel(FunctionTransEntry);


  /*
  CallGraph CG(M);
  for (auto E : FunctionTrans) {
    Function *F = E.first;
    // TODO what if cross module call
    getCalledFunctions(FunctionTrans, F, CG);
  }


  for (auto E : FunctionTrans) {
    Function *F = E.first;
    FunctionTrans[F] = cloneFuncWithATArg(F);
  }

  if (FunctionTrans.size()) {
    changed = true;
  }

  // Change function call
  for (auto E : FunctionTrans) {
    Function *F = E.second;
    swapCallInst(FunctionTrans, F);
  }*/

  // Remove old functions
  /*for (auto E : FunctionTrans) {
    Function *F = E.first;
    eraseFunction(FunctionTrans, F);
  }*/
  return changed;
}

void OmpTgtAddrTrans::getAnalysisUsage(AnalysisUsage &AU) const {
  //AU.getRequiredSet();
  //AU.setPreservesCFG();
  //AU.setPreservesAll();
  //AU.addRequiredID(LoopInfoWrapperPass::ID);
  AU.addRequired<LoopInfoWrapperPass>();
  AU.addRequired<DominatorTreeWrapperPass>();
  //AU.getRequiredSet();
}

char OmpTgtAddrTrans::ID = 0;

#ifdef LLVM_MODULE
static RegisterPass<OmpTgtAddrTrans>
Y("OmpTgtAddrTrans", "OmpTgtAddrTransPass Description");

#else
INITIALIZE_PASS(OmpTgtAddrTrans, "OmpTgtAddrTransPass", "Description TODO", false, false)

ModulePass *llvm::createOmpTgtAddrTransPass() {
  return new OmpTgtAddrTrans();
}
#endif

//INITIALIZE_PASS_BEGIN(OmpTgtAddrTrans, "OmpTgtAddrTransPass", "Description TODO", false, false)
//INITIALIZE_PASS_DEPENDENCY(CallGraphWrapperPass)
//INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
//INITIALIZE_PASS_END(OmpTgtAddrTrans, "OmpTgtAddrTransPass", "Description TODO", false, false)
//INITIALIZE_PASS_END(OmpTgtAddrTrans, "OmpTgtAddrTransPass", "Scalar Replacement Of Aggregates", false, false)
