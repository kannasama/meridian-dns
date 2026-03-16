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

/// Row type returned from API key queries.
struct ApiKeyRow {
  int64_t iId = 0;
  int64_t iUserId = 0;
  std::string sKeyHash;
  std::string sDescription;
  std::string sKeyPrefix;
  bool bRevoked = false;
  std::optional<std::chrono::system_clock::time_point> oExpiresAt;
  std::chrono::system_clock::time_point tpCreatedAt;
  std::optional<std::chrono::system_clock::time_point> oLastUsedAt;
};

/// Manages the api_keys table; create, findByHash, scheduleDelete, pruneScheduled.
/// Class abbreviation: akr
class ApiKeyRepository {
 public:
  explicit ApiKeyRepository(ConnectionPool& cpPool);
  ~ApiKeyRepository();

  /// Create a new API key row. Returns the row ID.
  int64_t create(int64_t iUserId, const std::string& sKeyHash,
                 const std::string& sDescription,
                 std::optional<std::chrono::system_clock::time_point> oExpiresAt);

  /// Find an API key by its SHA-512 hash. Returns nullopt if not found.
  std::optional<ApiKeyRow> findByHash(const std::string& sKeyHash);

  /// Mark a key for deferred deletion: set delete_after = NOW() + grace seconds.
  void scheduleDelete(int64_t iKeyId, int iGraceSeconds);

  /// Delete all API key rows where delete_after < NOW(). Returns rows deleted.
  int pruneScheduled();

  /// List all API keys for a specific user.
  std::vector<ApiKeyRow> listByUser(int64_t iUserId);

  /// List all API keys.
  std::vector<ApiKeyRow> listAll();

 private:
  ConnectionPool& _cpPool;
};

}  // namespace dns::dal
