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
#ifdef NDEBUG
#define HCM_DB(s)
#else
#define HCM_DB(s) s
#endif
namespace {

enum Ret {
  SUCCESS,
  FAILED
};

const string OutFile("result.txt");

// Init id table for langopts in ASTActionk
static IdentifierTable *IdTable;

struct HCMResult {
  string name;
  unsigned code_size;
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
    estimated_length = LOG2(n1) * n1 + LOG2(n2) * n2;
    volume = LOG2(volcabulary) * length;
    difficulty = (double)n1 / 2 * N2 / n2;
    effort = difficulty * volume;
  }
  void dump() {
#define OUTS_DOUBLE(d) << llvm::format("%.4lf", d)
    llvm::outs()
      << "\t* Distinct operators: " << n1 << "\n"
      << "\t* Distinct operands: " << n2 << "\n"
      << "\t* Operators: " << N1 << "\n"
      << "\t* Operands: " << N2 << "\n"
      << "\t* Volcabulary: "  << volcabulary << "\n"
      << "\t* Program Length: "  << length << "\n"
      << "\t* Estimated Program Length: "
      OUTS_DOUBLE(estimated_length)
      << "\n\t* Volume: "
      OUTS_DOUBLE(volume)
      << "\n\t* Difficulty: "
      OUTS_DOUBLE(difficulty)
      << "\n\t* Effort: "
      OUTS_DOUBLE(effort)
      << "\n";
#undef OUTS_DOUBLE
  }
  string exportJson() {
#define JSON_ENTRY(namestr, value) << "\"" namestr "\":" << value << ","
#define JSON_ENTRY_STR(namestr, value) << "\"" namestr "\":\"" << value << "\","
#define JSON_ENTRY_DOUBLE(namestr, value) \
    << "\"" namestr "\":" <<  llvm::format("%.4lf",value) << ","
#define JSON_ENTRY_DOUBLE_FINAL(namestr, value) \
    << "\"" namestr "\":" <<  llvm::format("%.4lf",value)
    string ret;
    llvm::raw_string_ostream stream(ret);
    stream
      << "{"
      JSON_ENTRY_STR("Name", name)
      JSON_ENTRY("Code size", code_size)
      JSON_ENTRY("Distinct operators", n1)
      JSON_ENTRY("Distinct operands", n2)
      JSON_ENTRY("Operators", N1)
      JSON_ENTRY("Operands", N2)
      JSON_ENTRY("Volcabulary", volcabulary)
      JSON_ENTRY("Program Length", length)
      JSON_ENTRY_DOUBLE("Estimated Program Length", estimated_length)
      JSON_ENTRY_DOUBLE("Volume", volume)
      JSON_ENTRY_DOUBLE("Difficulty", difficulty)
      JSON_ENTRY_DOUBLE_FINAL("Effort", effort)
      << "}";
#undef JSON_ENTRY_DOUBLE
#undef JSON_ENTRY
    return ret;
  }
};
class HCMCalculator {
  CompilerInstance &CI;
  // TODO Add list of result

public:
  HCMCalculator (CompilerInstance &CI) : CI(CI) {}
  /* BufferEnd should be \0 */
  HCMResult HCMonCharRange(const char *BufferBegin, const char *BufferEnd) {
    HCMResult result;
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
            HCM_DB(llvm::errs() << "OP: " << tok::getTokenName(kind) << "\n";)
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
    //result.dump();
    return result;
  }
};
using FuncCheckCallBack = std::function<bool(FunctionDecl*)>;

// Rename function visitor
class MyVisitor: public RecursiveASTVisitor<MyVisitor> {
  CompilerInstance &CI;
  vector<string> Functions;
  //vector<HCMResult> Results;
  vector<string> Results;
  SourceManager *sm;
  HCMCalculator Calc;
  FileID MainFID;
  FuncCheckCallBack *CheckFunction;

public:
  MyVisitor(CompilerInstance &CI, FileID(FID))
    : CI(CI), Calc(CI), MainFID(FID), CheckFunction(nullptr) {
    sm = &CI.getSourceManager();
  }
  vector<string> run(TranslationUnitDecl *TUD) {
    TraverseDecl(TUD);
    return Results;
  }

  vector<string> run(TranslationUnitDecl *TUD, FuncCheckCallBack *CB) {
    CheckFunction = CB;
    TraverseTranslationUnitDecl(TUD);
    return Results;
  }
  bool CheckFuncLoc(FunctionDecl *FD) {
    SourceLocation LocBegin = FD->getBeginLoc();
    SourceLocation LocEnd = FD->getEndLoc();
    // Begin and End should be in Main file
    if (sm->getFileID(LocBegin) != MainFID) {
      return false;
    }
    if (sm->getFileID(LocBegin) != sm->getFileID(LocEnd)) {
      return false;
    }
    unsigned StartOffset = sm->getFileOffset(LocBegin);
    unsigned EndOffset = sm->getFileOffset(LocEnd) + 1;
    if (StartOffset >= EndOffset) {
      llvm::errs() << "Loc range invalid\n";
      return false;
    }
    return true;
  }
  bool HCMonFunction(FunctionDecl *FD) {

    SourceLocation LocBegin = FD->getBeginLoc();
    SourceLocation LocEnd = FD->getEndLoc();

    bool invalid = false;
    const llvm::MemoryBuffer *MemBuffer = sm->getBuffer(MainFID, &invalid);
    if (invalid) {
      llvm::errs() << "getBuffer failed\n";
      return false;
    }
    unsigned StartOffset = sm->getFileOffset(LocBegin);
    unsigned EndOffset = sm->getFileOffset(LocEnd) + 1;

    const char *StartPtr = MemBuffer->getBufferStart() + StartOffset;
    unsigned FuncSize = EndOffset - StartOffset;
    // Copy the data
    char *FunctionBody = (char *) malloc(sizeof(char) * (FuncSize + 1));
    memcpy(FunctionBody, StartPtr, FuncSize);
    FunctionBody[FuncSize] = 0;

    HCMResult result = Calc.HCMonCharRange(FunctionBody, FunctionBody + FuncSize);
    result.name = FD->getNameInfo().getAsString();
    result.code_size = FuncSize;
    Results.push_back(result.exportJson());
    return true;
  }
  // Go function with body
  bool VisitFunctionDecl(FunctionDecl *FD) {
    if (!FD->hasBody()) {
      return true;
    }
    if (!CheckFuncLoc(FD)) {
      return true;
    }
    if (CheckFunction && !(*CheckFunction)(FD)) {
       return true;
    }
    HCMonFunction(FD);
    return true;
  }
};

enum ExtractMode {
  FileNameMode,


};
// Mode 1
class AllFuncConsumer : public ASTConsumer {
  MyVisitor visitor;
public:
  AllFuncConsumer(CompilerInstance &CI)
    : visitor(CI, CI.getSourceManager().getMainFileID()) {}
  // Note This method is called when the ASTs for entire
  //      translation unit have been parsed
  void HandleTranslationUnit(ASTContext& context) override {
    FuncCheckCallBack FuncCheckCB = [] (FunctionDecl *FD) -> bool {
        return true;
    };
    visitor.run(context.getTranslationUnitDecl(), &FuncCheckCB);
    return;
  }
};

// Mode 2
class LocConsumer : public ASTConsumer {
  MyVisitor visitor;
  SourceManager *sm;
  CompilerInstance &CI;
  string InFile;
public:
  LocConsumer(CompilerInstance &CI, string InFile)
    : visitor(CI, CI.getSourceManager().getMainFileID()), CI(CI),
      InFile(InFile) {
        sm = &CI.getSourceManager();
      }
  // Note This method is called when the ASTs for entire
  //      translation unit have been parsed
  void HandleTranslationUnit(ASTContext& context) override {
    vector<unsigned > LineNums;

    // Open InFile to get LineNums
    ifstream stream(InFile.c_str());
    if (!stream.is_open()) {
      llvm::errs() << "Failed to open file \"" << InFile.c_str() << "\"\n";
      return ;
    }
    unsigned line;
    while (stream >> line) {
      LineNums.push_back(line);
    }
    if (stream.bad()) {
      llvm::errs() << "Failed to parse loc file \"" << InFile.c_str() << "\"\n";
      return ;
    }

    auto sm = this->sm;
    FuncCheckCallBack FuncCheckCB = [&LineNums, sm] (FunctionDecl *FD) -> bool {
      bool ret = false;
      SourceLocation LocBegin = FD->getBeginLoc();
      SourceLocation LocEnd   = FD->getEndLoc();
      // The two loc is ensured to be in main file
      unsigned LineBegin = sm->getSpellingLineNumber(LocBegin);
      unsigned LineEnd = sm->getSpellingLineNumber(LocEnd);
      for (auto itr = LineNums.begin(); itr != LineNums.end();) {
        if (*itr >= LineBegin && *itr <= LineEnd) {
          ret = true;
          // erase;
          itr = LineNums.erase(itr);
          continue;
        } else {
          itr++;
        }
      }
      return ret;
    };

    vector<string> results;
    // run function
    results = visitor.run(context.getTranslationUnitDecl(), &FuncCheckCB);

    // Some line are not in functions
    // FIXME  if there is lineno left -> count together
    if (LineNums.size() != 0) {
      llvm::errs() << "Line left: " << LineNums.size() << "\n";
      for (auto line : LineNums) {
        llvm::errs() << line << " ";
      }
      llvm::errs() << "\n";
      HCMCalculator Calc(CI);
      unsigned AllLineSize;
      string AllLineChar;

      FileID MainFID = sm->getMainFileID();
      bool invalid = false;
      const llvm::MemoryBuffer *MemBuffer = sm->getBuffer(MainFID, &invalid);
      if (invalid) {
        llvm::errs() << "getBuffer failed\n";
        return;
      }
      for (auto line : LineNums) {
        // TODO wrap to a function
        SourceLocation LocBegin = sm->translateLineCol (MainFID, line, 1);
        SourceLocation LocEnd = sm->translateLineCol (MainFID, line+1, 1);

        unsigned StartOffset = sm->getFileOffset(LocBegin);
        unsigned EndOffset = sm->getFileOffset(LocEnd);
        const char *StartPtr = MemBuffer->getBufferStart() + StartOffset;
        unsigned LineSize = EndOffset - StartOffset;
        // Copy the data TODO use smart pointer
        char *LineBody = (char *) malloc(sizeof(char) * (LineSize + 1));
        memcpy(LineBody, StartPtr, LineSize + 1);
        LineBody[LineSize] = 0;
        AllLineChar += LineBody;
        free(LineBody);
      }
      /*
      HCMResult RestResult = Calc.HCMonCharRange(AllLineChar.data(),
          AllLineChar.data() + AllLineSize + 1);
      RestResult.code_size = AllLineSize;
      RestResult.name = "NonFunctionLine";
      results.push_back(RestResult.exportJson());
      */
    }
    string FinalResult;
    int i = 0;
    FinalResult += "{";
    for (auto it = results.begin(); it != results.end(); it++) {
      string prefix;
      if (it != results.begin()) {
        prefix += ",";
      }
      prefix += "\"" + to_string(i++) + "\":";
      FinalResult += prefix + *it;
    }
    FinalResult += "}";
    llvm::outs() << FinalResult;
    // store result to outfile
    return;
  }
};

// Mode 3
class FuncListConsumer : public ASTConsumer {
  MyVisitor visitor;
  string InFile;
public:
  FuncListConsumer(CompilerInstance &CI, string InFile)
    : visitor(CI, CI.getSourceManager().getMainFileID()), InFile(InFile) {}
  // Note This method is called when the ASTs for entire
  //      translation unit have been parsed
  vector<string> ParseFuncList(string FuncList) {
    vector<string> Functions;
    // Use find and substr
    return Functions;
  }
  void HandleTranslationUnit(ASTContext& context) override {
    string FuncsStr;
    vector<string> Functions = ParseFuncList(FuncsStr);
    FuncCheckCallBack FuncCheckCB = [&Functions] (FunctionDecl *FD) -> bool {
      bool found = false; for (auto itr = Functions.begin(); itr != Functions.end(); itr++) {
        // FIXME  getName is not allow for special funcs
        if (FD->getNameInfo().getAsString().compare(*itr) == 0) {
          found = true;
          Functions.erase(itr);
          break;
        }
      }
      if (!found) {
        return false;
      }
      return true;
    };
    visitor.run(context.getTranslationUnitDecl(), &FuncCheckCB);
    return;
  }
};
// Mode 4
void PlainTextConsumer(CompilerInstance &CI, string InFile) {
  string content, line;
  ifstream myfile (InFile);
  if (myfile.is_open())
  {
    while ( getline (myfile,line) )
    {
      content += line;
    }
    myfile.close();
  }
  HCMCalculator Calc(CI);
  HCMResult result = Calc.HCMonCharRange(content.c_str(),
      content.c_str() + content.size());
  result.name = "plain text";
  result.code_size = content.size();
  llvm::outs() << result.exportJson();
}

class NothingConsumer : public ASTConsumer {};
    /* output to file
      std::fstream OutFile;
      std::string name = sm.getFilename(nloc).str()+".c";
			OutFile.open(name, std::ios::out|std::ios::trunc);
			OutFile.write(std::string(buf->begin(), buf->end()).c_str(), buf->size());
			OutFile.close();
    */
class MyASTAction : public PluginASTAction {
  std::string InFile;
  CompilerInstance *CI;
  int mode;
public:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(
      CompilerInstance &CI, llvm::StringRef) override {
    IdTable = new IdentifierTable(CI.getLangOpts());
    this->CI = &CI;
    switch (mode) {
      case 1:
        //llvm::outs() << "AllFuncConsumer\n";
        return llvm::make_unique<AllFuncConsumer>(CI);
      case 2:
        //llvm::outs() <<"LocConsumer\n";
        return llvm::make_unique<LocConsumer>(CI, InFile);
      case 3:
        //llvm::outs() << "FuncListConsumer\n";
        return llvm::make_unique<FuncListConsumer>(CI, InFile);
      case 4:
        // No need to parse
        PlainTextConsumer(CI, InFile);
        return llvm::make_unique<NothingConsumer>();
      default:
        llvm::outs() << "Invalid Option\n";
        return llvm::make_unique<NothingConsumer>();
    }
  }
  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string> &args) override {

    if (!args.empty() && args[0] == "help") {
      PrintHelp(llvm::errs());
    }
    if (args.size() > 2) {
      llvm::errs() << "Arg size should not > 2, option and a file name\n";
      return false;
    }
    if (args.size() == 0) {
      mode = 0;
      return true;
    }
    char *endptr;
    mode = strtol(args[0].c_str(), &endptr, 10);
    if (*endptr != '\0') {
      llvm::errs() << "invalid option\n";
      return false;
    }
    if (args.size() == 2) {
      InFile = args[1];
    }
    /*
     * Modes
     * 1: Every functions with body in the main file
     * 2: Loc to per function
     * 3: Only Function names in append file
     * 4: Entire files in append file
     */
    return true;
  }
  void PrintHelp(llvm::raw_ostream& ros) {
    ros << "usage: \nclang -cc1 -load <PATH-TO-LLVM-BUILD>/lib/HCMCounter.so -plugin hcm <INPUT-FILEs> [-plugin-arg-hcm <mode num>] [-plugin-arg-hcm <append file>] \n";
  }
};
}

static FrontendPluginRegistry::Add<MyASTAction> X("hcm", "Halstead complexity calculator for C/C++ program");
