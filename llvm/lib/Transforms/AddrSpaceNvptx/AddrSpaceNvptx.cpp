//===- AddrSpaceNvptx.cpp ---------------===//
//
//
//===----------------------------------------------------------------------===//
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/LoopInfo.h"
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
  struct Glob2Const : public ModulePass {
    static char ID; // Pass identification, replacement for typeid
    unsigned int ParamID = 1;
    int ConstMemSize = 1024;
    //string KernelName = "_Z7kernel2PiS_";
    string KernelName = "_Z7kernel2PiS_";
    GlobalVariable *ConstArray;

    Glob2Const() : ModulePass(ID) {}

    bool runOnFunction(Function &F) {
      vector<Instruction*> InstsToDelete;
      if (!F.getName().equals(KernelName)) {
        return false;
      }
      if (!ConstArray) {
        errs() << "No symbol\n";
        return false;
      }
      bool ret = false;
      DB(F.dump();)

      // Check arg size
      if (ParamID >= F.arg_size()) {
        errs() << "Wrong param id\n";
        return false;
      }
      Argument *array = (F.arg_begin() + ParamID);

      for (auto &bb : F) {
        for (auto &inst: bb) {
          if (inst.getOpcode() == Instruction::GetElementPtr) {
            GetElementPtrInst *gepi = dyn_cast<GetElementPtrInst>(&inst);
            if (array == gepi->getPointerOperand()) {
              DB(gepi->dump();)
              vector<Value*> idxs;
              idxs.push_back(ConstantInt::get(IntegerType::get(F.getContext(),64), 0));
              for (auto &idx : gepi->indices ()) {
                idxs.push_back(idx.get());
              }

              // Replace find all inst use the Value
              GetElementPtrInst *gepiNew = GetElementPtrInst::CreateInBounds(ConstArray, idxs,"", gepi);
              AddrSpaceCastInst *asci = new AddrSpaceCastInst(gepiNew, gepi->getType(), "", gepi);
              // Remove from use list
              gepi->replaceAllUsesWith(asci);
              InstsToDelete.push_back(gepi);
              ret = true;
            }
          }
        }
      }

      for (auto inst : InstsToDelete) {
        inst->removeFromParent();
        inst->dropAllReferences();
      }
      DB(F.dump();)
        if(ret) {
          errs() << "Transformed kernel: " << KernelName << "\n";

        }

      return ret;
    }

    // Insert Global symbol
    bool runOnModule(Module &M) override {
      // TODO
      //CallGraphWrapperPass &CP = getAnalysis<CallGraphWrapperPass>();
      LoopInfoWrapperPass &LI = getAnalysis<LoopInfoWrapperPass> (*M.getFunction("main"));
      LI.dump();
      DominatorTreeWrapperPass &DT = getAnalysis<DominatorTreeWrapperPass> (*M.getFunction("main"));
      DT.dump();
      return false;

      string SymbolName(KernelName); 
      bool ret = false;

      // Avoid name conflict
      while (M.getGlobalVariable(genName(SymbolName)));

      // FIXME type is not ready
      ArrayType *at = ArrayType::get(IntegerType::get(M.getContext(),32), ConstMemSize);
      GlobalVariable *gv = new GlobalVariable(at, false, GlobalVariable::ExternalLinkage, ConstantAggregateZero::get(at), SymbolName, GlobalVariable::NotThreadLocal,4, true);
      gv->setAlignment(4);
      gv->setDSOLocal(true);
      gv->setUnnamedAddr(GlobalValue::UnnamedAddr::Local);
      ConstArray = gv;
      for (auto &F : M.functions()) {
        ret = ret || runOnFunction(F);
      }
      if (ret) {
        M.getGlobalList().push_back(gv); 
      }
      return ret;
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

    private:
    string genName(string &SymbolName) {
      SymbolName += "Const";
      SymbolName = "const_array";
      return SymbolName;
    }
  };
}

char Glob2Const::ID = 0;
static RegisterPass<Glob2Const>
Y("Glob2Const", "Test Pass");

//INITIALIZE_PASS_BEGIN(Glob2Const, "Glob2Const", "Scalar Replacement Of Aggregates", false, false)
//INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
//INITIALIZE_PASS_END(Glob2Const, "Glob2Const", "Scalar Replacement Of Aggregates", false, false)
