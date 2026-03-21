#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <string>
#include <vector>

namespace dns::dal {
struct ZoneRow;
struct RecordRow;
}

namespace dns::core {
class VariableEngine;

/// Serializes zone records to BIND-format zone file text.
/// Expands all {{var}} templates using the VariableEngine.
/// Class abbreviation: be
class BindExporter {
 public:
  explicit BindExporter(VariableEngine& veEngine);
  ~BindExporter();

  /// Serialize the given zone and its records to a BIND zone file string.
  /// Records with pending_delete=true are skipped.
  /// Templates are expanded using the VariableEngine for iZoneId.
  /// Expansion failures for individual records are skipped with a comment.
  std::string serialize(const dns::dal::ZoneRow& zone,
                        const std::vector<dns::dal::RecordRow>& vRecords) const;

 private:
  VariableEngine& _veEngine;
};

}  // namespace dns::core
