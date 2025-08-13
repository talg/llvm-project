//===--- LfiTaintCheck.cpp - clang-tidy -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LfiTaintCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

using namespace clang::ast_matchers;

namespace clang::tidy::misc {

namespace {

// Represents taint information
struct TaintInfo {
  bool IsTainted = false;
  std::string Sandbox;  // e.g., "jpeg", "png"
  bool IsPointer = false;
  
  // Constructor for convenience
  TaintInfo() = default;
  TaintInfo(bool tainted, StringRef sandbox, bool ptr) 
    : IsTainted(tainted), Sandbox(sandbox.str()), IsPointer(ptr) {}
    
  // Helper to check if two taints are compatible
  bool isCompatibleWith(const TaintInfo &Other) const {
    if (!IsTainted && !Other.IsTainted) return true;
    if (IsTainted && Other.IsTainted) {
      return Sandbox == Other.Sandbox;
    }
    return false;
  }
};

// Check if a VarDecl has a taint annotation and extract info
TaintInfo getVariableTaintInfo(const VarDecl *Var) {
  TaintInfo Info;
  
  // Check variable attributes
  for (const auto *Attr : Var->attrs()) {
    if (const auto *Annotate = dyn_cast<AnnotateAttr>(Attr)) {
      StringRef Ann = Annotate->getAnnotation();
      if (Ann.starts_with("tainted:")) {
        Info.IsTainted = true;
        Info.Sandbox = Ann.substr(8).str(); // Skip "tainted:"
        break;
      }
    }
  }
  
  // Check if it's a pointer type
  Info.IsPointer = Var->getType()->isPointerType();
  
  return Info;
}

// Get taint info from an expression
TaintInfo getExpressionTaintInfo(const Expr *E, ASTContext &Context) {
  TaintInfo Info;
  
  // Strip implicit casts
  E = E->IgnoreParenImpCasts();
  
  // If it's a reference to a variable, get that variable's taint
  if (const auto *DeclRef = dyn_cast<DeclRefExpr>(E)) {
    if (const auto *Var = dyn_cast<VarDecl>(DeclRef->getDecl())) {
      return getVariableTaintInfo(Var);
    }
  }
  
  // Check if expression type is a pointer
  Info.IsPointer = E->getType()->isPointerType();
  
  // TODO: Handle more complex expressions
  // - Binary operations with tainted operands
  // - Function calls returning tainted values
  // - Member access on tainted structs
  
  return Info;
}

// Format a diagnostic message based on taint info
std::string formatTaintDiag(const TaintInfo &Info) {
  if (!Info.IsTainted) {
    return Info.IsPointer ? "untainted pointer" : "untainted value";
  }
  std::string Result = "tainted[" + Info.Sandbox + "] ";
  Result += Info.IsPointer ? "pointer" : "value";
  return Result;
}

} // namespace

void LfiTaintCheck::registerMatchers(MatchFinder *Finder) {
  // Match binary assignment operators
  Finder->addMatcher(
      binaryOperator(hasOperatorName("=")).bind("assignment"), 
      this);
      
  // Match variable declarations with initializers
  Finder->addMatcher(
      varDecl(hasInitializer(anything())).bind("vardecl"),
      this);
}

void LfiTaintCheck::check(const MatchFinder::MatchResult &Result) {
  ASTContext &Context = *Result.Context;
  
  // Check assignments
  if (const auto *Assignment = Result.Nodes.getNodeAs<BinaryOperator>("assignment")) {
    TaintInfo LHSInfo = getExpressionTaintInfo(Assignment->getLHS(), Context);
    TaintInfo RHSInfo = getExpressionTaintInfo(Assignment->getRHS(), Context);
    
    // Rule 1: Cannot assign tainted VALUES to untainted
    if (RHSInfo.IsTainted && !RHSInfo.IsPointer && 
        !LHSInfo.IsTainted && !LHSInfo.IsPointer) {
      diag(Assignment->getOperatorLoc(), 
           "assigning %0 to %1 is not allowed")
          << formatTaintDiag(RHSInfo)
          << formatTaintDiag(LHSInfo);
      diag(Assignment->getOperatorLoc(),
           "tainted values cannot flow to untainted variables",
           DiagnosticIDs::Note);
    }
    
    // Rule 2: Cannot assign between pointers of different address spaces
    if (LHSInfo.IsPointer && RHSInfo.IsPointer) {
      // Check for NULL literal (special case - always allowed)
      if (!Assignment->getRHS()->isNullPointerConstant(Context, 
                                                        Expr::NPC_ValueDependentIsNotNull)) {
        if (LHSInfo.IsTainted != RHSInfo.IsTainted) {
          diag(Assignment->getOperatorLoc(),
               "assigning %0 to %1 is not allowed (incompatible address spaces)")
              << formatTaintDiag(RHSInfo)
              << formatTaintDiag(LHSInfo);
          diag(Assignment->getOperatorLoc(),
               "pointers from sandbox and host have incompatible address spaces",
               DiagnosticIDs::Note);
        } else if (LHSInfo.IsTainted && RHSInfo.IsTainted && 
                   LHSInfo.Sandbox != RHSInfo.Sandbox) {
          diag(Assignment->getOperatorLoc(),
               "assigning %0 to %1 is not allowed (different sandboxes)")
              << formatTaintDiag(RHSInfo)
              << formatTaintDiag(LHSInfo);
        }
      }
    }
    
    // Rule 3: Can assign untainted values to tainted (for passing to sandbox)
    if (!RHSInfo.IsTainted && !RHSInfo.IsPointer && 
        LHSInfo.IsTainted && !LHSInfo.IsPointer) {
      // This is allowed - maybe emit info diagnostic in verbose mode
      // diag(Assignment->getOperatorLoc(),
      //      "assigning untainted value to tainted variable (allowed)",
      //      DiagnosticIDs::Note);
    }
  }
  
  // Check variable declarations with initialization
  if (const auto *VarDecl = Result.Nodes.getNodeAs<clang::VarDecl>("vardecl")) {
    if (const Expr *Init = VarDecl->getInit()) {
      TaintInfo VarInfo = getVariableTaintInfo(VarDecl);
      TaintInfo InitInfo = getExpressionTaintInfo(Init, Context);
      
      // Same rules as assignment
      if (InitInfo.IsTainted && !InitInfo.IsPointer && 
          !VarInfo.IsTainted && !VarInfo.IsPointer) {
        diag(VarDecl->getLocation(),
             "initializing %0 with %1 is not allowed")
            << formatTaintDiag(VarInfo)
            << formatTaintDiag(InitInfo);
      }
      
      if (VarInfo.IsPointer && InitInfo.IsPointer) {
        if (!Init->isNullPointerConstant(Context, Expr::NPC_ValueDependentIsNotNull)) {
          if (VarInfo.IsTainted != InitInfo.IsTainted) {
            diag(VarDecl->getLocation(),
                 "initializing %0 with %1 is not allowed (incompatible address spaces)")
                << formatTaintDiag(VarInfo)
                << formatTaintDiag(InitInfo);
          }
        }
      }
    }
  }
}

} // namespace clang::tidy::misc
