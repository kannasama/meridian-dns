// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "core/BindExporter.hpp"

#include "common/TimeUtils.hpp"
#include "core/VariableEngine.hpp"
#include "dal/RecordRepository.hpp"
#include "dal/ZoneRepository.hpp"

#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace dns::core {

BindExporter::BindExporter(VariableEngine& veEngine) : _veEngine(veEngine) {}
BindExporter::~BindExporter() = default;

std::string BindExporter::serialize(const dns::dal::ZoneRow& zone,
                                     const std::vector<dns::dal::RecordRow>& vRecords) const {
  std::string sTimestamp = dns::common::nowIso8601();

  // Ensure origin has trailing dot
  std::string sOrigin = zone.sName;
  if (sOrigin.empty() || sOrigin.back() != '.') {
    sOrigin += '.';
  }

  std::ostringstream oss;
  oss << "; Zone: " << zone.sName << "\n";
  oss << "; Exported: " << sTimestamp << "\n";
  oss << "$ORIGIN " << sOrigin << "\n";
  oss << "$TTL 300\n";

  static const std::unordered_set<std::string> kPriorityTypes = {"MX", "SRV"};

  for (const auto& rec : vRecords) {
    if (rec.bPendingDelete) continue;

    std::string sExpanded;
    try {
      sExpanded = _veEngine.expand(rec.sValueTemplate, zone.iId);
    } catch (const std::exception&) {
      oss << "; SKIPPED " << rec.sName << " " << rec.sType << ": expansion failed\n";
      continue;
    }

    oss << rec.sName << "\t" << rec.iTtl << "\tIN\t" << rec.sType << "\t";
    if (kPriorityTypes.count(rec.sType) > 0) {
      oss << rec.iPriority << " ";
    }
    oss << sExpanded << "\n";
  }

  return oss.str();
}

}  // namespace dns::core
