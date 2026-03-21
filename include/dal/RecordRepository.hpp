#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "common/Types.hpp"

namespace dns::dal {

class ConnectionPool;

/// Row type returned from record queries.
struct RecordRow {
  int64_t iId = 0;
  int64_t iZoneId = 0;
  std::string sName;
  std::string sType;
  int iTtl = 300;
  std::string sValueTemplate;
  int iPriority = 0;
  nlohmann::json jProviderMeta;  // nullable JSONB from DB
  bool bPendingDelete = false;
  std::optional<int64_t> oLastAuditId;
  std::chrono::system_clock::time_point tpCreatedAt;
  std::chrono::system_clock::time_point tpUpdatedAt;
};

/// Manages the records table (raw templates); upsert for rollback restore.
/// Class abbreviation: rr
class RecordRepository {
 public:
  explicit RecordRepository(ConnectionPool& cpPool);
  ~RecordRepository();

  /// Create a record. Returns the new ID.
  int64_t create(int64_t iZoneId, const std::string& sName, const std::string& sType,
                 int iTtl, const std::string& sValueTemplate, int iPriority,
                 const nlohmann::json& jProviderMeta = nullptr);

  /// List records for a zone.
  virtual std::vector<RecordRow> listByZoneId(int64_t iZoneId);

  /// Find a record by ID. Returns nullopt if not found.
  std::optional<RecordRow> findById(int64_t iId);

  /// Update a record.
  void update(int64_t iId, const std::string& sName, const std::string& sType,
              int iTtl, const std::string& sValueTemplate, int iPriority,
              const nlohmann::json& jProviderMeta = nullptr);

  /// Soft-delete a record by ID (sets pending_delete = true).
  void deleteById(int64_t iId);

  /// Restore a soft-deleted record (clears pending_delete flag).
  void restoreById(int64_t iId);

  /// Hard-delete all pending-delete records for a zone (after successful push).
  int hardDeletePending(int64_t iZoneId);

  /// Delete all records for a zone. Returns deleted count.
  int deleteAllByZoneId(int64_t iZoneId);

  /// Batch soft-delete records by IDs. All must belong to iZoneId.
  void batchSoftDelete(int64_t iZoneId, const std::vector<int64_t>& vRecordIds);

  /// Create multiple records in a single transaction. Returns created IDs.
  /// Throws ValidationError with per-record details on failure.
  std::vector<int64_t> createBatch(int64_t iZoneId,
                                    const std::vector<std::tuple<std::string, std::string,
                                                                 int, std::string, int>>& vRecords);

  /// Batch update records. Each entry specifies an ID and optional field overrides.
  /// All records must belong to iZoneId. Executes in a single transaction.
  struct BatchUpdateEntry {
    int64_t iId = 0;
    std::optional<std::string> oName;
    std::optional<std::string> oType;
    std::optional<int> oTtl;
    std::optional<std::string> oValueTemplate;
    std::optional<int> oPriority;
    std::optional<nlohmann::json> oProviderMeta;
  };
  void batchUpdate(int64_t iZoneId, const std::vector<BatchUpdateEntry>& vUpdates);

  /// Upsert a record by ID. If the ID exists, update it. Otherwise, create a new record.
  /// Returns the record ID (existing or newly created).
  int64_t upsertById(int64_t iId, int64_t iZoneId, const std::string& sName,
                     const std::string& sType, int iTtl,
                     const std::string& sValueTemplate, int iPriority);

  /// Search records across all zones. Returns up to 200 results.
  /// Matches against record name, value_template, and parent zone name (case-insensitive).
  std::vector<dns::common::SearchResult> search(
      const std::string& sQuery,
      const std::optional<std::string>& osType,
      const std::optional<int64_t>& oiZoneId,
      const std::optional<int64_t>& oiViewId);

  /// Update TTL on all non-pending-delete records in a zone.
  /// If osFilterType is set, only records of that type are updated.
  /// Returns the number of rows affected.
  int bulkUpdateTtl(int64_t iZoneId, int iTtl,
                    const std::optional<std::string>& osFilterType);

 private:
  ConnectionPool& _cpPool;
};

}  // namespace dns::dal
