// Copyright 2022 The Dredd Project Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef LIBDREDD_MUTATE_VISITOR_H
#define LIBDREDD_MUTATE_VISITOR_H

#include <memory>
#include <set>
#include <unordered_set>
#include <vector>

#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/LambdaCapture.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Frontend/CompilerInstance.h"
#include "libdredd/mutation.h"

namespace dredd {

class MutateVisitor : public clang::RecursiveASTVisitor<MutateVisitor> {
 public:
  explicit MutateVisitor(const clang::CompilerInstance& compiler_instance);

  bool TraverseDecl(clang::Decl* decl);

  // Overridden in order to avoid visiting the expressions associated with case
  // statements.
  bool TraverseCaseStmt(clang::CaseStmt* case_stmt);

  // Overridden to avoid mutating constant array size expressions.
  bool TraverseConstantArrayTypeLoc(
      clang::ConstantArrayTypeLoc constant_array_type_loc);

  // Overridden to avoid mutating variable array size expressions in C++
  // (because lambdas cannot appear in such expressions).
  bool TraverseVariableArrayTypeLoc(
      clang::VariableArrayTypeLoc variable_array_type_loc);

  // Overridden to avoid mutating array sizes that are derived from template
  // parameters, because after template instantiation these lead to either
  // constant or variable-sized arrays, neither of which can be mutated in C++.
  bool TraverseDependentSizedArrayTypeLoc(
      clang::DependentSizedArrayTypeLoc dependent_sized_array_type_loc);

  // Overridden to avoid mutating template argument expressions, which typically
  // (and perhaps always) need to be compile-time constants.
  bool TraverseTemplateArgumentLoc(
      clang::TemplateArgumentLoc template_argument_loc);

  // Overridden to avoid mutating lambda capture expressions, because the code
  // that can occur in a lambda capture expression is very limited and in
  // particular cannot include other lambdas.
  bool TraverseLambdaCapture(clang::LambdaExpr* lambda_expr,
                             const clang::LambdaCapture* lambda_capture,
                             clang::Expr* init);

  // Overridden to avoid mutating expressions occurring as default values for
  // parameters, because the code that can occur in default values is very
  // limited and cannot include lambdas in general.
  bool TraverseParmVarDecl(clang::ParmVarDecl* parm_var_decl);

  bool VisitExpr(clang::Expr* expr);

  bool VisitCompoundStmt(clang::CompoundStmt* compound_stmt);

  // Overridden to track all source locations associated with variable
  // declarations, in order to avoid mutating variable declaration reference
  // expressions that collide with the declaration of the variable being
  // referenced (this can happen due to the use of "auto").
  bool VisitVarDecl(clang::VarDecl* var_decl);

  // NOLINTNEXTLINE
  bool shouldTraversePostOrder() { return true; }

  [[nodiscard]] const std::vector<std::unique_ptr<Mutation>>& GetMutations()
      const {
    return mutations_;
  }

  [[nodiscard]] clang::SourceLocation GetStartLocationOfFirstDeclInSourceFile()
      const {
    return start_location_of_first_decl_in_source_file_;
  }

 private:
  bool HandleUnaryOperator(clang::UnaryOperator* unary_operator);

  bool HandleBinaryOperator(clang::BinaryOperator* binary_operator);

  static bool IsTypeSupported(clang::QualType qual_type);

  // Determines whether the AST node being visited is directly inside a
  // function, allowing for the visitation point to be inside a variable
  // declaration as long as that declaration is itself directly inside a
  // function. This should return true in cases such as:
  //
  // void foo() {
  //   (*)
  // }
  //
  // and cases such as:
  //
  // void foo() {
  //   int x = (*);
  // }
  //
  // but should return false in cases such as:
  //
  // void foo() {
  //   class A {
  //     static int x = (*);
  //   };
  // }
  bool IsInFunction();

  const clang::CompilerInstance& compiler_instance_;

  // Records the start locat of the very first declaration in the source file,
  // before which Dredd's prelude can be placed.
  clang::SourceLocation start_location_of_first_decl_in_source_file_;

  // Tracks the nest of declarations currently being traversed. Any new Dredd
  // functions will be put before the start of the current nest, which avoids
  // e.g. putting a Dredd function inside a class or function.
  std::vector<const clang::Decl*> enclosing_decls_;

  // These fields track whether a statement contains some sub-statement that
  // might cause control to branch outside of the statement. This needs to be
  // tracked to determine when it is legitimate to move a statement into a
  // lambda to simulate statement removal.
  std::unordered_set<clang::Stmt*> contains_return_goto_or_label_;
  std::unordered_set<clang::Stmt*> contains_break_for_enclosing_loop_or_switch_;
  std::unordered_set<clang::Stmt*> contains_continue_for_enclosing_loop_;
  std::unordered_set<clang::Stmt*> contains_case_for_enclosing_switch_;

  // Records the mutations that can be applied.
  std::vector<std::unique_ptr<Mutation>> mutations_;

  // In C++, it is common to introduce a variable in a boolean guard via "auto",
  // and have the guard evaluate to the variable:
  //
  // if (auto x = ...) {
  //   // Use x
  // }
  //
  // The issue here is that while the Clang AST features separate nodes for the
  // declaration of x and its use in the condition of the if statement, these
  // nodes refer to the same source code locations. It is important to avoid
  // mutating the condition to "if (auto __dredd_function(x) = ...)".
  //
  // To avoid this, the set of all source locations for variable declarations is
  // tracked, and mutations are not applied to expression nodes whose start
  // location is one of these locations.
  std::set<clang::SourceLocation> var_decl_source_locations_;
};

}  // namespace dredd

#endif  // LIBDREDD_MUTATE_VISITOR_H
