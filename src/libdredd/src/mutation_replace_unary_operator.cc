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

#include "libdredd/mutation_replace_unary_operator.h"

#include <algorithm>
#include <cassert>
#include <initializer_list>
#include <sstream>
#include <vector>

#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/Type.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "libdredd/util.h"
#include "llvm/ADT/StringRef.h"

namespace dredd {

MutationReplaceUnaryOperator::MutationReplaceUnaryOperator(
    const clang::UnaryOperator& unary_operator)
    : unary_operator_(unary_operator) {}

bool MutationReplaceUnaryOperator::IsPrefix(clang::UnaryOperatorKind op) {
  return op != clang::UO_PostInc && op != clang::UO_PostDec;
}
bool MutationReplaceUnaryOperator::IsInvalidReplacementOperator(
    clang::UnaryOperatorKind op) const {
  bool result = (!unary_operator_.getSubExpr()->isLValue() &&
                 (op == clang::UO_PreInc || op == clang::UO_PreDec ||
                  op == clang::UO_PostInc || op == clang::UO_PostDec)) ||
                (unary_operator_.isLValue() &&
                 !(op == clang::UO_PreInc || op == clang::UO_PreDec)) ||
                (op == clang::UO_Not && unary_operator_.getSubExpr()
                                            ->getType()
                                            ->getAs<clang::BuiltinType>()
                                            ->isFloatingPoint());
  return result;
}

std::string MutationReplaceUnaryOperator::GenerateMutatorFunction(
    const std::string& function_name, const std::string& result_type,
    const std::string& input_type,
    const std::vector<clang::UnaryOperatorKind>& operators,
    int& mutation_id) const {
  std::stringstream new_function;
  new_function << "static " << result_type << " " << function_name;
  new_function << "(std::function<" << input_type << "()> arg, ";
  new_function << "int local_mutation_id) {\n";

  int mutant_offset = 0;

  for (const auto op : operators) {
    if (op == unary_operator_.getOpcode() || IsInvalidReplacementOperator(op)) {
      continue;
    }
    new_function << "  if (__dredd_enabled_mutation(local_mutation_id + "
                 << mutant_offset << ")) return ";
    if (IsPrefix(op)) {
      new_function << clang::UnaryOperator::getOpcodeStr(op).str()
                   << "arg();\n";
    } else {
      new_function << "arg()" << clang::UnaryOperator::getOpcodeStr(op).str()
                   << ";\n";
    }
    mutant_offset++;
  }
  new_function << "  if (__dredd_enabled_mutation(local_mutation_id + "
               << mutant_offset << ")) return arg();\n";
  mutant_offset++;

  if (unary_operator_.getOpcode() == clang::UnaryOperatorKind::UO_LNot) {
    // true
    new_function << "  if (__dredd_enabled_mutation(local_mutation_id + "
                 << mutant_offset << ")) return true;\n";
    mutant_offset++;

    // false
    new_function << "  if (__dredd_enabled_mutation(local_mutation_id + "
                 << mutant_offset << ")) return false;\n";
    mutant_offset++;
  }

  new_function << "  return ";
  if (IsPrefix(unary_operator_.getOpcode())) {
    new_function
        << clang::UnaryOperator::getOpcodeStr(unary_operator_.getOpcode()).str()
        << "arg();\n";
  } else {
    new_function
        << "arg()"
        << clang::UnaryOperator::getOpcodeStr(unary_operator_.getOpcode()).str()
        << ";\n";
  }
  new_function << "}\n\n";

  // The function captures |mutant_offset| different mutations, so bump up
  // the mutation id accordingly.
  mutation_id += mutant_offset;

  return new_function.str();
}

void MutationReplaceUnaryOperator::ApplyTypeModifiers(const clang::Expr* expr,
                                                      std::string& type) {
  if (expr->isLValue()) {
    type += "&";
    clang::QualType qualified_type = expr->getType();
    if (qualified_type.isVolatileQualified()) {
      assert(expr->getType().isVolatileQualified() &&
             "Expected expression to be volatile-qualified since subexpression "
             "is.");
      type = "volatile " + type;
    }
  }
}

void MutationReplaceUnaryOperator::Apply(
    clang::ASTContext& ast_context, const clang::Preprocessor& preprocessor,
    int first_mutation_id_in_file, int& mutation_id, clang::Rewriter& rewriter,
    std::unordered_set<std::string>& dredd_declarations) const {
  std::string new_function_name = "__dredd_replace_unary_operator_";

  // A string corresponding to the unary operator forms part of the name of the
  // mutation function, to differentiate mutation functions for different
  // operators
  switch (unary_operator_.getOpcode()) {
    case clang::UnaryOperatorKind::UO_Plus:
      new_function_name += "Plus";
      break;
    case clang::UnaryOperatorKind::UO_Minus:
      new_function_name += "Minus";
      break;
    case clang::UnaryOperatorKind::UO_Not:
      new_function_name += "Not";
      break;
    case clang::UnaryOperatorKind::UO_PreDec:
      new_function_name += "PreDec";
      break;
    case clang::UnaryOperatorKind::UO_PostDec:
      new_function_name += "PostDec";
      break;
    case clang::UnaryOperatorKind::UO_PreInc:
      new_function_name += "PreInc";
      break;
    case clang::UnaryOperatorKind::UO_PostInc:
      new_function_name += "PostInc";
      break;
    case clang::UnaryOperatorKind::UO_LNot:
      new_function_name += "LNot";
      break;
    default:
      assert(false && "Unsupported opcode");
  }

  std::string result_type = unary_operator_.getType()
                                ->getAs<clang::BuiltinType>()
                                ->getName(ast_context.getPrintingPolicy())
                                .str();
  std::string input_type = unary_operator_.getSubExpr()
                               ->getType()
                               ->getAs<clang::BuiltinType>()
                               ->getName(ast_context.getPrintingPolicy())
                               .str();

  // To avoid problems of ambiguous function calls, the argument types (ignoring
  // whether they are references or not) are baked into the mutation function
  // name. Some type names have space in them (e.g. 'unsigned int'); such spaces
  // are replaced with underscores.

  ApplyTypeModifiers(unary_operator_.getSubExpr(), input_type);
  ApplyTypeModifiers(&unary_operator_, result_type);

  std::string input_print_type(input_type);
  input_print_type.erase(
      std::remove(input_print_type.begin(), input_print_type.end(), '&'),
      input_print_type.end());
  new_function_name += "_" + SpaceToUnderscore(input_print_type);

  clang::SourceRange unary_operator_source_range_in_main_file =
      GetSourceRangeInMainFile(preprocessor, unary_operator_);
  assert(unary_operator_source_range_in_main_file.isValid() &&
         "Invalid source range.");
  clang::SourceRange input_source_range_in_main_file =
      GetSourceRangeInMainFile(preprocessor, *unary_operator_.getSubExpr());
  assert(input_source_range_in_main_file.isValid() && "Invalid source range.");

  // Replace the unary operator expression with a call to the wrapper
  // function.
  //
  // Subtracting |first_mutation_id_in_file| turns the global mutation id,
  // |mutation_id|, into a file-local mutation id.
  const int local_mutation_id = mutation_id - first_mutation_id_in_file;
  bool result = rewriter.ReplaceText(
      unary_operator_source_range_in_main_file,
      new_function_name + "(" +
          (unary_operator_.getSubExpr()->isCXX11ConstantExpr(ast_context)
               ? rewriter.getRewrittenText(input_source_range_in_main_file)
               : ("[&]() -> " + input_type + " { return static_cast<" +
                  input_type + ">(" +
                  rewriter.getRewrittenText(input_source_range_in_main_file) +
                  "); }")) +
          ", " + std::to_string(local_mutation_id) + ")");
  (void)result;  // Keep release-mode compilers happy.
  assert(!result && "Rewrite failed.\n");

  std::vector<clang::UnaryOperatorKind> arithmetic_operators = {
      clang::UnaryOperatorKind::UO_PreInc, clang::UnaryOperatorKind::UO_PostInc,
      clang::UnaryOperatorKind::UO_PreDec, clang::UnaryOperatorKind::UO_PostDec,
      clang::UnaryOperatorKind::UO_Not,    clang::UnaryOperatorKind::UO_Plus,
      clang::UnaryOperatorKind::UO_Minus};

  std::vector<clang::UnaryOperatorKind> logical_operators = {
      clang::UnaryOperatorKind::UO_LNot};

  std::string new_function;
  for (const auto& operators : {arithmetic_operators, logical_operators}) {
    if (std::find(operators.begin(), operators.end(),
                  unary_operator_.getOpcode()) != operators.end()) {
      new_function = GenerateMutatorFunction(
          new_function_name, result_type, input_type, operators, mutation_id);
      break;
    }
  }
  assert(!new_function.empty() && "Unsupported opcode.");

  dredd_declarations.insert(new_function);
}

}  // namespace dredd
