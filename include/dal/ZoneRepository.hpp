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

/// Row type returned from zone queries.
struct ZoneRow {
  int64_t iId = 0;
  std::string sName;
  int64_t iViewId = 0;
  std::optional<int> oDeploymentRetention;
  bool bManageSoa = false;
  bool bManageNs = false;
  std::string sSyncStatus;  // "unknown", "in_sync", "drift", "error"
  std::optional<std::chrono::system_clock::time_point> oSyncCheckedAt;
  std::optional<int64_t> oGitRepoId;      // FK to git_repos
  std::optional<std::string> oGitBranch;   // Branch override (nullopt = use repo default)
  std::optional<int64_t> oTemplateId;
  bool bTemplateCheckPending = false;
  std::optional<int64_t> oSoaPresetId;
  std::vector<std::string> vTags;
  std::chrono::system_clock::time_point tpCreatedAt;
};

/// Manages the zones table.
/// Class abbreviation: zr
class ZoneRepository {
 public:
  explicit ZoneRepository(ConnectionPool& cpPool);
  ~ZoneRepository();

  /// Create a zone. Returns the new ID.
  int64_t create(const std::string& sName, int64_t iViewId,
                 std::optional<int> oRetention,
                 bool bManageSoa = false, bool bManageNs = false,
                 std::optional<int64_t> oGitRepoId = std::nullopt,
                 std::optional<std::string> oGitBranch = std::nullopt,
                 std::optional<int64_t> oSoaPresetId = std::nullopt);

  /// List all zones.
  std::vector<ZoneRow> listAll();

  /// List zones belonging to a view.
  std::vector<ZoneRow> listByViewId(int64_t iViewId);

  /// Find a zone by ID. Returns nullopt if not found.
  std::optional<ZoneRow> findById(int64_t iId);

  /// Update a zone's name, view, and retention.
  void update(int64_t iId, const std::string& sName, int64_t iViewId,
              std::optional<int> oRetention, bool bManageSoa = false,
              bool bManageNs = false,
              std::optional<int64_t> oGitRepoId = std::nullopt,
              std::optional<std::string> oGitBranch = std::nullopt,
              std::optional<int64_t> oSoaPresetId = std::nullopt);

  /// Update a zone's sync status and set sync_checked_at to NOW().
  void updateSyncStatus(int64_t iZoneId, const std::string& sSyncStatus);

  /// Set or clear the template link; sets template_check_pending=TRUE when linking.
  void setTemplateLink(int64_t iZoneId, std::optional<int64_t> oTemplateId);

  /// Clear template_check_pending flag.
  void clearTemplateCheckPending(int64_t iZoneId);

  /// Replace the zone's tags.
  void updateTags(int64_t iZoneId, const std::vector<std::string>& vTags);

  /// Delete a zone. Cascades to records, variables, deployments.
  void deleteById(int64_t iId);

 private:
  ConnectionPool& _cpPool;
};

}  // namespace dns::dal
