#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/raw_ostream.h"
#include <set>
#include <vector>

using namespace clang;

namespace {

class DesugarMemberAccessVisitor : public RecursiveASTVisitor<DesugarMemberAccessVisitor> {
private:
  CompilerInstance &CI;
  ASTContext &Context;
  Rewriter &TheRewriter;
  
  // Track which expressions we've already transformed
  std::set<const Expr*> TransformedExprs;
  
  struct ChainElement {
    std::string memberName;
    bool isArrow;
    bool needsTransform;
    StringRef macroName;
  };
  
  // Helper to check if a type has our attribute
  const DesugarMemberAccessAttr* getDesugarAttr(QualType Type) {
    // Remove pointer/reference if needed
    if (Type->isPointerType()) {
      Type = Type->getPointeeType();
    } else if (Type->isReferenceType()) {
      Type = Type.getNonReferenceType();
    }
    
    // Get the record type
    if (const auto *RT = Type->getAs<RecordType>()) {
      if (const auto *Record = dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        return Record->getAttr<DesugarMemberAccessAttr>();
      }
    }
    return nullptr;
  }
  
  // Get the base expression as a string
  std::string getBaseExprAsString(const Expr *E) {
    // Special handling for implicit this
    if (isa<CXXThisExpr>(E)) {
      return "this";
    }
    
    SourceManager &SM = CI.getSourceManager();
    SourceLocation Start = E->getBeginLoc();
    SourceLocation End = E->getEndLoc();
    
    // Get the actual end location
    End = Lexer::getLocForEndOfToken(End, 0, SM, CI.getLangOpts());
    
    // Get the source text
    StringRef Text = Lexer::getSourceText(
      CharSourceRange::getCharRange(Start, End), 
      SM, 
      CI.getLangOpts()
    );
    return Text.str();
  }
  
  // Collect the full member chain from innermost to outermost
  void collectMemberChain(MemberExpr *ME, std::vector<ChainElement> &chain, Expr *&baseExpr) {
    // Add this member to the chain
    const auto *Attr = getDesugarAttr(ME->getBase()->getType());
    chain.push_back({
      ME->getMemberDecl()->getNameAsString(),
      ME->isArrow(),
      Attr != nullptr,
      Attr ? Attr->getMacroName() : ""
    });
    
    // Recursively process the base if it's another member expression
    if (auto *BaseME = dyn_cast<MemberExpr>(ME->getBase())) {
      collectMemberChain(BaseME, chain, baseExpr);
    } else {
      // We've reached the base expression
      baseExpr = ME->getBase();
    }
  }
  
  // Generate the nested transformation string
  std::string generateTransformation(const std::string &baseStr, 
                                   const std::vector<ChainElement> &chain) {
    std::string result = baseStr;
    
    // Process from outermost to innermost (reverse order)
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
      if (it->needsTransform) {
        // Wrap in macro call
        std::string accessType = it->isArrow ? "1" : "2";
        result = it->macroName.str() + "(" + accessType + ", " + result + ", " + it->memberName + ")";
      } else {
        // Normal member access
        result = result + (it->isArrow ? "->" : ".") + it->memberName;
      }
    }
    
    return result;
  }
  
  // Get the outermost member expression in a chain
  MemberExpr* getOutermostMemberExpr(MemberExpr *ME) {
    MemberExpr *current = ME;
    
    // Walk up the AST to find the outermost member expression
    while (true) {
      auto parents = Context.getParents(*current);
      if (parents.empty()) break;
      
      bool foundParentMember = false;
      for (const auto &parent : parents) {
        if (const auto *ParentME = parent.get<MemberExpr>()) {
          current = const_cast<MemberExpr*>(ParentME);
          foundParentMember = true;
          break;
        }
      }
      
      if (!foundParentMember) break;
    }
    
    return current;
  }
  
  // Transform a complete member chain
  void transformMemberChain(MemberExpr *outermostME) {
    // Skip if already transformed
    if (TransformedExprs.count(outermostME)) {
      return;
    }
    
    std::vector<ChainElement> chain;
    Expr *baseExpr = nullptr;
    
    // Collect the chain
    collectMemberChain(outermostME, chain, baseExpr);
    
    // Check if any element needs transformation
    bool needsAnyTransform = false;
    for (const auto &elem : chain) {
      if (elem.needsTransform) {
        needsAnyTransform = true;
        break;
      }
    }
    
    if (!needsAnyTransform) {
      return;
    }
    
    // Get the base expression as string
    std::string baseStr = getBaseExprAsString(baseExpr);
    
    // Generate the transformation
    std::string transformation = generateTransformation(baseStr, chain);
    
    // Apply the transformation to the outermost expression
    SourceRange range(outermostME->getBeginLoc(), outermostME->getEndLoc());
    TheRewriter.ReplaceText(range, transformation);
    
    // Mark all member expressions in this chain as transformed
    MemberExpr *current = outermostME;
    while (current) {
      TransformedExprs.insert(current);
      current = dyn_cast<MemberExpr>(current->getBase());
    }
    
    llvm::errs() << "Transformed: " << getBaseExprAsString(outermostME) 
                 << " => " << transformation << "\n";
  }
  
  // Transform a member call expression
  void transformMemberCall(CXXMemberCallExpr *CE, MemberExpr *ME) {
    // Skip if already transformed
    if (TransformedExprs.count(ME)) {
      return;
    }
    
    // Debug output
    llvm::errs() << "\nDEBUG transformMemberCall:\n";
    llvm::errs() << "  Full call expression: " << getBaseExprAsString(CE) << "\n";
    llvm::errs() << "  Method MemberExpr: " << getBaseExprAsString(ME) << "\n";
    llvm::errs() << "  Method name: " << ME->getMemberDecl()->getNameAsString() << "\n";
    
    // Debug: print the base of the method's MemberExpr
    Expr *MethodBase = ME->getBase();
    llvm::errs() << "  Method base type: " << MethodBase->getStmtClassName() << "\n";
    llvm::errs() << "  Method base as string: " << getBaseExprAsString(MethodBase) << "\n";
    
    std::vector<ChainElement> chain;
    Expr *baseExpr = nullptr;
    
    // Collect the chain for the member expression
    collectMemberChain(ME, chain, baseExpr);
    
    llvm::errs() << "  Chain collected (" << chain.size() << " elements):\n";
    for (size_t i = 0; i < chain.size(); ++i) {
      llvm::errs() << "    [" << i << "] " << chain[i].memberName 
                   << " (needsTransform=" << chain[i].needsTransform 
                   << ", isArrow=" << chain[i].isArrow << ")\n";
    }
    llvm::errs() << "  Base expression: " << getBaseExprAsString(baseExpr) << "\n";
    
    // The first element (index 0) should be the method
    if (chain.empty() || !chain[0].needsTransform) {
      return;
    }
    
    // Get the base expression as string
    std::string baseStr = getBaseExprAsString(baseExpr);
    
    // Generate the transformation for everything except the method
    std::string result = baseStr;
    
    // Process from outermost to method (reverse order, but skip the method itself)
    // chain is: [method, inner, middle, ...] 
    // We want to process: [..., middle, inner] (skip method)
    for (auto it = chain.rbegin(); it != chain.rend() - 1; ++it) {
      if (it->needsTransform) {
        std::string accessType = it->isArrow ? "1" : "2";
        result = it->macroName.str() + "(" + accessType + ", " + result + ", " + it->memberName + ")";
      } else {
        result = result + (it->isArrow ? "->" : ".") + it->memberName;
      }
    }
    
    // Now handle the method call (the first element in chain)
    const auto &methodElem = chain[0];
    std::string accessType = methodElem.isArrow ? "1" : "2";
    
    // Build the arguments string
    std::string argsStr;
    for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
      if (i > 0) argsStr += ", ";
      argsStr += getBaseExprAsString(CE->getArg(i));
    }
    
    // Build the final transformation
    std::string transformation = methodElem.macroName.str() + "(" + accessType + ", " + 
                                result + ", " + methodElem.memberName;
    if (!argsStr.empty()) {
      transformation += ", " + argsStr;
    }
    transformation += ")";
    
    // Apply the transformation
    SourceRange range(CE->getBeginLoc(), CE->getEndLoc());
    TheRewriter.ReplaceText(range, transformation);
    
    // Mark as transformed
    TransformedExprs.insert(CE);
    TransformedExprs.insert(ME);
    
    llvm::errs() << "Transformed call: " << getBaseExprAsString(CE) 
                 << " => " << transformation << "\n";
  }
  
public:
  DesugarMemberAccessVisitor(CompilerInstance &CI, ASTContext &Context, Rewriter &R)
    : CI(CI), Context(Context), TheRewriter(R) {}
  
  // Visit member expressions
  bool VisitMemberExpr(MemberExpr *ME) {
    // Skip if this is a method (will be handled by call expression)
    if (isa<CXXMethodDecl>(ME->getMemberDecl())) {
      return true;
    }
    
    // Skip implicit this access inside member functions
    if (ME->isImplicitAccess()) {
      // For implicit access like 'value' inside a method, it's really 'this->value'
      // We'll skip transforming these for now as they require special handling
      llvm::errs() << "Skipping implicit member access: " 
                   << ME->getMemberDecl()->getNameAsString() << "\n";
      return true;
    }
    
    // Find the outermost member expression in this chain
    MemberExpr *outermostME = getOutermostMemberExpr(ME);
    
    // Transform the complete chain
    transformMemberChain(outermostME);
    
    return true;
  }
  
  // Visit member function calls
  bool VisitCXXMemberCallExpr(CXXMemberCallExpr *CE) {
    if (auto *ME = dyn_cast<MemberExpr>(CE->getCallee())) {
      transformMemberCall(CE, ME);
    }
    
    return true;
  }
  
  // Visit the class declaration itself
  bool VisitCXXRecordDecl(CXXRecordDecl *Record) {
    if (auto *Attr = Record->getAttr<DesugarMemberAccessAttr>()) {
      llvm::errs() << "Processing class with __desugar_member_access attribute:\n";
      llvm::errs() << "  Class: " << Record->getQualifiedNameAsString() << "\n";
      llvm::errs() << "  Macro: " << Attr->getMacroName() << "\n\n";
    }
    return true;
  }
};

class DesugarMemberAccessConsumer : public ASTConsumer {
private:
  CompilerInstance &CI;
  Rewriter TheRewriter;
  
public:
  DesugarMemberAccessConsumer(CompilerInstance &CI) : CI(CI) {
    TheRewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
  }
  
  void HandleTranslationUnit(ASTContext &Context) override {
    llvm::errs() << "HandleTranslationUnit called\n";
    
    // Create and run the visitor
    DesugarMemberAccessVisitor Visitor(CI, Context, TheRewriter);
    Visitor.TraverseDecl(Context.getTranslationUnitDecl());
    
    // Get the rewritten content
    const FileID MainFileID = CI.getSourceManager().getMainFileID();
    
    // Check if we have any rewrites
    if (TheRewriter.buffer_begin() == TheRewriter.buffer_end()) {
      llvm::errs() << "No transformations applied.\n";
      // Output original file
      auto MainFileBuf = CI.getSourceManager().getBufferOrNone(MainFileID);
      if (MainFileBuf) {
        llvm::outs() << MainFileBuf->getBuffer();
      }
    } else {
      // We have rewrites, output them
      TheRewriter.getEditBuffer(MainFileID).write(llvm::outs());
    }
  }
};

class DesugarMemberAccessAction : public PluginASTAction {
protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef File) override {
    llvm::errs() << "DesugarMemberAccess plugin activated for file: " << File << "\n";
    return std::make_unique<DesugarMemberAccessConsumer>(CI);
  }
  
  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string> &Args) override {
    return true;
  }
  
  PluginASTAction::ActionType getActionType() override {
    return AddBeforeMainAction;
  }
};

} // namespace

static FrontendPluginRegistry::Add<DesugarMemberAccessAction>
    X("desugar-member-access", "Desugar member access using specified macro");

// Force the registration to not be optimized out
volatile int DesugarMemberAccessAnchorSource = 0;
