#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "common/Types.hpp"

namespace dns::dal {
class RecordRepository;
}

namespace dns::core {

/// Server-side record validation: checks DNS correctness rules and returns
/// a list of warnings/errors. Error-severity items block saving (422).
/// Warning-severity items are returned alongside the saved record.
/// Class abbreviation: rv
class RecordValidator {
 public:
  explicit RecordValidator(dns::dal::RecordRepository& rrRepo);
  ~RecordValidator();

  /// Validate a record before create or update.
  /// iZoneId: zone the record belongs to.
  /// sName: record name (e.g. "www", "@").
  /// sType: record type (e.g. "CNAME", "A").
  /// sValue: the value_template string.
  /// oExcludeRecordId: on update, exclude this record ID from coexistence checks.
  std::vector<dns::common::ValidationWarning> validate(
      int64_t iZoneId,
      const std::string& sName,
      const std::string& sType,
      const std::string& sValue,
      std::optional<int64_t> oExcludeRecordId = std::nullopt) const;

 private:
  dns::dal::RecordRepository& _rrRepo;
};

}  // namespace dns::core
