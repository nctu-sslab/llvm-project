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

using namespace llvm;
using namespace std;

#ifdef DEBUG
#define DB(STMT) STMT
#else
#define DB(STMT)
#endif

#define DEBUG_TYPE "hello"

namespace {
  struct OmpTgtAddrTransPass : public ModulePass {

    typedef  map<Function*, Function*> FunctionMapTy;
    static char ID; // Pass identification, replacement for typeid
    unsigned int ParamID = 1;
    int ConstMemSize = 1024;

    // Types
    IntegerType *IT;
    StructType *ST;
    PointerType *PT;

    OmpTgtAddrTransPass() : ModulePass(ID) {}

    bool runOnFunction(Function &F) {
      return true;
    }

    // Param
    Function *cloneFuncWithATArg(Function *F) {
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
      ArgsType.push_back(IT);
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
      NewArgs++->setName("__ATtable");
      NewArgs->setName("__table_size");
      SmallVector<ReturnInst *, 8> Returns; // Ignore returns cloned.

      // Clone body
      CloneFunctionInto(NewFunc, F, VMap, /*ModuleLevelChanges=*/true, Returns);
      errs() << "cloneFunc " << F->getName() << " to " << NewFunc->getName() << "\n";

      return NewFunc;
    }

    // Recursive
    // Store func called by Target function
    void getCalledFunctions(FunctionMapTy &Functions,
        Function *Target, CallGraph &CG) {

      CallGraphNode *CGN = CG[Target];

      if (!CGN) {
        return;
      }

      // get CallGraph
      for (auto &CR : *CGN) {
        Function *F = CR.second->getFunction();
        getCalledFunctions(Functions, F, CG);
        Functions[F] = NULL;
      }
    }

    void swapCallInst(FunctionMapTy &Functions, Function *F) {
      // Get last two args
      auto arg_it = F->arg_end();
      Value *size_arg = --arg_it;
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
            args.push_back(size_arg);
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

    void eraseFunction(FunctionMapTy FunctionTrans, Function* F) {
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

    bool runOnModule(Module &M) override {

      bool changed = false;
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

      FunctionMapTy FunctionTrans; // Functions after Transform

      // Get entry funcs
      string EntryFuncName("f");
      for (auto &F : M.functions()) {
        if (F.getName().contains(EntryFuncName)) {
          FunctionTrans[&F] = NULL;
          break;
        }
      }
      CallGraph CG(M);
      for (auto E : FunctionTrans) {
        Function *F = E.first;
        // TODO what if cross module call
        getCalledFunctions(FunctionTrans, F, CG);
      }

      if (FunctionTrans.size()) {
        changed = true;
      }
      for (auto E : FunctionTrans) {
        // ValueMap for args
        Function *F = E.first;
        FunctionTrans[F] = cloneFuncWithATArg(F);
      }

      // Change function call
      for (auto E : FunctionTrans) {
        Function *F = E.second;
        swapCallInst(FunctionTrans, F);
      }

      // Remove old functions
      /*for (auto E : FunctionTrans) {
        Function *F = E.first;
        eraseFunction(FunctionTrans, F);
      }*/
      return changed;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      //AU.getRequiredSet();
      //AU.setPreservesCFG();
      //AU.setPreservesAll();
      //AU.addRequiredID(LoopInfoWrapperPass::ID);
      AU.addRequired<LoopInfoWrapperPass>();
      AU.addRequired<DominatorTreeWrapperPass>();
      //AU.getRequiredSet();
    }

  };
}

char OmpTgtAddrTransPass::ID = 0;
static RegisterPass<OmpTgtAddrTransPass>
Y("OmpTgtAddrTransPass", "OmpTgtAddrTransPass");

//INITIALIZE_PASS_BEGIN(OmpTgtAddrTransPass, "OmpTgtAddrTransPass", "Scalar Replacement Of Aggregates", false, false)
//INITIALIZE_PASS_DEPENDENCY(CallGraphWrapperPass)

//INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
//INITIALIZE_PASS_END(OmpTgtAddrTransPass, "OmpTgtAddrTransPass", "Scalar Replacement Of Aggregates", false, false)
