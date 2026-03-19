#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.
#include "common/Types.hpp"
#include "dal/RecordRepository.hpp"
#include "dal/SnippetRepository.hpp"
#include <string>
#include <vector>

namespace dns::core {

/// Pure compliance diff logic — no DB access, fully unit-testable.
/// Compares a zone's current records against a template's expected records.
/// Class abbreviation: te
class TemplateEngine {
 public:
  /// Compare zone's current records against template's expected records.
  /// Returns a PreviewResult with only Add and Update diffs.
  /// Extra records in the zone beyond the template are NOT flagged.
  /// Match key: (name, type) — case-sensitive.
  static common::PreviewResult computeComplianceDiff(
      int64_t iZoneId,
      const std::string& sZoneName,
      const std::vector<dal::SnippetRecordRow>& vExpected,
      const std::vector<dal::RecordRow>& vCurrent);
};

}  // namespace dns::core
