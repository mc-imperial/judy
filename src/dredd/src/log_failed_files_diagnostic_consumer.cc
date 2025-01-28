// Copyright 2024 The Dredd Project Authors
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

#include "dredd/log_failed_files_diagnostic_consumer.h"

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileEntry.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/StringRef.h"

void LogFailedFilesDiagnosticConsumer::HandleDiagnostic(
    clang::DiagnosticsEngine::Level diag_level, const clang::Diagnostic& info) {
  if (diag_level == clang::DiagnosticsEngine::Error ||
      diag_level == clang::DiagnosticsEngine::Fatal) {
    auto file_id = info.getSourceManager().getMainFileID();
    if (file_id.isValid()) {
      files_with_errors.insert(
          info.getSourceManager().getFileEntryForID(file_id)->getName().str());
    }
  }
}
