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
#include "llvm/Analysis/AliasAnalysis.h"
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
  //bool IsNaiveAT = false;
  //bool EnableAS = false;
  bool DisableFakeLoad = false;
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
  // NOTE sync this data struct with OMP libtarget runtime
  struct ATTableTy {
    uintptr_t HstPtrBegin;
    uintptr_t HstPtrEnd;
    uintptr_t TgtPtrBegin;
  };
  enum ATVer {
    OMP_AT_TABLE,
    OMP_AT_MASK,
    OMP_AT_OFFSET
  };
  typedef map<ATVer, Function*> ATFunctionSet;
  const int MaxATTableSize = 20;
  const int ATTableEntyNum = sizeof(struct ATTableTy) / sizeof(uintptr_t);

  struct InsertionAT {
    Instruction* I;
    Value *Addr;
  };
  typedef map<Function*, vector<InsertionAT>> HostAddrInsts;

  struct InfoPerFunc {
    set<User*> TracedUserList;
    map<Instruction*, Instruction*> TranslatedResult;
    set<LoadInst*> LoadInstListReplacing;
  };

  class OmpTgtAddrTrans : public ModulePass {

    typedef  map<Function*, ATFunctionSet> FunctionMapTy;

    FunctionMapTy FunctionTransEntry; // Entry Functions after Transform
    FunctionMapTy FunctionTrans; // Other functions Transform mapping

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
    PointerType *PTptr; // uintptr_t *

    // mask value     0x00007f0000000000L
    ConstantInt *Mask0x7f;
    // offset value   0x0000200000000000L
    ConstantInt *Offset0x20;


    Function *ATFuncTable_tab;    // void *(void *, struct ATTableTy*)

    Function *ATFuncTable;        // void *(void *)
    Function *StoreTableFunc;     // pre-store table

    // Index-based
    Function *ATFuncOffset;       // void *(void *, intptr_t *)
    Function *StoreOffsetFunc;
    // Index-based-opt
    Function *ATFuncOffset2;      // void *(void *, intptr_t *) using constant mem

    // Mask-based
    Function *ATFuncMask;         // void *(void *)

    // llvm Module
    Module *module;
    LLVMContext *context;

    // AT mode
    ATVer CurATMode;

    // Metadatas
    MDNode *ATMD;

    // Constant memory space
    //GlobalVariable *CuConstMem;

    // UserList per Function
    map<Function*, InfoPerFunc> AllUserList;
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
    virtual StringRef getPassName() const override {return "OmpTgtAddrTrans";};
    private:
    Argument *getATArg(Function *F);
    int8_t init(Module &M);
    ATFunctionSet cloneFuncWithATArg(Function *F);
    Function *cloneFuncWithATArgByVer(Function *F, enum ATVer);
    void getCalledFunctions(FunctionMapTy &F, Function *T, CallGraph &CG);
    void addEntryFunctionsAsKernel(FunctionMapTy &EntryFuncs);
    CallInst *swapCallInst(CallInst *CI, ATVer Opt);
    void eraseFunction(FunctionMapTy FunctionTrans, Function* F);
    bool runOnModule(Module &M) override;
    void getAnalysisUsage(AnalysisUsage &AU) const override;
//    void naiveAT(Function *);
    vector<Value *> getSourceValues(User *U);
    void traceArgInFunc(Function *, Argument*, ATVer Opt);
    Instruction *insertATFuncBefore2(Instruction *I, Value *Ptr);
    Instruction *insertATFuncAfter(Value *V, set<User*> &UserList, bool DoReplaceAll = false);
    //Instruction *insertMaskingAfter(Value *V, set<User*> &UserList, bool DoReplaceAll = false);

    unsigned getPtrDepth(Type *T);
    bool typeContainPtr(Type *T);
    bool isArgNeedAT(Argument *A);
    bool mayContainHostPtr(Value *V);

    Type *addAddrSpace(Type *T);
    DominatorTree &getDomTree(Function *F);
    void getEntryFuncs(FunctionMapTy &EntryList);
    int16_t doSharedMemOpt();
    Function *genFakeLoadFunc(LoadInst *LI);
    Function *replaceLoad(LoadInst *LI);

    void getHostAddrInsts(Function *F, Argument*, HostAddrInsts &);
    void epilogue();

    // Helper function
    string printFunction(Function *F);
    string strFunc(Function *F);
    string strVal(Value *V);
  };

  class RestoreLoad : public ModulePass {
  public:
    static char ID;
    RestoreLoad(): ModulePass(ID) {
#ifndef LLVM_MODULE
      llvm::initializeRestoreLoadPass(*PassRegistry::getPassRegistry());
#endif
    }
    bool runOnModule(Module &M) override;
  private:
    bool restoreLoad(CallInst *CI);

  };

  class ConcurrentAT : public ModulePass {
  public:
    static char ID;
    Intrinsic::ID NvvmTidIID, NvvmCtaidIID;
    bool HasError;
    ConcurrentAT(): ModulePass(ID) {
#ifndef LLVM_MODULE
      //llvm::initializeConcurrentATPass(*PassRegistry::getPassRegistry());
#endif
    }
    bool runOnModule(Module &M) override;
  private:
    int doConcurrentAT(Function *);
    bool init(Module &M);

  };
}

int8_t OmpTgtAddrTrans::init(Module &M) {
  /*
  if (IsNaiveAT) {
    llvm::errs() << "Naive Address Translation enabled\n";
  }*/

  //errs() << "OmpTgtAddrTrans is called\n";
  module = &M;
  context = &M.getContext();

  // check omp_offload.info metadata to skip normal cuda complilation
  if (!M.getNamedMetadata("omp_offload.info")) {
    // TODO
    return FAILED;
  }
  // Use a metadata to avoid double application
  string PassMetadata("omp_offload.OmpTgtAddrTrans");
  if (M.getNamedMetadata(PassMetadata)) {
    errs() << "Metadata " << PassMetadata << " already exist!\n";
    return FAILED;
  } else if (!M.getNamedMetadata("nvvm.annotations")) {
    errs() << "Error no nvvm.annotations metadata found!\n";
    return FAILED;
  } else {
    auto *MD =M.getOrInsertNamedMetadata(PassMetadata);
    // Insert time and date
    //#include <time.h>
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    string timestamp;
    timestamp += to_string(tm.tm_mon + 1) + to_string(tm.tm_mday) +
      to_string(tm.tm_hour) + to_string(tm.tm_min);
    MD->addOperand(MDNode::get(*context, MDString::get(*context, timestamp)));
  }

  // Set AT function metadata
  ATMD = MDNode::get(*context, MDString::get(*context, "ompat"));

  DataLayout DL(&M);
  // Init IntegerType
  IT8 = IntegerType::get(*context, 8);
  IT16 = IntegerType::get(*context, 16);
  IT32 = IntegerType::get(*context, 32);
  ITptr = IntegerType::get(*context, DL.getPointerSizeInBits());
  PTptr = PointerType::get(ITptr, 0);
  AddrType = PointerType::get(IT8, 0);

  // Create TableTy
  vector<Type*> StructMem;
  for (int i = 0; i < ATTableEntyNum; i++) {
    StructMem.push_back(ITptr);
  }
  ATTableType = StructType::create(*context, StructMem,
      "struct.ATTableTy", false);
  ATTablePtrType = PointerType::getUnqual(ATTableType);

  vector<Type*> ParamTypes;
  // Create Address Translation function (table ver)
  ParamTypes.clear();
  ParamTypes.push_back(AddrType);
  FunctionType *ATFuncTableTy = FunctionType::get(
      AddrType, ParamTypes, false);
  ATFuncTable = Function::Create(
      ATFuncTableTy, GlobalValue::ExternalLinkage, "AddrTransTable", M);
  // Set pure function attribute to open optimization
  ATFuncTable->addFnAttr(Attribute::ReadNone);

  // Create Address Translation function (table ver) with table arg
  ParamTypes.clear();
  ParamTypes.push_back(AddrType);
  ParamTypes.push_back(ATTablePtrType);
  FunctionType *ATFuncTable_tabTy = FunctionType::get(
      AddrType, ParamTypes, false);
  ATFuncTable_tab = Function::Create(
      ATFuncTable_tabTy, GlobalValue::ExternalLinkage, "AddrTransTable2", M);
  ATFuncTable_tab->addFnAttr(Attribute::ReadNone);

  // Create AT func (mask ver)
  ParamTypes.clear();
  ParamTypes.push_back(AddrType);
  ATFuncMask = Function::Create(
      ATFuncTableTy, GlobalValue::ExternalLinkage, "AddrTransMask", M);
  ATFuncMask->addFnAttr(Attribute::ReadNone);

  // Create offset(index) version AT func
  ParamTypes.clear();
  ParamTypes.push_back(AddrType);
  ParamTypes.push_back(PTptr);
  FunctionType *ATFuncOffsetTy = FunctionType::get(
      AddrType, ParamTypes, false);
  ATFuncOffset = Function::Create(
      ATFuncOffsetTy, GlobalValue::ExternalLinkage, "AddrTransOffset", M);
  ATFuncOffset->addFnAttr(Attribute::ReadNone);

  ATFuncOffset2 = Function::Create(
      ATFuncOffsetTy, GlobalValue::ExternalLinkage, "AddrTransOffset2", M);
  ATFuncOffset2->addFnAttr(Attribute::ReadNone);

  //struct ATTableTy *StoreTableShared(
  //    struct ATTableTy*, struct ATTableTy *sm, int16_t, int32_t)
  ParamTypes.clear();
  ParamTypes.push_back(ATTablePtrType);
  /*
  ParamTypes.push_back(ATTablePtrType);
  ParamTypes.push_back(IT8);
  ParamTypes.push_back(IT32);
  */
  FunctionType *STSFuncTy = FunctionType::get(ATTablePtrType, ParamTypes, false);
  StoreTableFunc = Function::Create(STSFuncTy, GlobalValue::ExternalLinkage,
      "StoreTableShared", M);
  //FunctionType *SMFTy = FunctionType::get(Type::getVoidTy(*context),
  //    ITptr, false);

  // Mask0x7f init
  Mask0x7f = ConstantInt::get(ITptr, 0x00007f0000000000L);
  //Offset0x20
  //Offset0x20 = ConstantInt::get(ITptr, 0x0000200000000000L);

  // Get analysis
  //MD = &getAnalysis<MemoryDependenceWrapperPass>().getMemDep();

  /*
#define DEFAULT_CM_ENTRY 16
  ArrayType *ConstMemArrayTy = ArrayType::get(ITptr, DEFAULT_CM_ENTRY);
  Constant *UVInit = UndefValue::get(ConstMemArrayTy);
  CuConstMem = new GlobalVariable(*module, ConstMemArrayTy, false,
      GlobalValue::LinkageTypes::PrivateLinkage , UVInit, "ConstMem",
      nullptr, GlobalValue::ThreadLocalMode::NotThreadLocal, 4);
  CuConstMem->setAlignment(64);
  CuConstMem->dump();
  */
  return SUCCESS;
}

void OmpTgtAddrTrans::epilogue() {
  // TODO insert the AT functions at the end??
  dp() << "Epilogue\n";
  /*
   * Cannot reuse result by my self
  for (auto &F : module) {
    for (auto &BB : F) {
      for (auto &I : BB) {
        if (I.getMetaData("ompat")) {
          // Insert AT here
          I.dump();
        }
      }
    }
  }
  */
  // replace the load insts to readnone function for further opt
  if (DisableFakeLoad) {
    return;
  }
  for (auto &e : AllUserList) {
    auto &LoadInstListReplacing = e.second.LoadInstListReplacing;
    for (auto LI : LoadInstListReplacing) {
      replaceLoad(LI);
    }
  }
}

static bool isRelated(Value *V, set<Value*> &RelatedValues, int limit = 0) {
  if (!isa<Instruction>(V)) {
    return false;
  }
  if (limit > 50) {
    errs() << "[ConcurrentAT] isRelated recursive limit reached\n";
    return true;
  }
  User *U = dyn_cast<User>(V);
  for (auto use : U->operand_values()) {
    if (RelatedValues.find(use) != RelatedValues.end()) {
      return true;
    }
    if (isRelated(use, RelatedValues, limit+1)) {
      RelatedValues.insert(V);
      return true;
    }
  }
  return false;
}

int ConcurrentAT::doConcurrentAT(Function *F) {
  errs() << "Run doConcurrentAT on " << F->getName() << "\n";
  int mergedCall = 0;

  // Get idx first
  set<Value*> TidRelatedValues;
  for (auto &BB : *F) {
    for (auto &I : BB) {
      if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(&I)) {
        if (II->getIntrinsicID() == NvvmTidIID) {
          TidRelatedValues.insert(II);
          continue;
        } else if (II->getIntrinsicID() == NvvmCtaidIID) {
          TidRelatedValues.insert(II);
          continue;
        }
      }
    }
  }
  errs() << "dump TidRelatedValues\n";
  for (auto &V : TidRelatedValues) {
    V->dump();
  }
  for (auto &BB : *F) {
    int ATCallCount = 0;
    SmallVector<CallInst*, 4> ATs;
    for (auto &I : BB) {
      if (CallInst *CI = dyn_cast<CallInst>(&I)) {
        if (isa<IntrinsicInst>(CI)) {
          continue;
        }
        Function *Callee = CI->getCalledFunction();
        if (false/*Callee == ATFuncTable*/) {
          CI->dump();
          bool ret = isRelated(CI, TidRelatedValues);
          if (ret) {
            errs() << "Is Related\n";
          } else {
            errs() << "Not Related\n";
          }
          ATCallCount++;
          ATs.push_back(CI);
        }
      }
    }
    /*
    for (auto &V : ATs) {
      V->dump();
    }*/
  }
  return mergedCall;
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
string OmpTgtAddrTrans::strFunc(Function *F) {
  string str;
  raw_string_ostream s(str);
  s << F->getName() << "(";
  for (auto itr = F->arg_begin(); itr != F->arg_end(); itr++) {
    if (itr != F->arg_begin()) {
      s << ", ";
    }
    itr->print(s);
  }
  s << ") ";
  return str;
}
string OmpTgtAddrTrans::strVal(Value *V) {
  string str;
  raw_string_ostream s(str);
  V->print(s);
  s << " ";
  return str;
}

DominatorTree &OmpTgtAddrTrans::getDomTree(Function *F) {
  return getAnalysis<DominatorTreeWrapperPass>(*F).getDomTree();
}

static bool isATFunction(Function *Func) {
  // FIXME Add metadata
  if (Func->hasFnAttribute("omp-at-func")) {
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
ATFunctionSet OmpTgtAddrTrans::cloneFuncWithATArg(Function *F) {
  ATFunctionSet ret;
  // Disable Table ver
  ret[OMP_AT_TABLE]  = cloneFuncWithATArgByVer(F, OMP_AT_TABLE);
  ret[OMP_AT_MASK]   = cloneFuncWithATArgByVer(F, OMP_AT_MASK);
  ret[OMP_AT_OFFSET]   = cloneFuncWithATArgByVer(F, OMP_AT_OFFSET);
  return ret;
}

Function * OmpTgtAddrTrans::cloneFuncWithATArgByVer(Function *F, ATVer Opt) {
  // TODO
  vector<Type*> ArgsType;
  // Assert if target function is va_arg
  assert(!F->getFunctionType()->isVarArg() && "AddrTrans should not be VA");

  ValueToValueMapTy VMap;
  for (auto &arg: F->args()) {
    //arg.dump();
    // FIXME
    Type *T = arg.getType();
    /*
    if (EnableAS) {
      if (mayContainHostPtr(&arg)) {
        dp() << "Warning! cloning function with new addressspace";
        arg.dump();
        T = addAddrSpace(T);
      }
    }*/
    ArgsType.push_back(T);
  }

  string postfix;
  switch (Opt) {
    case OMP_AT_TABLE:
      postfix = "_AT_TABLE";
      ArgsType.push_back(ATTablePtrType);
      break;
    case OMP_AT_MASK:
      postfix = "_AT_MASK";
      /* MaskNoArg
      // uintptr_t
      ArgsType.push_back(ITptr);
      */
      break;
    case OMP_AT_OFFSET:
      postfix = "_AT_OFFSET";
      ArgsType.push_back(PTptr);
      break;
  }

  FunctionType *FT = FunctionType::get(F->getReturnType(), ArgsType, false);
  Twine FuncName(F->getName());

  // insert new Function
  Function *NewFunc = Function::Create(FT, F->getLinkage(),
      F->getAddressSpace(), FuncName.concat(postfix), F->getParent());

  // ValueMap for args
  VMap[F] = NewFunc;
  Function::arg_iterator NewArgs = NewFunc->arg_begin();
  for (auto &arg: F->args()) {
    if (VMap.count(&arg) == 0) {
      NewArgs->setName(arg.getName());
      VMap[&arg] = &*NewArgs++;
    }
  }

  // Set additional args name, NewArgs point to new arg
  switch (Opt) {
    case OMP_AT_TABLE:
      NewArgs->setName("_MappingTable");
      //NewArgs->setName("__table_size");
      break;
    case OMP_AT_MASK:
      // MaskNoArg
      //NewArgs->setName("_AT_Mask");
      break;
    case OMP_AT_OFFSET:
      NewArgs->setName("_OffsetTable");
      break;
  }

  // Clone body
  SmallVector<ReturnInst *, 8> Returns; // Ignore returns cloned.
  CloneFunctionInto(NewFunc, F, VMap, /*ModuleLevelChanges=*/true, Returns);
  /*
  if (EnableAS) {
    for (size_t i = 0; i < F->arg_size();i ++) {
      Argument *arg = &NewFunc->arg_begin()[i];
      if (mayContainHostPtr(arg)) {
        Type *T = FT->getParamType(i);
        arg->mutateType(T);
      }
    }
  }*/
  // TODO add attribute
  NewFunc->addFnAttr("omp-at-func");
  dp() << "Clone Function \"" << printFunction(F) << "\"\n\tto \""
    << printFunction(NewFunc) << "\"\n";
  return NewFunc;
}

/*
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
        (
        } else if (CallInst-> what if no body??
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
}*/

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
void OmpTgtAddrTrans::traceArgInFunc(
    Function *Func, Argument *Arg, ATVer Opt) {
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

  InfoPerFunc &Info = AllUserList[Func];

  auto &UserList = Info.TracedUserList;
  //auto &TranslatedResult = Info.TranslatedResult;
  auto &LoadInstListReplacing = Info.LoadInstListReplacing;
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
    std::make_unique<OrderedInstructions>(&getDomTree(Func));

  while (!Vals.empty()) {
    PtrInfo Val = Vals.front();
    // TODO check only Vals is in this function
    Value *V = Val.V;
    Instruction *TranslatedAddr = NULL;
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

    // Add debug metadata
    if (true/*IsDebug*/) {
      if (Instruction *I = dyn_cast<Instruction>(V)) {
        string prefix = "depth";
        I->setMetadata(prefix + to_string(Depth), ATMD);
      }
    }
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
            if (!TranslatedAddr) {
              // FIXME check dominate
              TranslatedAddr = insertATFuncAfter(V, UserList);
              //TranslatedResult[V] = TransResult;
            }
            SI->replaceUsesOfWith(V, TranslatedAddr);
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
        LoadInstListReplacing.insert(LI);
        if (Depth == 0) {
          LI->setMetadata("ompat", ATMD);
          if (!TranslatedAddr) {
            TranslatedAddr = insertATFuncAfter(V, UserList);
          }
          LI->replaceUsesOfWith (V, TranslatedAddr);

          if (typeContainPtr(U->getType())) {
            dp() << "\tLoadinst valContainPtr \n";
            Vals.push({U, 0});
          }
          changed = true;
        } else if (Depth > 0 && typeContainPtr(LI->getType())) {
          dp() << "\tLoadinst valContainPtr \n";
          if (Depth == 1) {
            TranslatedAddr = insertATFuncAfter(LI, UserList, true);
            changed = true;
            Vals.push({TranslatedAddr, 1});
          } else {
            Vals.push({U, Depth - 1});
          }
        }
      } else if (MemCpyInst *MI = dyn_cast<MemCpyInst>(U)) {
        // declare void @llvm.memcpy.p0i8.p0i8.i32 llvm.memcpy.p0i8.p0i8.i64
        //        (i8* <dest>, i8* <src>, i32 <len>, i1 <isvolatile>)
        unsigned ArgIdx = _U->getOperandNo();
        if (ArgIdx == 1) { // this is a load
          Value *dst = MI->getArgOperand(0);
          if (Depth == 0) {
            if (!TranslatedAddr) {
              TranslatedAddr = insertATFuncAfter(V, UserList);
            }
            MI->replaceUsesOfWith (V, TranslatedAddr);
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
            if (!TranslatedAddr) {
              TranslatedAddr = insertATFuncAfter(V, UserList);
            }
            MI->replaceUsesOfWith (V, TranslatedAddr);
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
        if (F->isIntrinsic()) {
          // skip
          continue;
        }
        if (!isATFunction(F)) {
          CI = swapCallInst(CI, Opt);
          changed = true;
          F = CI->getCalledFunction();
        }
        if (Depth == 0) {
          if (!TranslatedAddr) {
            TranslatedAddr = insertATFuncAfter(V, UserList);
          }
          CI->replaceUsesOfWith (V, TranslatedAddr);
          changed = true;
        }
        traceArgInFunc(F, F->arg_begin() + ArgIdx, Opt);
        if (typeContainPtr(CI->getType())) {
          dp() << "\tCallInst valContainPtr ";
          dp() << "Func " << Func->getName() << " arg: " ;
          Arg->print(dp());
          dp() << "\n";
          // TODO
          // Function could return device address Depth would not be 1
          Vals.push({CI, 1});
        }
        OI = std::make_unique<OrderedInstructions>(&getDomTree(Func));
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
            if (!TranslatedAddr) {
              TranslatedAddr = insertATFuncAfter(V, UserList);
            }
            AI->replaceUsesOfWith (V, TranslatedAddr);
            changed = true;
          }
        }
        // TODO  trace??  in else
      } else if (AtomicCmpXchgInst *ACXI = dyn_cast<AtomicCmpXchgInst>(U)) {
        if (ACXI->getPointerOperand() == dyn_cast<Value>(V)) {
          if (Depth == 0) {
            if (!TranslatedAddr) {
              TranslatedAddr = insertATFuncAfter(V, UserList);
            }
            ACXI->replaceUsesOfWith (V, TranslatedAddr);
            changed = true;
          }
        }
        // TODO  trace??  in else
      } else if (ReturnInst *RI= dyn_cast<ReturnInst>(U)) {
        // Return device addr
        if (Depth == 0) {
          if (!TranslatedAddr) {
            TranslatedAddr = insertATFuncAfter(V, UserList);
          }
          RI->replaceUsesOfWith (V, TranslatedAddr);
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
        OI = std::make_unique<OrderedInstructions>(&getDomTree(Func));
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
/*
Instruction *OmpTgtAddrTrans::insertATFuncBefore(Instruction *Inst,
    Value *PtrAddr) {
  InsertedATCount++;
  // old version
  //Argument *ATTableArg = getFuncTableArg(Inst->getFunction());

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
  // old version
  //Args.push_back(ATTableArg);
  CallInst *CI = CallInst::Create(->getFunctionType(), ,
      Args, "TransResult", Inst);
  // insert bitcast
  CastInst *PostCastI = CastInst::Create(Instruction::BitCast, CI,
      PtrAddr->getType(), "PostATCast", Inst);
  //Inst->replaceUsesOfWith (PtrUse->get(), PostCastI);
  Inst->replaceUsesOfWith (PtrAddr, PostCastI);
  //PtrAddr->replaceAllUsesWith (PostCastI);
  UserList.insert(PreCastI);
  UserList.insert(CI);
  UserList.insert(PostCastI);
  return PostCastI;
  fuck
}*/
Instruction *OmpTgtAddrTrans::insertATFuncBefore2(Instruction *Inst,
    Value *PtrAddr) {
  InsertedATCount++;
  dp () << "@Insert translate before Inst: ";
  Inst->print(dp());
  dp() << "\n";
  // insert bitcast
  // Dummy cast as tmp for replace all

  Instruction *PreCastI, *TransI, *PostCastI;
  if (CurATMode == OMP_AT_MASK) {
    // MaskNoArg
    PreCastI = new PtrToIntInst(PtrAddr, ITptr, "PreMaskCast");
    TransI = BinaryOperator::Create(
        BinaryOperator::Or ,Mask0x7f, PreCastI, "Masked");
    TransI->setMetadata("at", ATMD);
    PostCastI = new IntToPtrInst(TransI, Inst->getType(), "PreMaskCast");
    goto END;
  } else if (CurATMode == OMP_AT_OFFSET) {
    PreCastI = CastInst::Create(Instruction::BitCast, PtrAddr,
        AddrType, "PreATCast");
    vector<Value*> Args;
    Args.push_back(PreCastI);
    Args.push_back(getATArg(Inst->getFunction()));
    if (getenv("OMP_OFFSET_CM")) {
      TransI = CallInst::Create(FunctionCallee(ATFuncOffset2),
          Args, "TransResult");
      // NOTE OFFset2 2nd arg is not used
    } else {
      TransI = CallInst::Create(FunctionCallee(ATFuncOffset),
          Args, "TransResult");
    }
    TransI->setMetadata("at", ATMD);
    PostCastI = CastInst::Create(Instruction::BitCast, TransI,
        PtrAddr->getType(), "PostATCast");
    goto END;
  }

  {
  PreCastI = CastInst::Create(Instruction::BitCast, PtrAddr,
      AddrType, "PreATCast");
  vector<Value*> Args;
  Args.push_back(PreCastI);
#if 1
  Args.push_back(getATArg(Inst->getFunction()));
  TransI = CallInst::Create(FunctionCallee(ATFuncTable_tab),
      Args, "TransResult");
#else
  /* no table arg type */
  TransI = CallInst::Create(FunctionCaln(ATFuncTable,Args, "TransResult");
#endif
  TransI->setMetadata("at", ATMD);
  PostCastI = CastInst::Create(Instruction::BitCast, TransI,
      PtrAddr->getType(), "PostATCast");
  }
END:
  if (Instruction *I = dyn_cast<Instruction>(PtrAddr)) {
    PostCastI->insertAfter(I);
    TransI->insertAfter(I);
    PreCastI->insertAfter(I);
  }
  Inst->replaceUsesOfWith (PtrAddr, PostCastI);
  return PostCastI;
}

Instruction *OmpTgtAddrTrans::insertATFuncAfter(Value *V, set<User*> &UserList,
    bool DoReplaceAll) {
  Instruction *Inst = dyn_cast<Instruction>(V);
  InsertedATCount++;
  dp () << "@Insert translate after Inst: ";
  Inst->print(dp());
  dp() << "\n";
  // insert bitcast
  // Dummy cast as tmp for replace all
  Instruction *DummyInst;
  if (DoReplaceAll) {
    DummyInst = new BitCastInst(Inst, Inst->getType(), "dummy", Inst);
    Inst->replaceAllUsesWith(DummyInst);
  }

  Instruction *PreCastI, *TransI, *PostCastI;
  if (CurATMode == OMP_AT_MASK) {
    // MaskNoArg
    PreCastI = new PtrToIntInst(Inst, ITptr, "PreMaskCast");
    TransI = BinaryOperator::Create(
        BinaryOperator::Or ,Mask0x7f, PreCastI, "Masked");
    TransI->setMetadata("at", ATMD);
    PostCastI = new IntToPtrInst(TransI, Inst->getType(), "PreMaskCast");
    goto END;
  } else if (CurATMode == OMP_AT_OFFSET) {
    PreCastI = CastInst::Create(Instruction::BitCast, Inst,
        AddrType, "PreATCast");
    vector<Value*> Args;
    Args.push_back(PreCastI);
    Args.push_back(getATArg(Inst->getFunction()));
    if (getenv("OMP_OFFSET_CM")) {
      TransI = CallInst::Create(FunctionCallee(ATFuncOffset2),
          Args, "TransResult");
      // NOTE OFFset2 2nd arg is not used
    } else {
      TransI = CallInst::Create(FunctionCallee(ATFuncOffset),
          Args, "TransResult");
    }
    PostCastI = CastInst::Create(Instruction::BitCast, TransI,
        V->getType(), "PostATCast");
    goto END;
  }

  {
  PreCastI = CastInst::Create(Instruction::BitCast, Inst,
      AddrType, "PreATCast");
  vector<Value*> Args;
  Args.push_back(PreCastI);
#if 1
  Args.push_back(getATArg(Inst->getFunction()));
  TransI = CallInst::Create(FunctionCallee(ATFuncTable_tab),
      Args, "TransResult");
#else
  /* no table arg type */
  TransI = CallInst::Create(FunctionCaln(ATFuncTable,Args, "TransResult");
#endif
  TransI->setMetadata("at", ATMD);
  PostCastI = CastInst::Create(Instruction::BitCast, TransI,
      V->getType(), "PostATCast");
  }
END:
  PostCastI->insertAfter(Inst);
  TransI->insertAfter(Inst);
  PreCastI->insertAfter(Inst);

  if (DoReplaceAll) {
    // FIXME testing no parent inst
    DummyInst->replaceAllUsesWith(PostCastI);
    DummyInst->dropAllReferences();
    DummyInst->eraseFromParent();
  } else {
    Inst->replaceUsesOfWith (V, PostCastI);
  }
  UserList.insert(PreCastI);
  UserList.insert(TransI);
  UserList.insert(PostCastI);
  return PostCastI;
}

/*
Instruction *OmpTgtAddrTrans::insertATFuncAfter(Value *V) {
  Instruction *Inst = dyn_cast<Instruction>(V);
  InsertedATCount++;
  dp () << "@Insert translate after Inst: ";
  Inst->print(dp());
  dp() << "\n";
  // Get bb
  BasicBlock *BB = Inst->getParent();
  // insert bitcast
  CastInst *PreCastI = CastInst::Create(Instruction::BitCast, Inst,
      AddrType, "PreATCast");

  // insert call
  vector<Value*> Args;
  Args.push_back(PreCastI);
  CallInst *CI = CallInst::Create(->getFunctionType(), ,
      Args, "TransResult");
  // insert bitcast
  CastInst *PostCastI = CastInst::Create(Instruction::BitCast, CI,
      V->getType(), "PostATCast");

  PostCastI->insertAfter(Inst);
  CI->insertAfter(Inst);
  PreCastI->insertAfter(Inst);

  Inst->replaceUsesOfWith (V, PostCastI);

  return PostCastI;
}*/

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
    ATFunctionSet &FS = E.second;
    for (auto &Ver : FS) {
      Function *F = Ver.second;
      vector<Metadata*> MetaList = PreMetaList;
      MetaList.insert(MetaList.begin(), ValueAsMetadata::get(F));
      MDTuple *node = MDNode::get(*context, MetaList);
      NvvmMeta->addOperand(node);
    }
  }
}

// swap CallInst to call AT function
CallInst *OmpTgtAddrTrans::swapCallInst(CallInst *CI, ATVer Opt) {
  // Check if callee is transformed
  Function *Callee = CI->getCalledFunction();
  if (isATFunction(Callee)) {
    return CI;
  } else if (FunctionTrans.find(Callee) == FunctionTrans.end()) {
    FunctionTrans[Callee] = cloneFuncWithATArg(Callee);
  }
  Function *NewCallee = FunctionTrans[Callee][Opt];

  // Get table Arg of parent function
  Argument *TableArg = getATArg(CI->getFunction());

  // Get old arg
  vector<Value*> ArgsOfNew;
  for (auto &operand : CI->args()) {
    ArgsOfNew.push_back(operand);
  }
  if (Opt == OMP_AT_TABLE || Opt == OMP_AT_OFFSET) {
    ArgsOfNew.push_back(TableArg);
  }

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
        EntryList[Entry] = ATFunctionSet();
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

static bool isSimpleLoad(LoadInst *LI) {
  // TODO align
  if (LI->isSimple() && LI->isUnordered()
      && LI->getPointerAddressSpace() == 0) {
    return true;
  } else {
    LI->print(errs());
    errs() << "LoadInst is not simple, ignore replacing\n";
    return false;
  }
  return true;
}

Function *OmpTgtAddrTrans::genFakeLoadFunc(LoadInst *LI) {
  if (!isSimpleLoad(LI)) {
    LI->print(errs());
    errs() << "Not a simple load inst, ignore\n";
    return NULL;
  }
  // TODO add cachae
  static map<PointerType*, Function *> Cache;
  Type *OperandTy = LI->getPointerOperand()->getType();
  if (!isa<PointerType>(OperandTy)) {
    //errs() << "Don't use fake load on load of non-pointer\n";
    //LI->print(errs());
    return NULL;
  }
  PointerType *PT = dyn_cast<PointerType>(OperandTy);
  Type *PointeeTy = PT->getElementType();
  if (!isa<PointerType>(PointeeTy))  {
    //errs() << "Don't use fake load on load of pointer to non-pointer\n";
    //LI->print(errs());
    return NULL;
  }
  auto search = Cache.find(PT);
  if (search != Cache.end()) {
    return search->second;
  }
  vector<Type*> ParamTypes;
  ParamTypes.push_back(PT);
  FunctionType *FakeLoadFuncTy = FunctionType::get(PointeeTy, ParamTypes, false);
  hash_code NameHash((size_t)(uintptr_t)PT);
  string FuncName = "__fload" + to_string(NameHash);
  Function *FakeLoadFunc = Function::Create(FakeLoadFuncTy,
      GlobalValue::ExternalLinkage, FuncName, module);
  //FakeLoadFunc->addFnAttr(Attribute::ReadOnly);
  FakeLoadFunc->addFnAttr(Attribute::NoUnwind);
  FakeLoadFunc->addFnAttr(Attribute::ReadNone);

  // Give fn attr
  FakeLoadFunc->addFnAttr("omp-at-fload");

  Cache[PT] = FakeLoadFunc;
  return FakeLoadFunc;
}

Function *OmpTgtAddrTrans::replaceLoad(LoadInst *LI) {
  Function *f = genFakeLoadFunc(LI);
  if (!f) {
    return f;
  }
  // gen CallInst
  vector<Value*> Args;
  Args.push_back(LI->getPointerOperand());
  CallInst *CI = CallInst::Create(f->getFunctionType(), f, Args, Twine::createNull(), LI);
  LI->replaceAllUsesWith(CI);
  LI->dropAllReferences();
  LI->eraseFromParent();
  return f;
}

int16_t OmpTgtAddrTrans::doSharedMemOpt() {
  // TODO evaluate effect

  // Create shared array
  // @_ZZ13staticReversePiiE1s =
  //   internal addrspace(3) global [64 x i32] undef, align 4
  //   @SMforATTablein = internal addrspace(3) global [480 x i64], align 4

  // Type : intptr * 3 * 20  = 480
  //ArrayType *SMArrayTy = ArrayType::get(ITptr, ATTableEntyNum * MaxATTableSize);
  /* ShareMem declared in ob
  Constant *SMInit = UndefValue::get(SMArrayTy);
  GlobalVariable *SharedMem = new GlobalVariable(*module, SMArrayTy, false,
      GlobalValue::LinkageTypes::PrivateLinkage , SMInit, "SMforATTable",
      nullptr, GlobalValue::ThreadLocalMode::NotThreadLocal, 3);
  SharedMem->setAlignment(64);
  */

  Function *TidFunc = module->getFunction("llvm.nvvm.read.ptx.sreg.tid.x");
  if (!TidFunc) {
    dp() <<  "llvm.nvvm.read.ptx.sreg.tid.x is not found\n";
    return FAILED;
  }

  // Run from each entry
  for (auto E : FunctionTransEntry) {
    Function *F = E.second[OMP_AT_TABLE];
    if (!F) {
      break;
    }
    Instruction *FirstInst = &*F->begin()->begin();
    // NO -> Find first use of table
    // insert AddrSpaceCast as first

    /*
    CastInst *SM2GenericAddr = CastInst::Create(Instruction::AddrSpaceCast,
        SharedMem, ATTablePtrType, "SM2GenericAddr", FirstInst);
        */
    // insert callinst
    //struct ATTableTy *(struct ATTableTy*, struct ATTableTy *sm, int16, int32)
    vector<Value*> StoreTableCallArgs;
    Value *ATArg = getATArg(F);
    StoreTableCallArgs.push_back(ATArg);

    //auto *DummyInst = new AllocaInst(ATArg->getType(), 0, "dummy", &F->front());
    auto *DummyInst = new BitCastInst(ATArg, ATArg->getType(), "dummy", &F->front());
    ATArg->replaceAllUsesWith(DummyInst);

    CallInst *NewTableAddr = CallInst::Create(
        StoreTableFunc->getFunctionType(), StoreTableFunc,
        StoreTableCallArgs, "NewTableAddr", FirstInst);

    // replace use
    DummyInst->replaceAllUsesWith(NewTableAddr);
    DummyInst->dropAllReferences();
    DummyInst->eraseFromParent();
  }
  return SUCCESS;
}

Argument *OmpTgtAddrTrans::getATArg(Function *F) {
  // Check name
  assert(isATFunction(F));
  Argument *ATTableArg = F->arg_end() - 1;
  return ATTableArg;
}
struct PtrInfo {
  Value *V;
  unsigned DevicePtrDepth;
  PtrInfo(Value *V, unsigned D): V(V), DevicePtrDepth(D){
    dp() << "Taint Value: ";
    V->print(dp());
    dp() << "\n";
  };
  PtrInfo(Value *V): V(V), DevicePtrDepth(0){
    dp() << "Taint Value: ";
    V->print(dp());
    dp() << "\n";
  };
};

// FIXME how to swap callinst
void OmpTgtAddrTrans::getHostAddrInsts(Function *F,
    Argument* Arg, HostAddrInsts &CPUInsts) {
  dp() << "\ngetHostAddrInsts on " << strFunc(F) << strVal(Arg) << "\n";
  auto &CPUInstList = CPUInsts[F];
  queue<PtrInfo> Vals;
  Vals.push({Arg, 1});

  while (!Vals.empty()) {
    PtrInfo Taint = Vals.front();
    Vals.pop();
    Taint.V->dump();
    unsigned Depth = Taint.DevicePtrDepth;
    for (auto &use : Taint.V->uses()) {
      User *user = use.getUser();
      user->dump();
      if (CallInst *CI = dyn_cast<CallInst>(user)) {
        unsigned ArgIdx = use.getOperandNo();
        Function *F = CI->getCalledFunction();
        if (F->isIntrinsic()) {
          continue;
        }
        //TODO mark try to swap call inst
        getHostAddrInsts(F, F->arg_begin() + ArgIdx, CPUInsts);
        if (typeContainPtr(CI->getType())) {
          Vals.push({CI});
        }
      } else if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(user)) {
        if (GEPI->getPointerOperand() == use.get()) {
          if (typeContainPtr(GEPI->getType())) {
            Vals.push({GEPI});
          }
        }
      } else if (LoadInst *LI = dyn_cast<LoadInst>(user)) {
        if (Depth == 0) {
          dp() << "Requires AT " << strVal(LI) << "\n";
          CPUInstList.push_back({LI,use.get()});
        }
        if (typeContainPtr(LI->getType())) {
          Vals.push({LI});
        }
      } else if (StoreInst *SI = dyn_cast<StoreInst>(user)) {
        if (SI->getPointerOperand() == use.get()) {
          if (Depth == 0) {
            dp() << "Requires AT " << strVal(SI) << "\n";
            CPUInstList.push_back({SI,use.get()});
          }
        } else {
          // FIXME avoid inf loop??
          llvm::errs() << "StoreInst store taint value";
          exit(38);
        }
      } else if (MemCpyInst *MI = dyn_cast<MemCpyInst>(user)) {
        /*
        unsigned ArgIdx = use.getOperandNo();
        if (ArgIdx == 0) {

        }*/
      } else {
        llvm::errs() << "Error: unknown instruction";
        user->print(llvm::errs());
        llvm::errs() << "n";
      }
    }
  }
}

bool OmpTgtAddrTrans::runOnModule(Module &M) {
  dp() << "Entering OmpTgtAddrTrans\n";
  IsDebug = (bool) getenv("DP2");
  //IsNaiveAT = (bool) getenv("OMP_NAIVE_AT");
  //EnableAS = (bool) getenv("LLVM_AS");
  DisableFakeLoad = true;//(bool) getenv("OMP_NOFL");
  bool changed = false;

  if (init(M)) {
    dp() << "[OmpTgtAddrTrans] Failed to init, exit\n";
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

  if (getenv("OMP_NEW_AT")) {
    dp() << "OMP_NEW_AT\n";
    // Find insertion point first
    HostAddrInsts InstList;
    for (auto E : FunctionTransEntry) {
      Function *F = E.first;
      // All pointer args of entry function is CPU address
      for (size_t i = 0; i < F->arg_size(); i++) {
        Argument *Arg = F->arg_begin() + i;
        if (isArgNeedAT(Arg)) {
          getHostAddrInsts(F, Arg, InstList);
        }
      }
    }
    //goto skip_old;
    // beforehand insertion??

    dp() << "Finished getHostAddrInsts\n";
    for (auto entry : InstList) {
      if (entry.second.size() == 0) {
        continue;
      }
      // Do real clone and insertion here??
      dp() << strFunc(entry.first) << "\n";
      dp() << "there are " << entry.second.size() << "AT\n";
      for (auto &Insertion : entry.second) {
        dp() << strVal(Insertion.I) << "\n";
      }
    }

    // clone functions
    for (auto E : FunctionTransEntry) {
      Function *F = E.first;
      FunctionTransEntry[F] = cloneFuncWithATArg(F);
    }
    addEntryFunctionsAsKernel(FunctionTransEntry);
    for (auto E : InstList) {
      Function *F = E.first;
      // check is entry
      if (FunctionTransEntry.find(F) != FunctionTransEntry.end()) {
        continue;
      }
      FunctionTrans[F] = cloneFuncWithATArg(F);
    }
      // get all callinst
    auto swapCallInstLamdda = [&](FunctionMapTy &Map) {
    for (auto E : Map) {
      ATFunctionSet &FS = E.second;
      for (auto entry : FS) {
        auto scheme = entry.first;
        Function *F = FS[scheme];

        vector<CallInst*> CallInstToSwap;
        for (auto &BB: *F) {
          for (auto &I : BB) {
            if (CallInst *CI = dyn_cast<CallInst>(&I)) {
              if (isa<IntrinsicInst>(CI)) {
                continue;
              }
              Function *Callee = CI->getCalledFunction();
              if (FunctionTrans.find(Callee) == FunctionTrans.end()) {
                continue;
              }
              CallInstToSwap.push_back(CI);
              //if (isFunctionUseAT())
            }
          }
        }
        for (auto CI : CallInstToSwap) {
          swapCallInst(CI, scheme);
        }
      }
    }
    };
    swapCallInstLamdda(FunctionTransEntry);
    swapCallInstLamdda(FunctionTrans);

    //    insert AT
    //    Iterate scheme
    vector<enum ATVer> Schemes;
    //Schemes.push_back(OMP_AT_TABLE);
    //Schemes.push_back(OMP_AT_MASK);
    Schemes.push_back(OMP_AT_OFFSET);
    for (auto scheme : Schemes) {
      InstList.clear();
      CurATMode = scheme;
      for (auto E : FunctionTransEntry) {
        Function *F = FunctionTransEntry[E.first][scheme];
        // All pointer args of entry function is CPU address
        for (size_t i = 0; i < F->arg_size()-1/*NOTE -1*/; i++) {
          Argument *Arg = F->arg_begin() + i;
          if (isArgNeedAT(Arg)) {
            getHostAddrInsts(F, Arg, InstList);
          }
        }
      }
      for (auto E : InstList) {
        Function *F = E.first;
        vector<InsertionAT> &inserts = E.second;
        // for each scheme
        // FIXME no search entry func
        for (auto &insert : inserts) {
          // insert
          insert.Addr->dump();
          insert.I->dump();
          // FIXME prevent duplicate insert
          insertATFuncBefore2(insert.I, insert.Addr);
        }
      }
    }
    goto skip_old;
  }

  for (auto E : FunctionTransEntry) {
    Function *F = E.first;
    FunctionTransEntry[F] = cloneFuncWithATArg(F);
  }

  // Add Functions to metadata
  addEntryFunctionsAsKernel(FunctionTransEntry);

  /*
  if (EnableAS) {
    //FunctionPass *IASP = createInferAddressSpacesPass();
    for (auto E : FunctionTransEntry) {
    //  TODO
      //Function *F = E.second;
      //dp() << printFunction(F);
    }
    return changed;
  }
*/

  for (auto &E : FunctionTransEntry) {
    ATFunctionSet &FS = E.second;
    for (auto &Ver : FS) {
      Function *F = Ver.second;
      CurATMode = Ver.first;
      size_t EntryArgSize = F->arg_size();
      auto ArgItr = F->arg_begin();
      // All pointer args of entry function is CPU address
      for (size_t i = 0; i < EntryArgSize; i++, ArgItr++) {
        if (isArgNeedAT(ArgItr)) {
          traceArgInFunc(F, ArgItr, Ver.first);
        }
      }
    }
  }
skip_old:
  epilogue();


  //doSharedMemOpt();
  dp() << "Inserted " << InsertedATCount << " address tranlation\n";
  dp() << "OmpTgtAddrTrans Finished\n";

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

static bool isFakeLoad(Instruction *I) {
  Function *Callee;
  if (CallInst *CI = dyn_cast<CallInst>(I)) {
    Callee = CI->getCalledFunction();
  } else {
    return false;
  }
  if (!Callee) {
    return false;
  }
  if (!Callee->hasFnAttribute("omp-at-fload")) {
    return false;
  }
  return true;
}
static bool isATInst(Instruction *I) {
  if(I->getMetadata("at")) {
    return true;
  }
  return false;
}

struct SumRecord {
  int InstSum;
  int FakeLoadSum;
  int ATSum;
  SumRecord() {
    InstSum = 0;
    FakeLoadSum = 0;
    ATSum = 0;
  }
};

bool RestoreLoad::runOnModule(Module &M) {
  int Count = 0;
  map<Function*,SumRecord> records;
  for (auto &F : M) {
    if (!isATFunction(&F)) {
      continue;
    }
    SumRecord record;
    vector<Instruction *> fakeLoads;
    for (auto &BB : F) {
      for (auto &I : BB) {
        record.InstSum++;
        if (isFakeLoad(&I)) {
          fakeLoads.push_back(&I);
        }
        if (isATInst(&I)) {
          record.ATSum++;
        }
      }
    }
    for (auto I : fakeLoads) {
      if (CallInst *CI = dyn_cast<CallInst>(I)) {
        if (restoreLoad(CI)) {
          Count++;
          record.FakeLoadSum++;
        }
      }
    }
    if (record.ATSum || record.FakeLoadSum) {
      records[&F] = record;
    }
  }
  if (Count > 0) {
    errs() << "RestoreLoad restored " << Count << " LoadInst\n";
  }
  // Print record append to file
  // User should delete file before new compile
  std::error_code ec;
  static raw_fd_ostream OS("/tmp/AT_stats.txt", ec, sys::fs::OF_Append);
  for (auto &E: records) {
    Function *F = E.first;
    SumRecord &R = E.second;
    OS << F->getName() << " Inst " << R.InstSum <<
      " ATSum " << R.ATSum << " FakeLoadSum " << R.FakeLoadSum << "\n";
  }

  return Count > 0;
}

bool ConcurrentAT::init(Module &M) {
  HasError = false;
  NvvmTidIID = Function::lookupIntrinsicID("llvm.nvvm.read.ptx.sreg.tid.x");
  if (NvvmTidIID == Intrinsic::not_intrinsic) {
    errs() << "nvvm tid intrinsic not found";
    HasError = true;
  }
  NvvmCtaidIID = Function::lookupIntrinsicID("llvm.nvvm.read.ptx.sreg.ctaid.x");
  if (NvvmCtaidIID == Intrinsic::not_intrinsic) {
    errs() << "nvvm ttaid intrinsic not found";
    HasError = true;
  }
  return true;
}
bool ConcurrentAT::runOnModule(Module &M) {
  init(M);
  if (HasError) {
    return false;
  }
  int Count = 0;
  for (auto &F : M) {
    if (!isATFunction(&F)) {
      continue;
    }
    Count += doConcurrentAT(&F);
  }
  errs() << "Merged " << Count << " ATCalls\n";
  return Count > 0;
}

bool RestoreLoad::restoreLoad(CallInst *CI) {
  if (!isFakeLoad(CI)) {
    return false;
  }
  LoadInst *LI = new LoadInst(CI->getType(), CI->getArgOperand(0), "restoredLI",CI);
  CI->replaceAllUsesWith(LI);
  CI->dropAllReferences();
  CI->eraseFromParent();
  return true;
}

char RestoreLoad::ID = 0;
char OmpTgtAddrTrans::ID = 0;
char ConcurrentAT::ID = 0;

static string Description = "OpenMP Offloading with Address Translation";
static string PassBreifName = "ompat";
static string Description2 = "Restore Fake Load CallInst";
static string PassBreifName2 = "reflo";
static string Description3 = "Merger AT calls to concurrent";
static string PassBreifName3 = "ompcat";

#ifdef LLVM_MODULE
static RegisterPass<OmpTgtAddrTrans> Y(PassBreifName, Description);

static RegisterPass<RestoreLoad> X(PassBreifName2, Description2);

//static RegisterPass<ConcurrentAT> Z(PassBreifName3, Description3);

#else
INITIALIZE_PASS(OmpTgtAddrTrans, PassBreifName, Description, false, false)
INITIALIZE_PASS(RestoreLoad, PassBreifName2, Description2, false, false)
//INITIALIZE_PASS(ConcurrentAT, PassBreifName2, Description2, false, false)
ModulePass *llvm::createOmpTgtAddrTransPass() {
  return new OmpTgtAddrTrans();
}
ModulePass *llvm::createRestoreLoadPass() {
  return new RestoreLoad();
}
/*
ModulePass *llvm::createConcurrentATPass() {
  return new ConcurrentAT();
}
*/
#endif

//  Add pass dependency
//  at
//INITIALIZE_PASS_BEGIN(OmpTgtAddrTrans, "OmpTgtAddrTrans", "Description TODO", false, false)
//INITIALIZE_PASS_DEPENDENCY(CallGraphWrapperPass)
//INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
//INITIALIZE_PASS_END(OmpTgtAddrTrans, "OmpTgtAddrTrans", "Description TODO", false, false)
