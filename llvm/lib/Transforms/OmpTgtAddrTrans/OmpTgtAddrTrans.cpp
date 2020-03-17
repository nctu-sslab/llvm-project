//===- AddrSpaceNvptx.cpp ---------------===//
//
//
//===----------------------------------------------------------------------===//
#include <map>
#include <queue>

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
//#include "llvm/Analysis/MemoryDependenceAnalysis.h"

using namespace llvm;
using namespace std;

#ifdef DEBUG
#define DB(STMT) STMT
#else
#define DB(STMT)
#endif

#define DEBUG_TYPE "omp-at"

#define FAILED 1
#define SUCCESS 0

#define LLVM_MODULE

namespace {
  class OmpTgtAddrTrans : public ModulePass {

    typedef  map<Function*, Function*> FunctionMapTy;

    FunctionMapTy FunctionTransEntry; // Entry Functions after Transform
    FunctionMapTy FunctionTrans; // Functions after Transform

    // Types
    IntegerType *IT;
    StructType *ST;
    PointerType *PT;

    // llvm Module
    Module *module;

 //   MemoryDependenceResults *MD;

    public:
    static char ID; // Pass identification, replacement for typeid
    // Functions
    OmpTgtAddrTrans() : ModulePass(ID) {
#ifndef LLVM_MODULE
      llvm::initializeOmpTgtAddrTransPass(*PassRegistry::getPassRegistry());
#endif
    }
    int8_t init(Module &M);
    Function *cloneFuncWithATArg(Function *F);
    //void getCalledFunctions(FunctionMapTy &F, Function *T, CallGraph &CG);
    void addEntryFunctionsAsKernel(FunctionMapTy &EntryFuncs);
    CallInst *swapCallInst(CallInst *CI);
    void eraseFunction(FunctionMapTy FunctionTrans, Function* F);
    bool runOnModule(Module &M) override;
    void getAnalysisUsage(AnalysisUsage &AU) const override;
    void traceArgInFunc(Function *, Argument*);
    bool isATFunction(Function *Func);
  };
}

int8_t OmpTgtAddrTrans::init(Module &M) {

  errs() << "OmpTgtAddrTransPass is called\n";
  module = &M;

  // Use a metadata to avoid double application
  if (M.getNamedMetadata("omptgtaddrtrans")) {
    return FAILED;
  } else if (!M.getNamedMetadata("nvvm.annotations")) {
    errs() << "Error no nvvm.annotations metadata found!\n";
    return FAILED;
  } else {
    M.getOrInsertNamedMetadata("omptgtaddrtrans");
  }
  // Create TableTy
  DataLayout DL(&M);
  vector<Type*> StructMem;
  IT = IntegerType::get(M.getContext(), DL.getPointerSizeInBits());
  for (int i = 0; i < 4; i++) {
    StructMem.push_back(IT);
  }
  ST = StructType::create(M.getContext(), StructMem, "struct.ATTableTy", false);
  PT = PointerType::getUnqual(ST);

  // Get analysis
  //MD = &getAnalysis<MemoryDependenceWrapperPass>().getMemDep();
  return SUCCESS;
}

bool OmpTgtAddrTrans::isATFunction(Function *Func) {
  if (Func->getName().endswith("AT")) {
    return true;
  }
  return false;
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

// Arg is CPU addr, keep trace it and insert AT function
void OmpTgtAddrTrans::traceArgInFunc(Function *Func, Argument *Arg) {
  queue<pair<Value*, int>> Vals;
  set<User*> UserList;

  if (!isATFunction(Func)) {
    errs() << "Tried to trace non-AT function: ";
    Func->getFunctionType()->dump();
    return;
  }
  // TODO keep trace which Instruction has been traced

  // Check if this has been cloned

  Vals.push({Arg,0});
  while (!Vals.empty()) {
    Value *V = Vals.front().first;
    int NestPtr = Vals.front().second;
    Vals.pop();
    for (auto &Use : V->uses()) {
      User *U = Use.getUser();
      // Check if User done before
      if (UserList.find(U) == UserList.end()) {
        //U->dump();
      } else {
        continue;
      }
      if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
        if (SI->getPointerOperand() == dyn_cast<Value>(V)) {
          assert(0 && "StoreInst to CPU address");
          continue;
        }
        Vals.push({SI->getPointerOperand(), NestPtr + 1});
      } else if (isa<LoadInst>(U)) {
        if (NestPtr == 0) {
          errs() << "!!!!!!!!!! Insert AT function here!!!!!!\n";
        } else {
          Vals.push({U, NestPtr - 1});
        }
      } else if (CallInst *CI = dyn_cast<CallInst>(U)) {
        // check if swap callinst
        unsigned ArgIdx = Use.getOperandNo();
        Function *F = CI->getCalledFunction();
        if (!isATFunction(F)) {
          // Cause redundant use?? TODO
          CI = swapCallInst(CI);
          F = CI->getCalledFunction();
        }
        traceArgInFunc(F, F->arg_begin() + ArgIdx);
      } else if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(U)) {
        if (GEPI->getPointerOperand() == dyn_cast<Value>(V)) {
          Vals.push({U, NestPtr});
        }
      } else if (Instruction *Inst = dyn_cast<Instruction>(U)) {
        errs() << "Unknown inst\n";
        Inst->dump();
        continue;
      } else {
        errs() << "Unknown use\n";
        U->dump();
        continue;
      }
      UserList.insert(U);
    }
  }

  /*
  MD = &getAnalysis<MemoryDependenceWrapperPass>(*Func).getMemDep();
  for (auto &BB: *Func) {
    for (auto &I : BB) {
      if (StoreInst *MI = dyn_cast<StoreInst>(&I)) {
        Instruction *resultI = MD->getDependency(&I).getInst();
        errs() << "\nDep: << ";
        I.dump();
        if (resultI) {
          resultI->dump();
        } else {
          errs() << "abnormal\n";
        }

      }
    }
  }
  */
}

// Recursive
// Store func called by Target function
/*void OmpTgtAddrTrans::getCalledFunctions(FunctionMapTy &Functions,
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
}*/

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

// swap CallInst to call AT function
CallInst *OmpTgtAddrTrans::swapCallInst(CallInst *CI) {
  // Check if callee is transformed
  Function *Callee = CI->getCalledFunction();
  if (isATFunction(Callee)) {
    return CI;
  } else if (FunctionTrans.find(Callee) == FunctionTrans.end()) {
    FunctionTrans[Callee] = cloneFuncWithATArg(Callee);
  }
  Function *NewCallee = FunctionTrans[Callee];

  // Get table Arg of parent function
  Function *ParentFunc = CI->getFunction();
  Argument *TableArg = (ParentFunc->arg_end() - 1);

  // Get old arg
  vector<Value*> ArgsOfNew;
  for (auto &operand : CI->args()) {
    ArgsOfNew.push_back(operand);
  }
  ArgsOfNew.push_back(TableArg);

  // Create new inst
  CallInst *CINew = NULL;
  CINew = CallInst::Create(NewCallee->getFunctionType(), NewCallee, ArgsOfNew);
  CINew->insertBefore(CI);
  CI->replaceAllUsesWith(CINew);
  CI->dropAllReferences();
  CI->eraseFromParent ();
  return CINew;
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

  if (init(M)) {
    return changed;
  }

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

  for (auto &E : FunctionTransEntry) {
    Function *F = E.second;
    size_t EntryArgSize = F->arg_size() - 1;
    auto ArgItr = F->arg_begin();
    // All pointer args of entry function is CPU address
    for (size_t i = 0; i < EntryArgSize; i++, ArgItr++) {
      if (auto PT = dyn_cast<PointerType> (ArgItr->getType())) {
        if (PT->getElementType()->isPointerTy()) {
          traceArgInFunc(F, ArgItr);
        }
      }
    }
  }

  return changed;
}

void OmpTgtAddrTrans::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
//  AU.addRequired<MemoryDependenceWrapperPass>();
//  AU.addRequired<DominatorTreeWrapperPass>();
//  AU.addRequired<AAResultsWrapperPass>();
//  AU.addRequired<LoopInfoWrapperPass>();
//  AU.addRequired<DominatorTreeWrapperPass>();
//  AU.getRequiredSet();
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
