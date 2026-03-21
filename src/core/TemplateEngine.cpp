// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.
#include "core/TemplateEngine.hpp"
#include <chrono>
#include <unordered_map>

namespace dns::core {

common::PreviewResult TemplateEngine::computeComplianceDiff(
    int64_t iZoneId,
    const std::string& sZoneName,
    const std::vector<dal::SnippetRecordRow>& vExpected,
    const std::vector<dal::RecordRow>& vCurrent) {

  // Build lookup: "name\0type" -> current zone RecordRow pointer
  std::unordered_map<std::string, const dal::RecordRow*> mCurrent;
  mCurrent.reserve(vCurrent.size());
  for (const auto& r : vCurrent) {
    mCurrent[r.sName + '\0' + r.sType] = &r;
  }

  common::PreviewResult result;
  result.iZoneId        = iZoneId;
  result.sZoneName      = sZoneName;
  result.tpGeneratedAt  = std::chrono::system_clock::now();

  for (const auto& expected : vExpected) {
    const std::string sKey = expected.sName + '\0' + expected.sType;
    auto it = mCurrent.find(sKey);

    if (it == mCurrent.end()) {
      // Record missing from zone — Add
      common::RecordDiff diff;
      diff.action        = common::DiffAction::Add;
      diff.sName         = expected.sName;
      diff.sType         = expected.sType;
      diff.sSourceValue  = expected.sValueTemplate;
      diff.uTtl          = static_cast<uint32_t>(expected.iTtl);
      diff.iPriority     = expected.iPriority;
      result.vDiffs.push_back(std::move(diff));
    } else {
      const auto* pCurrent = it->second;
      const bool bValueDiffers = (pCurrent->sValueTemplate != expected.sValueTemplate);
      const bool bTtlDiffers   = (pCurrent->iTtl != expected.iTtl);
      const bool bPriDiffers   = (pCurrent->iPriority != expected.iPriority);

      if (bValueDiffers || bTtlDiffers || bPriDiffers) {
        // Record exists but differs — Update
        common::RecordDiff diff;
        diff.action         = common::DiffAction::Update;
        diff.sName          = expected.sName;
        diff.sType          = expected.sType;
        diff.sSourceValue   = expected.sValueTemplate;    // template's desired value
        diff.sProviderValue = pCurrent->sValueTemplate;   // zone's current value
        diff.uTtl           = static_cast<uint32_t>(expected.iTtl);
        diff.iPriority      = expected.iPriority;
        result.vDiffs.push_back(std::move(diff));
      }
      // Identical — no diff
    }
  }

  return result;
}

}  // namespace dns::core
