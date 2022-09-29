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

#ifndef LIBDREDD_MUTATION_REPLACE_UNARY_OPERATOR_H
#define LIBDREDD_MUTATION_REPLACE_UNARY_OPERATOR_H

#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTFwd.h"
#include "clang/AST/OperationKinds.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "libdredd/mutation.h"
#include "libdredd/protobufs/dredd.pb.h"

namespace dredd {

class MutationReplaceUnaryOperator : public Mutation {
 public:
  explicit MutationReplaceUnaryOperator(
      const clang::UnaryOperator& unary_operator);

  protobufs::MutationGroup Apply(
      clang::ASTContext& ast_context, const clang::Preprocessor& preprocessor,
      bool optimise_mutations, int first_mutation_id_in_file, int& mutation_id,
      clang::Rewriter& rewriter,
      std::unordered_set<std::string>& dredd_declarations) const override;

 private:
  std::string GenerateMutatorFunction(clang::ASTContext& ast_context,
                                      const std::string& function_name,
                                      const std::string& result_type,
                                      const std::string& input_type,
                                      bool optimise_mutations,
                                      int& mutation_id) const;

  [[nodiscard]] static bool IsPrefix(clang::UnaryOperatorKind operator_kind);

  [[nodiscard]] bool IsValidReplacementOperator(
      clang::UnaryOperatorKind operator_kind) const;

  [[nodiscard]] bool IsRedundantReplacementOperator(
      clang::UnaryOperatorKind operator_kind,
      clang::ASTContext& ast_context) const;

  // This returns a string corresponding to the non-mutated expression.
  std::string GetExpr(clang::ASTContext& ast_context) const;

  std::string GetFunctionName(bool optimise_mutations,
                              clang::ASTContext& ast_context) const;

  // Replaces unary operators with other valid unary operators.
  void GenerateUnaryOperatorReplacement(
      const std::string& arg_evaluated, clang::ASTContext& ast_context,
      bool optimise_mutations,
      const std::vector<clang::UnaryOperatorKind>& operators,
      std::stringstream& new_function, int& mutant_offset) const;

  const clang::UnaryOperator& unary_operator_;
};

}  // namespace dredd

#endif  // LIBDREDD_MUTATION_REPLACE_UNARY_OPERATOR_H
