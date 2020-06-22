//===- AddrSpaceNvptx.cpp ---------------===//
//
//
//===----------------------------------------------------------------------===//
#include <map>
#include <queue>
#include <unordered_set>

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/OrderedInstructions.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
//#include "llvm/Analysis/MemoryDependenceAnalysis.h"

using namespace llvm;
using namespace std;

/*
 * TODO
 * Reuse translate result???
 */

#ifdef DEBUG
#define DB(STMT) STMT
#else
#define DB(STMT)
#endif

#define DEBUG_TYPE "omp-at"

#define FAILED 1
#define SUCCESS 0

#define MAX_ATTable_SIZE 20

#define MyAddressSpace 7

// define LLVM_MODULE if emit llvm module
// otherwise, visible by llvm
//#define LLVM_MODULE

// TODO What if it is a pointer data which will also be traced
namespace {
  bool IsDebug = false;
  bool IsNaiveAT = false;
  bool EnableAS = false;
  int InsertedATCount = 0;

  raw_ostream &dp() {
    std::error_code  EC;
    static raw_fd_ostream null_ostream("/dev/null", EC, sys::fs::OF_None);
    assert(!EC);
    if (IsDebug) {
      return errs(); // llvm::errs()
    } else {
      return null_ostream;
    }
  }
  // NOTE sync this data struct
  struct ATTableTy {
    uintptr_t HstPtrBegin;
    uintptr_t HstPtrEnd;
    uintptr_t TgtPtrBegin;
  };
  const int MaxATTableSize = 20;
  const int ATTableEntyNum = sizeof(struct ATTableTy) / sizeof(uintptr_t);

  class OmpTgtAddrTrans : public ModulePass {

    typedef  map<Function*, Function*> FunctionMapTy;

    FunctionMapTy FunctionTransEntry; // Entry Functions after Transform
    FunctionMapTy FunctionTrans; // Functions after Transform

    // Types
    // intptr_t
    IntegerType *IT8;
    IntegerType *IT16;
    IntegerType *IT32;
    IntegerType *ITptr;
    // ATTableTy
    StructType *ATTableType;
    // ATTableTy*
    PointerType *ATTablePtrType;
    // void *
    PointerType *AddrType;

    Function *ATFunc;
    Function *StoreTableFunc;

    // llvm Module
    Module *module;
    LLVMContext *context;

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
    private:
    Argument *getFuncTableArg(Function *F);
    int8_t init(Module &M);
    Function *cloneFuncWithATArg(Function *F);
    void getCalledFunctions(FunctionMapTy &F, Function *T, CallGraph &CG);
    void addEntryFunctionsAsKernel(FunctionMapTy &EntryFuncs);
    CallInst *swapCallInst(CallInst *CI);
    void eraseFunction(FunctionMapTy FunctionTrans, Function* F);
    bool runOnModule(Module &M) override;
    void getAnalysisUsage(AnalysisUsage &AU) const override;
    void naiveAT(Function *);
    vector<Value *> getSourceValues(User *U);
    void traceArgInFunc(Function *, Argument*);
    bool isATFunction(Function *Func);
    void insertATFuncBefore(Instruction *I, Value *Ptr, set<User*> &UserList);

    unsigned getPtrDepth(Type *T);
    bool typeContainPtr(Type *T);
    bool isArgNeedAT(Argument *A);
    bool mayContainHostPtr(Value *V);

    Type *addAddrSpace(Type *T);
    DominatorTree &getDomTree(Function *F);
    void getEntryFuncs(FunctionMapTy &EntryList);
    int16_t doSharedMemOpt();

    // Helper function
    string printFunction(Function *F);
  };
}

int8_t OmpTgtAddrTrans::init(Module &M) {
  if (IsNaiveAT) {
    llvm::errs() << "Naive Address Translation enabled\n";

  }

  //errs() << "OmpTgtAddrTransPass is called\n";
  module = &M;
  context = &M.getContext();

  // check omp_offload.info metadata to skip normal cuda complilation
  if (!M.getNamedMetadata("omp_offload.info")) {
    // TODO
    //return FAILED;
  }
  // Use a metadata to avoid double application
  if (M.getNamedMetadata("omptgtaddrtrans")) {
    return FAILED;
  } else if (!M.getNamedMetadata("nvvm.annotations")) {
    errs() << "Error no nvvm.annotations metadata found!\n";
    return FAILED;
  } else {
    M.getOrInsertNamedMetadata("omptgtaddrtrans");
  }

  DataLayout DL(&M);
  // Init IntegerType
  IT8 = IntegerType::get(*context, 8);
  IT16 = IntegerType::get(*context, 16);
  IT32 = IntegerType::get(*context, 32);
  ITptr = IntegerType::get(*context, DL.getPointerSizeInBits());

  // Create TableTy
  vector<Type*> StructMem;
  for (int i = 0; i < ATTableEntyNum; i++) {
    StructMem.push_back(ITptr);
  }
  ATTableType = StructType::create(*context, StructMem,
      "struct.ATTableTy", false);
  ATTablePtrType = PointerType::getUnqual(ATTableType);

  // Create Address Translation function
  AddrType = PointerType::get(IT8, 0);
  vector<Type*> ParamTypes;
  ParamTypes.push_back(AddrType);
  ParamTypes.push_back(ATTablePtrType);
  FunctionType *ATFuncTy = FunctionType::get(AddrType, ParamTypes, false);
  ATFunc = Function::Create(ATFuncTy, GlobalValue::ExternalLinkage,
      "AddrTrans", M);

  //struct ATTableTy *StoreTableShared(
  //    struct ATTableTy*, struct ATTableTy *sm, int16_t, int32_t)
  ParamTypes.clear();
  ParamTypes.push_back(ATTablePtrType);
  ParamTypes.push_back(ATTablePtrType);
  ParamTypes.push_back(IT8);
  ParamTypes.push_back(IT32);
  FunctionType *STSFuncTy = FunctionType::get(ATTablePtrType, ParamTypes, false);
  StoreTableFunc = Function::Create(STSFuncTy, GlobalValue::ExternalLinkage,
      "StoreTableShared", M);

  // Get analysis
  //MD = &getAnalysis<MemoryDependenceWrapperPass>().getMemDep();
  return SUCCESS;
}

// Print function with Arg
string OmpTgtAddrTrans::printFunction(Function *F) {
  string str;
  raw_string_ostream s(str);
  s << F->getName() << "(";
  for (auto itr = F->arg_begin(); itr != F->arg_end(); itr++) {
    if (itr != F->arg_begin()) {
      s << ", ";
    }
    itr->print(s);
  }
  s << ")";
  return str;
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

/*unsigned OmpTgtAddrTrans::getPtrDepth(Value *V) {
  return getPtrDepth(V->getType());
}*/

bool OmpTgtAddrTrans::isArgNeedAT(Argument *A) {
  return mayContainHostPtr(A);
}

unsigned OmpTgtAddrTrans::getPtrDepth(Type *T) {
  unsigned depth = 0;
  while (PointerType *PT = dyn_cast<PointerType>(T)) {
    depth++;
    T = PT->getElementType();
  }
  return depth;
}
bool OmpTgtAddrTrans::typeContainPtr(Type *T) {
  unsigned depth = getPtrDepth(T);
  if (depth > 0) {
    return true;
  }
  // Deal with struct
  if (StructType *ST = dyn_cast<StructType>(T)) {
    for (auto element : ST->elements()) {
      if (typeContainPtr(element)) {
        return true;
      }
    }
  }
  return false;
}

bool OmpTgtAddrTrans::mayContainHostPtr(Value *V) {
  // This must be a pointer
  Type *T = V->getType();
  if (PointerType *PT = dyn_cast<PointerType>(T)) {
    return typeContainPtr(PT->getElementType());
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
    // FIXME
    Type *T = arg.getType();
    if (EnableAS) {
      if (mayContainHostPtr(&arg)) {
        dp() << "cloning function with new addressspace";
        arg.dump();
        T = addAddrSpace(T);
      }
    }
    ArgsType.push_back(T);
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
  if (EnableAS) {
    for (size_t i = 0; i < F->arg_size();i ++) {
      Argument *arg = &NewFunc->arg_begin()[i];
      if (mayContainHostPtr(arg)) {
        Type *T = FT->getParamType(i);
        arg->mutateType(T);
      }
    }
  }
  dp() << "Clone Function \"" << printFunction(F) << "\"\n\tto \""
    << printFunction(NewFunc) << "\"\n";
  return NewFunc;
}

void OmpTgtAddrTrans::naiveAT(Function *F) {
  // FIXME only once per function function
  static set<Function*> FunctionTransed;
  if (FunctionTransed.find(F) != FunctionTransed.end()) {
    return ;
  }
  FunctionTransed.insert(F);
  bool doInsert = true;
  StringRef name = F->getName();
  dp() << "Naive Translate Function " << name << "\n";

  // Pre kernel functions
  if (name.startswith("__omp_offloading_")) {
    // kernel entry function
    doInsert = false;
  } else if (name.startswith("__omp_outlined__")) {
    doInsert = false;
    StringRef index_str = name.substr(sizeof("__omp_outlined__")-1);
    if (index_str.size() == 0) {
      // __omp_outlined__
    } else {
      int index = strtol(index_str.str().c_str(), nullptr, 10);
      if (index & 1) {
        doInsert = true;
      }
    }
  }
  if (!doInsert) {
    dp() << "\tEntry function don't insert\n";
  }
  // climb every inst
  int MemoryInstCount = 0;
  set<User*> dummyList;
  for (auto &BB : *F) {
    // copy Instruction list
    list<Instruction*> CopiedInsts;
    for (auto &I : BB) {
      CopiedInsts.push_back(&I);
    }
    for (auto &I: CopiedInsts) {
      Instruction *Inst = I;
      if (CallInst *CI = dyn_cast<CallInst>(Inst)) {
        Function *Callee = CI->getCalledFunction();
        StringRef name = Callee->getName();
        if (name.startswith("__omp_outlined__")) {
          // swap call
        } else if (name.startswith("__")) {
          // ignore internel function
          continue;
        } else if (Callee->isIntrinsic()) {
          continue;
        }
        /*(
        } else if (CallInst-> what if no body??
        */
        CallInst *NewCallee = swapCallInst(CI);
        naiveAT(NewCallee->getCalledFunction());
      } else if (!doInsert) {
        continue;
      }
      if (StoreInst *SI = dyn_cast<StoreInst>(Inst)) {
        insertATFuncBefore(SI, SI->getPointerOperand(), dummyList);
      } else if (LoadInst *LI = dyn_cast<LoadInst>(Inst)) {
        insertATFuncBefore(LI, LI->getPointerOperand(), dummyList);
      } else if (MemCpyInst *MCI = dyn_cast<MemCpyInst>(Inst)) {
        insertATFuncBefore(MCI, MCI->getArgOperand(0), dummyList);
        insertATFuncBefore(MCI, MCI->getArgOperand(1), dummyList);
      } else if (AtomicRMWInst *AI = dyn_cast<AtomicRMWInst>(Inst)) {
        insertATFuncBefore(AI, AI->getPointerOperand(), dummyList);
      } else if (AtomicCmpXchgInst *ACXI = dyn_cast<AtomicCmpXchgInst>(Inst)) {
        insertATFuncBefore(ACXI, ACXI->getPointerOperand(), dummyList);
      } else {
        continue;
      }
      MemoryInstCount++;
    }
  }
  dp() << "END Func " << name << " Inserted " << MemoryInstCount << " AT\n";
}

string getValStr(Value *V, bool dump = false) {
  string ret;
  raw_string_ostream out(ret);

  if (dump) {
    V->print(out);
    return ret;
  }

  if (V->hasName()) {
    out << V->getName();
  } else {
    V->print(out);
  }
  return ret;
}

vector<Value *> OmpTgtAddrTrans::getSourceValues(User *U) {
  vector<Value *> ret;
  dp() << "\tgetSourceValues\n";

  if (BitCastInst *BCI = dyn_cast<BitCastInst>(U)) {
    dp() << "\t\tBitCastInst: ";
    ret.push_back(BCI->getOperand(0));
  }
  return ret;
}

std::unordered_set<Argument*> TracedArgs;
// Arg is CPU addr, keep trace it and insert AT function
void OmpTgtAddrTrans::traceArgInFunc(Function *Func, Argument *Arg) {
  // TODO Insert and Delete all together at the end
  // TODO what if passed GPU variable address
  //    -> AT function won't work if of out range
  if (TracedArgs.find(Arg) != TracedArgs.end()) {
    return ;
  }
  TracedArgs.insert(Arg);
  struct PtrInfo {
    Value *V;
    //unsigned PtrDepth;
    unsigned DevicePtrDepth;
    Instruction *UseAfter;
    /*
    PtrInfo(Value *V, unsigned D): V(V), PtrDepth(D), UseAfter(NULL) {};
    PtrInfo(Value *V, unsigned D, Instruction *I): V(V), PtrDepth(D),
      UseAfter(I) {};
      */
    PtrInfo(Value *V, unsigned D): V(V), DevicePtrDepth(D), UseAfter(NULL) {};
    PtrInfo(Value *V, unsigned D, Instruction *I): V(V), DevicePtrDepth(D),
      UseAfter(I) {};
  };
  queue<PtrInfo> Vals; // Value waiting for tracing
  // TODO keep this as per-function list??

  set<User*> &UserList = AllUserList[Func]; // User that has been checked
  // TODO Possible that have to be checked multiple time??

  if (!isATFunction(Func)) {
    errs() << "Tried to trace non-AT function: ";
    Func->getFunctionType()->dump();
    return;
  }
  dp() << "\n>>>>>>>>>>\nTrace arg: "
    << getValStr(Arg, true) << " In Func: " << Func->getName() << "\n";

  Vals.push({Arg, 1});

  // Regen if there is instruction changed
  unique_ptr<OrderedInstructions> OI =
    make_unique<OrderedInstructions>(&getDomTree(Func));

  while (!Vals.empty()) {
    PtrInfo Val = Vals.front();
    // TODO check only Vals is in this function
    Value *V = Val.V;
    unsigned Depth = Val.DevicePtrDepth;
    Vals.pop();
    if (!V) {
      errs() << "Empty Value*: ";
      continue;
    }
    if (Instruction *I = dyn_cast<Instruction>(V)) {
      if (I->getFunction() != Func) {
        errs() << "Warning!!! Tracing value is not in this function\n";
        continue;
      }
    }
    dp() << "\n  Trace depth: " << Depth << " value: "
      << getValStr(V) << "\n";

    if (User *self = dyn_cast<User>(V)) {
      /*
      dp() << ">>>>>>>>>>>>>>>>>>>>>>nserlist: \n";
      for (auto user : UserList) {
        dp() << getValStr(user) << " \n" ;
      }
      dp() << "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n";
      */
      if (UserList.find(self) == UserList.end()) {
        UserList.insert(self);
        // FIXME check self
        dp() << "\tSelf value was not checked\n";
        for (auto srcVal : getSourceValues(self)) {
          dp() << "\tPush source value\n";
          Vals.push({srcVal, Depth, Val.UseAfter});
        }
      }
    }

    // Copy Use to avoid itr broken after insert and swap
    list<Use*> CopiedUses;
    for (auto &U : V->uses()) {
      // uses are reverse order
      CopiedUses.insert(CopiedUses.begin(), &U);
    }

    for (auto _U : CopiedUses) {
      User *U = _U->getUser();
      bool changed = false;
      if (!U) {
        errs() << "Empty User of Val: ";
        V->dump();
        continue;
      }
      // if UseAfter exist
      if (!isa<Instruction>(U)) {
        errs() << "!!Unknown user: func/Arg/Value/User: ";
        errs() << Func->getName() << " ";
        Arg->dump();
        V->dump();
        U->dump();
        continue;
      }
      // Check if User done before
      if (UserList.find(U) != UserList.end()) {
        continue;
      }
      dp() << "\tUser: " << getValStr(U, true) << "\n";
      if (Instruction *I = dyn_cast<Instruction>(U)) {
        if (Val.UseAfter && OI->dfsBefore(I, Val.UseAfter)) {
          I->print(dp());
          dp() << "\tThis user is before cared data stored, Ignore\n";
          // This is a use before it is important
          // TODO  some use should be recheck if UseAfter is different
          continue;
        }
      }
      if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
        // Store has two oprand
        if (SI->getPointerOperand() == dyn_cast<Value>(V)) {
          if (Depth == 0) {
            insertATFuncBefore(SI, V, UserList);
            changed = true;
            // TODO replace further load/ store?
          }
        } else {
          // tracing ptr is stored in new addr
          // Only check use after this Store
          // FIXME what if trace another user or used of this value
          Vals.push({SI->getPointerOperand(), Depth + 1, SI});
        }
      } else if (LoadInst *LI = dyn_cast<LoadInst>(U)) {
        if (Depth == 0) {
          insertATFuncBefore(LI, V, UserList);
          changed = true;
          if (typeContainPtr(U->getType())) {
            dp() << "\tLoadinst valContainPtr ";
            dp() << "\n";
            Vals.push({U, 0});
          }
        } else if (Depth > 0 && typeContainPtr(U->getType())) {
          dp() << "\tLoadinst valContainPtr ";
          dp() << "\n";
          Vals.push({U, Depth - 1});
        }
      } else if (MemCpyInst *MI = dyn_cast<MemCpyInst>(U)) {
        // declare void @llvm.memcpy.p0i8.p0i8.i32 llvm.memcpy.p0i8.p0i8.i64
        //        (i8* <dest>, i8* <src>, i32 <len>, i1 <isvolatile>)
        unsigned ArgIdx = _U->getOperandNo();
        if (ArgIdx == 1) { // this is a load
          Value *dst = MI->getArgOperand(0);
          if (Depth == 0) {
            insertATFuncBefore(MI, V, UserList);
            changed = true;
            Vals.push({dst, 1, MI});
            // get dst come from
          } else if (Depth > 0) {
            // FIXME trace another user or used of this value
            Vals.push({dst, Depth, MI});
          }
          dp() << "\tMemCpyInst valContainPtr\n";
        } else if (ArgIdx == 0) { // this is a store
          // TODO
          if (Depth == 0) {
            insertATFuncBefore(MI, V, UserList);
            changed = true;
          }
          // FIXME
          // Ignore further use -> maybe not, just cause redundant translate
        } else {
          llvm::outs() << "Error, tracing invalid arg in MemCpyInst\n";
          continue;
        }
      } else if (CallInst *CI = dyn_cast<CallInst>(U)) {
        // check if swap callinst
        unsigned ArgIdx = _U->getOperandNo();
        Function *F = CI->getCalledFunction();
        if (!isATFunction(F)) {
          CI = swapCallInst(CI);
          changed = true;
          F = CI->getCalledFunction();
        }
        if (Depth == 0) {
          insertATFuncBefore(CI, V, UserList);
          changed = true;
        }
        traceArgInFunc(F, F->arg_begin() + ArgIdx);
        if (typeContainPtr(CI->getType())) {
          dp() << "\tCallInst valContainPtr ";
          dp() << "Func " << Func->getName() << " arg: " ;
          Arg->print(dp());
          dp() << "\n";
          // TODO
          // Function could return device address Depth would not be 1
          Vals.push({CI, 1});
        }
        OI = make_unique<OrderedInstructions>(&getDomTree(Func));
        continue;
      } else if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(U)) {
        // NOTE GEPI could has multiple value for struct
        if (GEPI->getPointerOperand() == dyn_cast<Value>(V)) {
          if (Depth > 1) {
            llvm::errs() << "[Warning] GEPI with value pointer val > 1\n";
          }
          if (typeContainPtr(U->getType())) {
            dp() << "\tGEPI mayContainPtr ";
            dp() << "\n";
            // FIXME inherit depth??
            Vals.push({U, Depth});
          }
        }
      } else if (BitCastInst *BCI = dyn_cast<BitCastInst>(U)) {
        if (getPtrDepth(BCI->getSrcTy()) == getPtrDepth(BCI->getDestTy())) {
          Vals.push({U, Depth});
        } else {
          dp() << "Ignore different depth BitCastInst for now: ";
          errs() << "Ignore different depth BitCastInst for now: ";
          BCI->dump();
        }
      } else if (AtomicRMWInst *AI = dyn_cast<AtomicRMWInst> (U)) {
        if (AI->getPointerOperand() == dyn_cast<Value>(V)) {
          if (Depth == 0) {
            insertATFuncBefore(AI, V, UserList);
            changed = true;
          }
        }
        // TODO  trace??  in else
      } else if (AtomicCmpXchgInst *ACXI = dyn_cast<AtomicCmpXchgInst>(U)) {
        if (ACXI->getPointerOperand() == dyn_cast<Value>(V)) {
          if (Depth == 0) {
            insertATFuncBefore(ACXI, V, UserList);
            changed = true;
          }
        }
        // TODO  trace??  in else
      } else if (ReturnInst *RI= dyn_cast<ReturnInst>(U)) {
        // Return device addr
        if (Depth == 0) {
          insertATFuncBefore(RI, V, UserList);
          changed = true;
        }
      } else {
        errs() << "!!Unknown Inst: func/Arg/Value/User: ";
        errs() << Func->getName() << " ";
        Arg->dump();
        V->dump();
        U->dump();
        continue;
      }
      UserList.insert(U);
      if (changed) {
        OI = make_unique<OrderedInstructions>(&getDomTree(Func));
      }
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

      iiiiiiiiiiiiiii}
    }
  }
  */
  dp() << "\nExit tracing function: " << Func->getName() << "\n<<<<<<<<<<<<<\n";
}

// Inst has to be in AT function
// TODO Load only once for same addr
//void OmpTgtAddrTrans::insertATFuncBefore(Instruction *Inst, Use *PtrUse, set<User*> &UserList) {
void OmpTgtAddrTrans::insertATFuncBefore(Instruction *Inst, Value *PtrAddr, set<User*> &UserList) {
  InsertedATCount++;
  Argument *ATTableArg = getFuncTableArg(Inst->getFunction());

  dp () << "@Insert translate before Inst: ";
  Inst->print(dp());
  dp() << "\n\tTranslate value: " ;
  PtrAddr->print(dp());
  dp() << "\n";
  // insert bitcast
  CastInst *PreCastI = CastInst::Create(Instruction::BitCast, PtrAddr,
      AddrType, "PreATCast", Inst);

  // insert call
  vector<Value*> Args;
  Args.push_back(PreCastI);
  Args.push_back(ATTableArg);
  CallInst *CI = CallInst::Create(ATFunc->getFunctionType(), ATFunc,
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
  // TODO what does !omp_offload.info mean

  // Prepare metadata
  vector<Metadata *> PreMetaList;
  PreMetaList.push_back(MDString::get(*context, "kernel"));
  ConstantInt *Const = ConstantInt::get(IT32, 1, false);
  PreMetaList.push_back(ConstantAsMetadata::get(Const));

  // Append metadata of kernel entry to nvvm.annotations
  auto NvvmMeta = module->getNamedMetadata("nvvm.annotations");
  for (auto &E : EntryFuncs) {
    Function *F = E.second;
    vector<Metadata*> MetaList = PreMetaList;
    MetaList.insert(MetaList.begin(), ValueAsMetadata::get(F));
    MDTuple *node = MDNode::get(*context, MetaList);
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
  Argument *TableArg = getFuncTableArg(CI->getFunction());

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
  dp() << "Replace call " << CI->getCalledFunction()->getName();
  dp() << " to " << CINew->getCalledFunction()->getName() << "\n";
  CI->replaceAllUsesWith(CINew);
  CI->dropAllReferences();
  CI->eraseFromParent ();
  return CINew;
}

/* Unused funct
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
*/

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
        dp() << "Entry Function: " << printFunction(Entry) << "\n";
      }
    }
  }
}

Type *OmpTgtAddrTrans::addAddrSpace(Type *T) {
  if (PointerType *PT = dyn_cast<PointerType>(T)) {
    Type *Pointee = PT->getElementType();
    return PointerType::get(Pointee, MyAddressSpace);
  }
  return T;
}

int16_t OmpTgtAddrTrans::doSharedMemOpt() {
  // TODO
  // check if we should apply it

  // Create shared array
  // @_ZZ13staticReversePiiE1s =
  //   internal addrspace(3) global [64 x i32] undef, align 4
  //   @SMforATTablein = internal addrspace(3) global [480 x i64], align 4

  // Type : intptr * 3 * 20  = 480
  ArrayType *SMArrayTy = ArrayType::get(ITptr, ATTableEntyNum * MaxATTableSize);
  Constant *SMInit = UndefValue::get(SMArrayTy);
  GlobalVariable *SharedMem = new GlobalVariable(*module, SMArrayTy, false,
      GlobalValue::LinkageTypes::PrivateLinkage , SMInit, "SMforATTable",
      nullptr, GlobalValue::ThreadLocalMode::NotThreadLocal, 3);
  SharedMem->setAlignment(64);

  Function *TidFunc = module->getFunction("llvm.nvvm.read.ptx.sreg.tid.x");
  if (!TidFunc) {
    dp() <<  "llvm.nvvm.read.ptx.sreg.tid.x is not found\n";
    return FAILED;
  }

  Function *BarFunc = module->getFunction("llvm.nvvm.barrier0");
  if (!TidFunc) {
    dp() <<  "llvm.nvvm.barrier0 is not found\n";
    return FAILED;
  }

  // Run from each entry
  for (auto E : FunctionTransEntry) {
    Function *F = E.second;
    // NO -> Find first use of table
    // insert AddrSpaceCast as first
    Instruction *FirstInst = &*F->begin()->begin();
    CastInst *SM2GenericAddr = CastInst::Create(Instruction::AddrSpaceCast,
        SharedMem, ATTablePtrType, "SM2GenericAddr", FirstInst);
    // Get tid reg
    CallInst *Tid = CallInst::Create(TidFunc->getFunctionType(), TidFunc,
        "tid", FirstInst);
    // insert callinst
    //struct ATTableTy *(struct ATTableTy*, struct ATTableTy *sm, int16, int32)
    vector<Value*> StoreTableCallArgs;
    StoreTableCallArgs.push_back(getFuncTableArg(F));
    StoreTableCallArgs.push_back(SM2GenericAddr);
    StoreTableCallArgs.push_back(ConstantInt::get(IT8, MaxATTableSize, false));
    StoreTableCallArgs.push_back(Tid);
    CallInst *NewTableAddr = CallInst::Create(StoreTableFunc->getFunctionType(),
       StoreTableFunc, StoreTableCallArgs, "NewTableAddr", FirstInst);
    // replace use
    vector<Use*> UseToReplace;
    for (auto &U : getFuncTableArg(F)->uses()) {
      // Except the NewTableAddr call
      if (U.getUser() == dyn_cast<User>(NewTableAddr)) {
        continue;
      }
      UseToReplace.push_back(&U);
    }
    for (auto U : UseToReplace) {
      U->set(NewTableAddr);
    }
    // barrier
    CallInst::Create(BarFunc->getFunctionType(), BarFunc,
        "", FirstInst);
  }
  return SUCCESS;
}

Argument *OmpTgtAddrTrans::getFuncTableArg(Function *F) {
  // Check name
  assert(F->getName().endswith("_AT"));
  Argument *ATTableArg = F->arg_end() - 1;
  // Check type
  assert(ATTablePtrType == ATTableArg->getType());
  return ATTableArg;
}

bool OmpTgtAddrTrans::runOnModule(Module &M) {
  IsDebug = (bool) getenv("DP2");
  IsNaiveAT = (bool) getenv("OMP_NAIVE_AT");
  EnableAS = (bool) getenv("LLVM_AS");
  bool changed = false;

  if (init(M)) {
    return changed;
  }

  // Get entry funcs from metadata
  getEntryFuncs(FunctionTransEntry);

  if (FunctionTransEntry.size()) {
    changed = true;
  } else {
    dp() << "No entry function(kernel\n";
    return changed;
  }

  for (auto E : FunctionTransEntry) {
    Function *F = E.first;
    FunctionTransEntry[F] = cloneFuncWithATArg(F);
  }

  // Add Functions to metadata
  addEntryFunctionsAsKernel(FunctionTransEntry);

  if (EnableAS) {
    /*
    //FunctionPass *IASP = createInferAddressSpacesPass();
    for (auto E : FunctionTransEntry) {
    //  TODO
      //Function *F = E.second;
      //dp() << printFunction(F);
    }
    return changed;
    */
  }

  for (auto &E : FunctionTransEntry) {
    Function *F = E.second;
    size_t EntryArgSize = F->arg_size() - 1;
    auto ArgItr = F->arg_begin();
    // All pointer args of entry function is CPU address
    for (size_t i = 0; i < EntryArgSize; i++, ArgItr++) {
      if (isArgNeedAT(ArgItr)) {
        // For naive translation
        if (IsNaiveAT) {
          naiveAT(F);
          break;
        }
        // For performance translation
        traceArgInFunc(F, ArgItr);

      }
    }
  }

  doSharedMemOpt();
  dp() << "Inserted " << InsertedATCount << " address tranlation\n";
  dp() << "OmpTgtAddrTransPass Finished\n";

  return changed;
}

void OmpTgtAddrTrans::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
//  AU.addRequired<MemoryDependenceWrapperPass>();
  AU.addRequired<DominatorTreeWrapperPass>();
//  AU.addRequired<AAResultsWrapperPass>();
//  AU.addRequired<LoopInfoWrapperPass>();
//  AU.getRequiredSet();
}

// TODO NameSpacePropagate
namespace {
class NameSpacePropagate {
// julia reference
//https://github.com/JuliaLang/julia/blob/master/src/llvm-propagate-addrspaces.cpp
  vector<Instruction *> ToDelete;
  vector<pair<Instruction *, Instruction *>> ToInsert;
public:
  bool runOnFunction(Function *F, Value *V) {
    /*
    for (auto it : ToInsert)
      it.first->insertBefore(it.second);
    for (Instruction *I : ToDelete)
        I->eraseFromParent();
    ToInsert.clear();
    ToDelete.clear();
    */
    return true;
  }
};
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
