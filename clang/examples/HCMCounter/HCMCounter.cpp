//===----------------------------------------------------------------------===//
// This is a C/C++ Halstead Metrics Calculator Built with clang/llvm
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
using namespace std;

/* https://www.geeksforgeeks.org/software-engineering-halsteads-software-metrics/
 * Counting rules for C language
 * Support Status
 *    [O] Yes
 *    [X] No
 *    [?] TBD
1. [O] Comments are not considered.
2. [X] The identifier and function declarations are not considered
3. [O] All the variables and constants are considered operands.
4. [X] Global variables used in different modules of the same program are counted as multiple occurrences of the same variable.
5. [O] Local variables with the same name in different functions are counted as unique operands.
6. [X] Functions calls are considered as operators.
7. [O] All looping statements e.g., do {…} while ( ), while ( ) {…}, for ( ) {…}, all control statements e.g., if ( ) {…}, if ( ) {…} else {…}, etc. are considered as operators.
8. [O] In control construct switch ( ) {case:…}, switch as well as all the case statements are considered as operators.
9. [O] The reserve words like return, default, continue, break, sizeof, etc., are considered as operators.
10. [O] All the brackets, commas, and terminators are considered as operators.
11. [X] GOTO is counted as an operator and the label is counted as an operand.
12. [O] The unary and binary occurrence of “+” and “-” are dealt separately. Similarly “*” (multiplication operator) are dealt separately.
13. [O] In the array variables such as “array-name [index]” “array-name” and “index” are considered as operands and [ ] is considered as operator.
14. [?] In the structure variables such as “struct-name, member-name” or “struct-name -> member-name”, struct-name, member-name are taken as operands and ‘.’, ‘->’ are taken as operators. Some names of member elements in different structure variables are counted as unique operands.
15. [X] All the hash directive are ignored.
*/

/* TODO
Pragma: PPkeywords
Function call as operator
*/
namespace {

enum Ret {
  SUCCESS,
  FAILED
};

// Init id table for langopts in ASTActionk
static IdentifierTable *IdTable;

#define HCM_DB(s) s
struct HCMResult {
  size_t n1, n2, N1, N2;
  size_t volcabulary, length;
  double estimated_length, volume, difficulty, effort, time, bugs;
  enum Ret ret;
  // Tables to count unique
  size_t Operators[tok::NUM_TOKENS];
  std::unordered_map<string, size_t> Operands;
  HCMResult() : n1(0), n2(0), N1(0), N2(0), ret(FAILED) {
    // set Operators to 0
    memset(Operators, 0, sizeof(Operators));
  }
  void emit() {
    for (size_t i = 0; i < tok::NUM_TOKENS; i++) {
      if (Operators[i] > 0) {
        this->n1 += 1;
        this->N1 += Operators[i];
      }
    }
    for (auto &i : Operands) {
      size_t count = i.second;
      if (count > 0) {
        this->n2 += 1;
        this->N2 += count;
      }
    }
#define LOG2(n) (log(n) / log(2))
    volcabulary = n1 + n2;
    length = N1 + N2;
    estimated_length = n1 * LOG2(n1) + n2 * LOG2(n2);
    volume = length * LOG2(volcabulary);
    difficulty = n1 / 2 * N2 / n2;
    effort = difficulty * volume;
  }
  void dump() {
    llvm::outs() << "# of distinct operators: " << n1 << "\n" <<
      "# of operators: " << N1 << "\n" <<
      "# of distinct operands: " << n2 << "\n" <<
      "# of operands: " << N2 << "\n" <<
      "Volcabulary: "  << volcabulary << "\n" <<
      "Program Length: "  << length << "\n" <<
      "Estimated Program Length: " << estimated_length << "\n" <<
      "Volume: " << volume << "\n" <<
      "Difficulty: " << difficulty << "\n" <<
      "Effort: " << effort << "\n";
  }
};

class HCMCounter: public RecursiveASTVisitor<HCMCounter> {
    CompilerInstance &CI;
    vector<string> Functions;
    SourceManager *sm;

    HCMResult HCMCount(const char *BufferBegin, const char *BufferEnd) {
      HCMResult result;
      // TODO ID table
      SourceLocation Loc;
      Token token;
      Lexer RawLexer(/*Not important*/Loc,
          CI.getLangOpts(), BufferBegin, BufferBegin, BufferEnd);

      bool IsLast;
      while(1) {
        IsLast = RawLexer.LexFromRawLexer(token);
        unsigned len = token.getLength();
        // Count how many kind first
        auto kind = token.getKind();
        if (kind == tok::raw_identifier) {
          // Convert raw id to keywords if possible
          IdentifierInfo &II = IdTable->get(token.getRawIdentifier());
          kind = II.getTokenID();
        }
        if (tok::isAnyIdentifier(kind)) {
          auto TokenStrRef = token.getRawIdentifier();
          result.Operands[TokenStrRef]++;
          HCM_DB(llvm::errs() << "[" << TokenStrRef.str() << "]\n";)

        } else if (tok::isLiteral(kind)) {
          const char *data = token.getLiteralData();
          string TokenStr(data,len);
          result.Operands[TokenStr]++;
          HCM_DB(llvm::errs() << "[" << TokenStr << "]\n";)
        } else {
          switch (kind) { // clang/include/clang/Basic/TokenKinds.def
            case tok::unknown:
            case tok::eof:
            case tok::eod:
            case tok::code_completion:
              ;// ignore
              break;
            case tok::r_square:
            case tok::r_paren:
            case tok::r_brace:
              ; // ignore right part of paired op
              break;
            default:
              // get operators
              HCM_DB(llvm::errs() << "OP: " << tok::getTokenName(kind) << "\n";)
              result.Operators[kind]++;
          }
        }
        if (IsLast) {
          break;
        }
      }
      result.emit();
      result.ret = SUCCESS;
      result.dump();
      return result;
    }
  public:
    HCMCounter(CompilerInstance &CI) : CI(CI) {
      sm = &CI.getSourceManager();
    }
    void run(TranslationUnitDecl *TUD) {
      char Names[200];
      //const char str[] = "pgain dist";
      const char str[] = "main sort";
      memcpy(Names, str, sizeof(str));
      char *token = strtok(Names, " ");
      while (token != NULL)
      {
          Functions.emplace_back(token);
          token = strtok(NULL, " ");
      }
      llvm::errs() << "\n";

      TraverseDecl(TUD);
    }
    // Go function with body
    bool VisitFunctionDecl(FunctionDecl *FD) {
      if (!FD->hasBody()) {
        return true;
      }
      if (!FD->getNameInfo().getName().isIdentifier()) {
        return true;
      }
      bool found = false;
      for (auto itr = Functions.begin(); itr != Functions.end(); itr++) {
        if (FD->getName ().compare(*itr) == 0) {
          llvm::errs() << "Get " << *itr << "\n";
          found = true;
          Functions.erase(itr);
          break;
        }
      }
      // FIXME
      found = true;
      if (!found) {
        return true;
      }

      if (sm->getFileID(FD->getBeginLoc()) != sm->getFileID(FD->getEndLoc())) {
        return true;
      }
      bool invalid = false;
      FileID FID = sm->getFileID(FD->getBeginLoc());
      const llvm::MemoryBuffer *MemBuffer = sm->getBuffer(FID, &invalid);
      if (invalid) {
        llvm::errs() << "getBuffer failed\n";
        return true;
      }
      unsigned StartOffset = sm->getFileOffset(FD->getBeginLoc());
      unsigned EndOffset = sm->getFileOffset(FD->getEndLoc()) + 1;
      if (StartOffset >= EndOffset) {
        llvm::errs() << "Loc range invalid\n";
        return true;
      }
      const char *StartPtr = MemBuffer->getBufferStart() + StartOffset;
      unsigned FuncSize = EndOffset - StartOffset;
      llvm::errs() << "Function Size: " << FuncSize << "\n";
      char *FunctionBody = (char *) malloc(sizeof(char) * (FuncSize + 1));
      memcpy(FunctionBody, StartPtr, FuncSize);
      FunctionBody[FuncSize] = 0;
      HCMCount(FunctionBody, FunctionBody + FuncSize);
      return true;
    }
};

class OpenMPRewriteConsumer : public ASTConsumer {
  CompilerInstance &CI;
  HCMCounter Counter;

public:
  OpenMPRewriteConsumer(CompilerInstance &Instance)
      : CI(Instance), Counter(CI) {}

  void HandleTranslationUnit(ASTContext& context) override {
    llvm::errs() << "HandleTranslationUnit\n";
    Counter.run(context.getTranslationUnitDecl());
    return;
    //Visitor.TraverseDecl(context.getTranslationUnitDecl());
    //const RewriteBuffer *buf = OpenMPRewriter.getRewriteBufferFor(fid);
    /*
    if (buf) {
			SourceManager &sm = CI.getSourceManager();
			SourceLocation loc = sm.getLocForStartOfFile(fid);
		//	OpenMPRewriter.InsertText(loc, RuntimeFuncDecl, true, true);

			std::fstream OutFile;
      std::string name = sm.getFilename(loc).str()+".c";
			OutFile.open(name, std::ios::out|std::ios::trunc);
			OutFile.write(std::string(buf->begin(), buf->end()).c_str(), buf->size());
			OutFile.close();
    }
    */
  }
};

class OpenMPRewriteAction : public PluginASTAction {
  std::string DiffFile;
  bool IsFirstDiff;
  CompilerInstance *CI;
public:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 llvm::StringRef) override {
    IdTable = new IdentifierTable(CI.getLangOpts());
    llvm::errs() << "CreateASTConsumer\n";
    this->CI = &CI;
    //SourceManager &SourceMgr = CI.getSourceManager();
    return llvm::make_unique<OpenMPRewriteConsumer>(CI);
    //return llvm::make_unique<OpenMPRewriteConsumer>(CI, DiffFile, IsFirstDiff);
  }
  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string> &args) override {
    if (!args.empty() && args[0] == "help") {
      PrintHelp(llvm::errs());
    }
    return true;
    if (args.size() != 2) {
      llvm::errs() << "Arg size should be 2, diff file and option\n";
      return true;
    }
    DiffFile = args[0];
    llvm::errs() << "diff file: " << DiffFile << "\n";
    int DiffIndex = atoi(args[1].c_str());
    IsFirstDiff = !DiffIndex;
    llvm::errs() << "Is " << (IsFirstDiff ? "1st" : "2nd") << " diff file\n";
    return true;
  }
  void PrintHelp(llvm::raw_ostream& ros) {
    ros << "usage: \nclang -cc1 -load <PATH-TO-LLVM-BUILD>/lib/OpenMPRewrite.so -plugin omp-rewtr <INPUT-FILE>\n";
  }
};
}

static FrontendPluginRegistry::Add<OpenMPRewriteAction>
 X("hcm", "HCM Counter");
