// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "core/RecordValidator.hpp"

#include "dal/RecordRepository.hpp"

#include <algorithm>
#include <string>
#include <unordered_set>

namespace dns::core {

RecordValidator::RecordValidator(dns::dal::RecordRepository& rrRepo)
    : _rrRepo(rrRepo) {}

RecordValidator::~RecordValidator() = default;

std::vector<dns::common::ValidationWarning> RecordValidator::validate(
    int64_t iZoneId,
    const std::string& sName,
    const std::string& sType,
    const std::string& sValue,
    std::optional<int64_t> oExcludeRecordId) const {
  using dns::common::ValidationWarning;

  auto vAll = _rrRepo.listByZoneId(iZoneId);

  // Filter out the record being updated and pending-delete records.
  std::vector<dns::dal::RecordRow> vExisting;
  vExisting.reserve(vAll.size());
  for (const auto& r : vAll) {
    if (r.bPendingDelete) continue;
    if (oExcludeRecordId && r.iId == *oExcludeRecordId) continue;
    vExisting.push_back(r);
  }

  std::vector<ValidationWarning> vWarnings;

  // Check 1 & 2: CNAME coexistence and multiple CNAMEs.
  if (sType == "CNAME") {
    bool bOtherTypeAtSameName = std::any_of(vExisting.begin(), vExisting.end(),
        [&](const dns::dal::RecordRow& r) {
          return r.sName == sName && r.sType != "CNAME";
        });
    if (bOtherTypeAtSameName) {
      vWarnings.push_back({"CNAME_COEXISTENCE", "error",
          "A CNAME cannot coexist with other record types at the same name"});
    }

    bool bExistingCname = std::any_of(vExisting.begin(), vExisting.end(),
        [&](const dns::dal::RecordRow& r) {
          return r.sName == sName && r.sType == "CNAME";
        });
    if (bExistingCname) {
      vWarnings.push_back({"MULTIPLE_CNAMES", "error",
          "Only one CNAME record is allowed per name"});
    }
  } else {
    bool bCnameAtSameName = std::any_of(vExisting.begin(), vExisting.end(),
        [&](const dns::dal::RecordRow& r) {
          return r.sName == sName && r.sType == "CNAME";
        });
    if (bCnameAtSameName) {
      vWarnings.push_back({"CNAME_COEXISTENCE", "error",
          "Cannot add a record alongside an existing CNAME at the same name"});
    }
  }

  // Check 3: Missing trailing dot for FQDN-valued types.
  static const std::unordered_set<std::string> kFqdnTypes = {"CNAME", "MX", "NS", "SRV"};
  if (kFqdnTypes.count(sType) > 0) {
    bool bHasDot    = sValue.find('.') != std::string::npos;
    bool bEndsWithDot = !sValue.empty() && sValue.back() == '.';
    if (bHasDot && !bEndsWithDot) {
      vWarnings.push_back({"MISSING_TRAILING_DOT", "warning",
          sType + " value looks like an FQDN but is missing a trailing dot"});
    }
  }

  // Check 4: Multiple SOA.
  if (sType == "SOA") {
    bool bExistingSoa = std::any_of(vExisting.begin(), vExisting.end(),
        [&](const dns::dal::RecordRow& r) {
          return r.sType == "SOA";
        });
    if (bExistingSoa) {
      vWarnings.push_back({"MULTIPLE_SOA", "warning",
          "More than one SOA record in a zone is not valid (relevant when manage_soa=true)"});
    }
  }

  return vWarnings;
}

}  // namespace dns::core
