#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dns::dal {

class ConnectionPool;

struct ZoneTemplateRow {
  int64_t iId = 0;
  std::string sName;
  std::string sDescription;
  std::optional<int64_t> oSoaPresetId;
  std::vector<int64_t> vSnippetIds;   // ordered by sort_order
  std::chrono::system_clock::time_point tpCreatedAt;
  std::chrono::system_clock::time_point tpUpdatedAt;
};

/// Manages zone_templates and zone_template_snippets tables.
/// Class abbreviation: ztr
class ZoneTemplateRepository {
 public:
  explicit ZoneTemplateRepository(ConnectionPool& cpPool);
  ~ZoneTemplateRepository();

  int64_t create(const std::string& sName, const std::string& sDescription,
                 std::optional<int64_t> oSoaPresetId);
  std::vector<ZoneTemplateRow> listAll();
  std::optional<ZoneTemplateRow> findById(int64_t iId);
  void update(int64_t iId, const std::string& sName, const std::string& sDescription,
              std::optional<int64_t> oSoaPresetId);
  void deleteById(int64_t iId);

  /// Replace snippet ordering for a template (ordered by position in vector).
  /// Also flags all zones linked to this template (template_check_pending=TRUE).
  void setSnippets(int64_t iTemplateId, const std::vector<int64_t>& vSnippetIds);

  /// Flag all zones linked to a template as needing compliance check.
  void flagLinkedZones(int64_t iTemplateId);

 private:
  ConnectionPool& _cpPool;
};

}  // namespace dns::dal
