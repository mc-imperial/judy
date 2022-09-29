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
// limitations under the License

#include "libdredd/util.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include "clang/AST/ASTContext.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/StringRef.h"

namespace dredd {

// Utility method used to avoid spaces when types, such as 'unsigned int', are
// used in mutation function names.
std::string SpaceToUnderscore(const std::string& input) {
  std::string result(input);
  std::replace(result.begin(), result.end(), ' ', '_');
  return result;
}

InfoForSourceRange::InfoForSourceRange(clang::SourceRange source_range,
                                       const clang::ASTContext& ast_context) {
  const auto& source_manager = ast_context.getSourceManager();
  auto char_source_range = clang::CharSourceRange::getTokenRange(source_range);
  assert(char_source_range.isTokenRange() && "Expected a token range.");
  unsigned int final_token_length = clang::Lexer::MeasureTokenLength(
      source_range.getEnd(), source_manager, ast_context.getLangOpts());

  auto start_loc_decomposed =
      source_manager.getDecomposedLoc(source_range.getBegin());
  auto end_loc_decomposed =
      source_manager.getDecomposedLoc(source_range.getEnd());
  auto buffer_data = source_manager.getBufferData(start_loc_decomposed.first);

  start_line = source_manager.getSpellingLineNumber(source_range.getBegin());
  start_column =
      source_manager.getSpellingColumnNumber(source_range.getBegin());
  end_line = source_manager.getSpellingLineNumber(source_range.getEnd());
  end_column = source_manager.getSpellingColumnNumber(source_range.getEnd()) +
               final_token_length;

  unsigned int length = end_loc_decomposed.second -
                        start_loc_decomposed.second + final_token_length;

  if (length <= 36) {
    snippet = buffer_data.substr(start_loc_decomposed.second, length).str();
  } else {
    snippet =
        buffer_data.substr(start_loc_decomposed.second, 10).str() +
        " ... [snip] ... " +
        buffer_data.substr(start_loc_decomposed.second + length - 10, 10).str();
  }
}

}  // namespace dredd
