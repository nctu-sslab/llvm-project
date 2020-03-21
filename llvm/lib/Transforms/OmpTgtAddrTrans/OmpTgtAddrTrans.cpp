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
#include "llvm/Analysis/OrderedInstructions.h"
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

// define LLVM_MODULE if emit llvm module
// otherwise, visible by llvm
//#define LLVM_MODULE


// TODO What if it is a pointer data which will also be traced
namespace {
  class OmpTgtAddrTrans : public ModulePass {

    typedef  map<Function*, Function*> FunctionMapTy;

    FunctionMapTy FunctionTransEntry; // Entry Functions after Transform
    FunctionMapTy FunctionTrans; // Functions after Transform

    // Types
    IntegerType *IT;
    StructType *ATTableType;
    PointerType *ATTablePtrType;
    PointerType *AddrType;

    Function *ATFunction;

    // llvm Module
    Module *module;

    // UserList per Function
    map<Function*,set<User*>> AllUserList;
 //   MemoryDependenceResults *MD;

    unique_ptr<raw_fd_ostream> db_ostream;

    public:
    static char ID; // Pass identification, replacement for typeid
    // Functions
    OmpTgtAddrTrans() : ModulePass(ID) {
#ifndef LLVM_MODULE
      llvm::initializeOmpTgtAddrTransPass(*PassRegistry::getPassRegistry());
#endif
    }
    raw_ostream &dp() {
      std::error_code  EC;
      static raw_fd_ostream null_ostream("/dev/null", EC, sys::fs::OF_None);
      assert(!EC);
      static char IsDP =(bool) getenv("DP2");
      if (IsDP) {
        return errs();
      } else {
        return null_ostream;
      }
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
    void insertATFuncBefore(Instruction *I, Value *Ptr, set<User*> &UserList);
    unsigned getPtrDepth(Value *V);
    unsigned getPtrDepth(Type *T);
    DominatorTree &getDomTree(Function *F);
    void getEntryFuncs(FunctionMapTy &EntryList);
  };
}

int8_t OmpTgtAddrTrans::init(Module &M) {

  //errs() << "OmpTgtAddrTransPass is called\n";
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
  ATTableType = StructType::create(M.getContext(), StructMem, "struct.ATTableTy", false);
  ATTablePtrType = PointerType::getUnqual(ATTableType);

  // Create function
  AddrType = PointerType::get(IntegerType::get(M.getContext(), 8), 0);
  vector<Type*> ParamTypes;
  ParamTypes.push_back(AddrType);
  ParamTypes.push_back(ATTablePtrType);
  FunctionType *FT = FunctionType::get(AddrType, ParamTypes, false);
  ATFunction = Function::Create(FT, GlobalValue::ExternalLinkage, "AddrTrans", M);

  // Get analysis
  //MD = &getAnalysis<MemoryDependenceWrapperPass>().getMemDep();
  return SUCCESS;
}

DominatorTree &OmpTgtAddrTrans::getDomTree(Function *F) {
  return getAnalysis<DominatorTreeWrapperPass>(*F).getDomTree();
}

bool OmpTgtAddrTrans::isATFunction(Function *Func) {
  if (Func->getName().endswith("AT")) {
    return true;
  }
  return false;
}

unsigned OmpTgtAddrTrans::getPtrDepth(Value *V) {
  return getPtrDepth(V->getType());
}

unsigned OmpTgtAddrTrans::getPtrDepth(Type *T) {
  unsigned depth = 0;
  while (PointerType *PT = dyn_cast<PointerType>(T)) {
    depth++;
    T = PT->getElementType();
  }
  return depth;
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
  ArgsType.push_back(ATTablePtrType);
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
  //errs() << "cloneFunc " << F->getName() << " to " << NewFunc->getName() << "\n";

  return NewFunc;
}

// Arg is CPU addr, keep trace it and insert AT function
void OmpTgtAddrTrans::traceArgInFunc(Function *Func, Argument *Arg) {
  // FIXME Possible that pointer to pointer is all GPU addr
  struct PtrInfo {
    Value *V;
    unsigned PtrDepth;
    Instruction *UseAfter;
    PtrInfo(Value *V, unsigned D): V(V), PtrDepth(D), UseAfter(NULL) {};
    PtrInfo(Value *V, unsigned D, Instruction *I): V(V), PtrDepth(D),
      UseAfter(I) {};
  };
  //queue<pair<Value*, unsigned>> Vals;
  queue<PtrInfo> Vals;
  // TODO test per-function List

  unsigned ArgDepth = getPtrDepth(Arg);

  set<User*> &UserList = AllUserList[Func];

  if (!isATFunction(Func)) {
    errs() << "Tried to trace non-AT function: ";
    Func->getFunctionType()->dump();
    return;
  }
  dp() << "traceArgInFunc: " << Func->getName() <<  " PtrDepth: "<< ArgDepth;
  Arg->print(dp());
  dp() << "\n";

  Vals.push({Arg, getPtrDepth(Arg)});

  unique_ptr<OrderedInstructions> OI =
    make_unique<OrderedInstructions>(&getDomTree(Func));

  while (!Vals.empty()) {
    PtrInfo Val = Vals.front();
    Value *V = Val.V;
    // TODO Change name to
    unsigned NestPtr = Val.PtrDepth;
    Vals.pop();
    if (!V) {
      errs() << "Empty Value*: ";
      continue;
    }
    dp()  << "Trace depth: " << NestPtr << " value: ";
    V->print(dp());
    dp() << "\n";

    // Copy Use to avoid itr broken after insert and swap
    list<Use*> CopiedUses;
    for (auto &Use : V->uses()) {
      CopiedUses.push_back(&Use);
    }

    for (auto _U : CopiedUses) {
      User *U = _U->getUser();
      if (!U) {
        errs() << "Empty User of Val: ";
        V->dump();
        continue;
      }
      // if UseAfter exist
      if (Instruction *I = dyn_cast<Instruction>(U)) {
        if (Val.UseAfter && OI->dfsBefore(I, Val.UseAfter)) {
          // This is a use before it is important
          // TODO  some use should be recheck if UseAfter is different
          continue;
        }
      } else {
        errs() << "!!Unknown user: func/Arg/Value/User/UseAfter: ";
        errs() << Func->getName() << " ";
        Arg->dump();
        V->dump();
        U->dump();
        continue;
      }
      // Check if User done before
      if (UserList.find(U) == UserList.end()) {
        U->print(dp());
        dp() << "\n";
      } else {
        continue;
      }
      if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
        if (SI->getPointerOperand() == dyn_cast<Value>(V)) {
          //assert(0 && "StoreInst to CPU address");
          //continue;
          if (NestPtr < ArgDepth) {
            insertATFuncBefore(SI, V, UserList);
            dp() << "!!!!!! Inserted AT function before Store\n";
            // TODO replace further load
          }
        } else {
          // ptr is store into here
          Vals.push({SI->getPointerOperand(), NestPtr + 1, SI});
        }
        // Only check use after this Store
        // FIXME seems wierd
      } else if (LoadInst *LI = dyn_cast<LoadInst>(U)) {
        if (NestPtr < ArgDepth) {
          insertATFuncBefore(LI, V, UserList);
          dp() << "!!!!!! Inserted AT function before Load\n";
        }
        if (NestPtr > 1) {
          Vals.push({U, NestPtr - 1});
        }
      } else if (CallInst *CI = dyn_cast<CallInst>(U)) {
        // check if swap callinst
        unsigned ArgIdx = _U->getOperandNo();
        Function *F = CI->getCalledFunction();
        if (!isATFunction(F)) {
          // Cause redundant use?? FIXME
          CI = swapCallInst(CI);
          F = CI->getCalledFunction();
          // FIXME push val???
        }
        if (NestPtr < ArgDepth) {
          dp() << "!!!!!! Inserted AT function before call\n";
          insertATFuncBefore(CI, V, UserList);
        }
        traceArgInFunc(F, F->arg_begin() + ArgIdx);
        continue;
        // dont push call to Vals
      } else if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(U)) {
        if (GEPI->getPointerOperand() == dyn_cast<Value>(V)) {
          Vals.push({U, NestPtr});
        }
      } else if (BitCastInst *BCI = dyn_cast<BitCastInst>(U)) {
        if (getPtrDepth(BCI->getSrcTy()) == getPtrDepth(BCI->getDestTy())) {
          Vals.push({U, NestPtr});
        } else {
          errs() << "Ignore different depth BitCastInst for now: ";
          BCI->dump();
        }
      } else if (AtomicRMWInst *AI = dyn_cast<AtomicRMWInst> (U)) {
        if (AI->getPointerOperand() == dyn_cast<Value>(V)) {
          if (NestPtr < ArgDepth) {
            insertATFuncBefore(AI, V, UserList);
            dp() << "!!!!!! Inserted AT function before AtomicRMWInst\n";
          }
        }
        // TODO  trace??  in else
      } else if (AtomicCmpXchgInst *ACXI = dyn_cast<AtomicCmpXchgInst>(U)) {
        if (ACXI->getPointerOperand() == dyn_cast<Value>(V)) {
          if (NestPtr < ArgDepth) {
            insertATFuncBefore(ACXI, V, UserList);
            dp() << "!!!!!! Inserted AT function before AtomicCmpXchgInst\n";
          }
        }
        // TODO  trace??  in else
      } else {
        errs() << "!!Unknown Inst: func/Arg/Value/User: ";
        errs() << Func->getName() << " ";
        Arg->dump();
        V->dump();
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

// Inst has to be in AT function
// TODO Load only once for same addr
//void OmpTgtAddrTrans::insertATFuncBefore(Instruction *Inst, Use *PtrUse, set<User*> &UserList) {
void OmpTgtAddrTrans::insertATFuncBefore(Instruction *Inst, Value *PtrAddr, set<User*> &UserList) {
  Value *ATTableArg = Inst->getFunction()->arg_end() - 1;

  /*
  if (LoadInst *LI = dyn_cast<LoadInst>(Inst)) {
    PtrAddr = LI->getPointerOperand();
  } else if (StoreInst *SI = dyn_cast<StoreInst>(Inst)) {
    PtrAddr = SI->getPointerOperand();
  } else if (CallInst *CI = dyn_cast<CastInst>(Inst)) {
    // TODO
    PtrAddr = SI->getPointerOperand();
  } else {
    errs() << "insertATFuncBefore on unsupported inst\n";
    return;
  }*/

  dp () << "insertATFuncBefore PtrAddr: " ;
  PtrAddr->print(dp());
  dp() << "\n";
  // insert bitcast
  CastInst *PreCastI = CastInst::Create(Instruction::BitCast, PtrAddr,
      AddrType, "PreATCast", Inst);

  // insert call
  vector<Value*> Args;
  Args.push_back(PreCastI);
  Args.push_back(ATTableArg);
  CallInst *CI = CallInst::Create(ATFunction->getFunctionType(), ATFunction,
      Args, "TransResult", Inst);
  // insert bitcast
  CastInst *PostCastI = CastInst::Create(Instruction::BitCast, CI,
      PtrAddr->getType(), "PostATCast", Inst);
  //Inst->replaceUsesOfWith (PtrUse->get(), PostCastI);
  Inst->replaceUsesOfWith (PtrAddr, PostCastI);
  UserList.insert(PreCastI);
  UserList.insert(CI);
  UserList.insert(PostCastI);
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
  CINew = CallInst::Create(NewCallee->getFunctionType(), NewCallee, ArgsOfNew,
      Twine::createNull(), CI);
  //CINew->insertBefore(CI);
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

void OmpTgtAddrTrans::getEntryFuncs(FunctionMapTy &EntryList) {
  NamedMDNode *NVVM = module->getNamedMetadata("nvvm.annotations");

  for (const auto &MD : NVVM->operands()) {
    if (MD->getNumOperands() != 3) {
      continue;
    }
    Function *Entry;
    if (!MD->getOperand(0).get()) {
      continue;
    }
    if (auto *VAM = dyn_cast<ValueAsMetadata>(MD->getOperand(0).get())) {
      if (Function *F = dyn_cast<Function>(VAM->getValue())) {
        Entry = F;
        goto SECOND;
      }
    }
    continue;
SECOND:
    if (!MD->getOperand(1).get()) {
      continue;
    }
    if (auto *MDS = dyn_cast<MDString>(MD->getOperand(1).get())) {
      if (MDS->getString().compare("kernel") == 0) {
        goto THIRD;
      }
    }
    continue;
THIRD:
    if (!MD->getOperand(2).get()) {
      continue;
    }
    if (auto *CAM = dyn_cast<ConstantAsMetadata>(MD->getOperand(2).get())) {
      if (CAM->getValue()->isOneValue()) {
        EntryList[Entry] = NULL;
        dp() << "Get entry Func: " << Entry->getName() << "\n";
      }
    }
  }
}

bool OmpTgtAddrTrans::runOnModule(Module &M) {
  bool changed = false;

  if (init(M)) {
    return changed;
  }

  // Get entry funcs from metadata
  getEntryFuncs(FunctionTransEntry);

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
      if (getPtrDepth(ArgItr) > 1) {
        traceArgInFunc(F, ArgItr);
      }
    }
  }

  return changed;
}

void OmpTgtAddrTrans::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
//  AU.addRequired<MemoryDependenceWrapperPass>();
  AU.addRequired<DominatorTreeWrapperPass>();
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

// TODO
//INITIALIZE_PASS_BEGIN(OmpTgtAddrTrans, "OmpTgtAddrTransPass", "Description TODO", false, false)
//INITIALIZE_PASS_DEPENDENCY(CallGraphWrapperPass)
//INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
//INITIALIZE_PASS_END(OmpTgtAddrTrans, "OmpTgtAddrTransPass", "Description TODO", false, false)
//INITIALIZE_PASS_END(OmpTgtAddrTrans, "OmpTgtAddrTransPass", "Scalar Replacement Of Aggregates", false, false)
