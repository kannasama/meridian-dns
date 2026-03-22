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

/// Row type returned from system_logs queries.
struct SystemLogRow {
  int64_t iId = 0;
  std::string sCategory;
  std::string sSeverity;
  std::optional<int64_t> oZoneId;
  std::optional<int64_t> oProviderId;
  std::optional<std::string> osOperation;
  std::optional<std::string> osRecordName;
  std::optional<std::string> osRecordType;
  std::optional<bool> obSuccess;
  std::optional<int> oiStatusCode;
  std::string sMessage;
  std::optional<std::string> osDetail;
  std::chrono::system_clock::time_point tpCreatedAt;
};

/// Manages the system_logs table; insert, query, purge.
/// Class abbreviation: slr
class SystemLogRepository {
 public:
  explicit SystemLogRepository(ConnectionPool& cpPool);
  ~SystemLogRepository();

  /// Insert a system log entry. Returns the new ID.
  int64_t insert(const std::string& sCategory,
                 const std::string& sSeverity,
                 const std::string& sMessage,
                 std::optional<int64_t> oZoneId = std::nullopt,
                 std::optional<int64_t> oProviderId = std::nullopt,
                 const std::optional<std::string>& osOperation = std::nullopt,
                 const std::optional<std::string>& osRecordName = std::nullopt,
                 const std::optional<std::string>& osRecordType = std::nullopt,
                 std::optional<bool> obSuccess = std::nullopt,
                 std::optional<int> oiStatusCode = std::nullopt,
                 const std::optional<std::string>& osDetail = std::nullopt);

  /// Query system logs with optional filters. Orders by created_at DESC.
  std::vector<SystemLogRow> query(
      const std::optional<std::string>& osCategory = std::nullopt,
      const std::optional<std::string>& osSeverity = std::nullopt,
      const std::optional<int64_t>& oZoneId = std::nullopt,
      const std::optional<int64_t>& oProviderId = std::nullopt,
      const std::optional<std::chrono::system_clock::time_point>& otpFrom = std::nullopt,
      const std::optional<std::chrono::system_clock::time_point>& otpTo = std::nullopt,
      int iLimit = 200);

  /// Delete entries older than iRetentionDays. Returns count deleted.
  int64_t purge(int iRetentionDays);

 private:
  ConnectionPool& _cpPool;
};

}  // namespace dns::dal
