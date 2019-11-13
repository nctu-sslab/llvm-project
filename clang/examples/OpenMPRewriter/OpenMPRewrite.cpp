//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#include <sstream>
#include <fstream>
#include <iostream>
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Sema/Sema.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Path.h"

using namespace clang;

namespace {

  // Runtime header
std::string RuntimeFuncDecl = 
  "#include <stdio.h>\n"
  "#include <stdint.h>\n"
  "void * __tgt_register_mem(void* mem, uint64_t size)"
  ";\n";
  //"{printf(\"mem :%p, size: %lu\\n\",mem,size);return 1; }\n";

class OpenMPRewriteASTVisitor : public RecursiveASTVisitor<OpenMPRewriteASTVisitor> {
  Rewriter &OpenMPRewriter;
  CompilerInstance &CI;
public:
  OpenMPRewriteASTVisitor(Rewriter &R, CompilerInstance &CI) : OpenMPRewriter(R), CI(CI) {}
  // TODO global array
  // There is alot of case has CK_ArrayToPointerDecay
  // Side effect is very complex to handle
  // LLVM IR pass can handle all case
  bool VisitDeclStmt(DeclStmt *ds) {
    std::vector<std::string> name_list;
    for (auto i = ds->decl_begin(); i != ds->decl_end(); i++) {
      Decl *d = *i;

      if (VarDecl *vd = dyn_cast<VarDecl>(d)) {
        QualType qt = vd->getType().getDesugaredType(CI.getASTContext());
        if (!isa<ArrayType>(qt)) {
          return true;
        }
        enum StorageClass sc = vd->getStorageClass();
        if (sc == SC_PrivateExtern || sc == SC_Extern || sc == SC_Register || sc == SC_Auto) {
          return true;
        }

        std::string var_name =  vd->getNameAsString();
        name_list.push_back(var_name);

      }
    }
    std::stringstream insert_txt;
    for (auto s : name_list) {
      std::cout << s;
      insert_txt << "__tgt_register_mem((void*)" << s << ", sizeof(" << s << "));";
    }
    SourceLocation loc = ds->getEndLoc().getLocWithOffset(1);
    OpenMPRewriter.InsertText(loc, insert_txt.str(), true, true);
    return true;
  }
  bool VisitCallExpr(CallExpr *ce) {
    Expr *e = ce->getCallee()->IgnoreCasts();
    if (DeclRefExpr *dre = dyn_cast<DeclRefExpr>(e)) {
      // check API
      ValueDecl *vd = dre->getDecl();
      if (vd && isa<FunctionDecl>(vd)) {
        FunctionDecl *fd = dyn_cast<FunctionDecl>(vd);
        StringRef sf = vd->getName();
        if (!(sf == "malloc")) {
          return true;
        }
        SourceManager &sm = CI.getSourceManager();
        PresumedLoc PLoc = sm.getPresumedLoc(fd->getSourceRange().getBegin());
        if (PLoc.isInvalid()) {
          return true;
        }
        StringRef HeaderName = llvm::sys::path::filename(PLoc.getFilename());
        if (HeaderName != "stdlib.h") {
          return true;
        }
        if (ce->getNumArgs() > 1) {
          return true;
        }
        SmallVector<char, 25> buffer;
        SourceRange ArgSR = ce->getArg(0)->getSourceRange();
        SourceRange CallSR = ce->getSourceRange();

        if (ArgSR.getBegin().isMacroID()) {
          ArgSR.setBegin(sm.getExpansionLoc(ArgSR.getBegin()));
        }
        if (ArgSR.getEnd().isMacroID()) {
          ArgSR.setEnd(sm.getExpansionLoc(ArgSR.getEnd()));
        }

        unsigned token_size = Lexer::MeasureTokenLength(ArgSR.getEnd(), sm, CI.getLangOpts());
        if (token_size > 1) {
          ArgSR.setEnd(ArgSR.getEnd().getLocWithOffset(token_size - 1));
        }
        const char *begin = sm.getCharacterData(ArgSR.getBegin());
        const char *end = sm.getCharacterData(ArgSR.getEnd());
        if (!begin || !end) {
          llvm::errs() << "Error getCharacterData\n";
          return true;
        }
        std::string s(begin, end+1);
        std::string prefix = "__tgt_register_mem((void*)";
        std::string postfix;
        postfix += "," + s + ")";

        OpenMPRewriter.InsertText(CallSR.getBegin(), prefix, true, true);
        OpenMPRewriter.InsertText(CallSR.getEnd().getLocWithOffset(1), postfix, true, true);
      }
    }
    return true;
  }
};


class OpenMPRewriteConsumer : public ASTConsumer {
  CompilerInstance &CI;
  OpenMPRewriteASTVisitor Visitor;
  Rewriter &OpenMPRewriter;

public:
  OpenMPRewriteConsumer(CompilerInstance &Instance, Rewriter &R)
      : CI(Instance), Visitor(R, Instance), OpenMPRewriter(R) {}

  void HandleTranslationUnit(ASTContext& context) override {
    Visitor.TraverseDecl(context.getTranslationUnitDecl());
    FileID fid = CI.getSourceManager().getMainFileID();
    const RewriteBuffer *buf = OpenMPRewriter.getRewriteBufferFor(fid);
    if (buf) {
			SourceManager &sm = CI.getSourceManager();
			SourceLocation loc = sm.getLocForStartOfFile(fid);
			OpenMPRewriter.InsertText(loc, RuntimeFuncDecl, true, true);

			std::fstream OutFile;
      std::string name = sm.getFilename(loc).str()+".c";
			OutFile.open(name, std::ios::out|std::ios::trunc);
			OutFile.write(std::string(buf->begin(), buf->end()).c_str(), buf->size());
			OutFile.close();
    }
  }
};

class OpenMPRewriteAction : public PluginASTAction {
  Rewriter OpenMPRewriter;
  CompilerInstance *CI;
public:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 llvm::StringRef) override {
    this->CI = &CI;
    SourceManager &SourceMgr = CI.getSourceManager();
    OpenMPRewriter.setSourceMgr(SourceMgr, CI.getLangOpts());
    return llvm::make_unique<OpenMPRewriteConsumer>(CI, OpenMPRewriter);
  }
  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string> &args) override {
    for (unsigned i = 0, e = args.size(); i != e; ++i) {
    }
    if (!args.empty() && args[0] == "help") {
      PrintHelp(llvm::errs());
    }
    return true;
  }
  void PrintHelp(llvm::raw_ostream& ros) {
    ros << "usage: \nclang -cc1 -load <PATH-TO-LLVM-BUILD>/lib/OpenMPRewrite.so -plugin omp-rewtr <INPUT-FILE>\n";
  }
};
}

static FrontendPluginRegistry::Add<OpenMPRewriteAction>
 X("omp-rewtr", "OpenMP Rewriter");
