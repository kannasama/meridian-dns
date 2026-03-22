#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace dns::dal {

class ConnectionPool;

/// Manages the user_preferences table.
/// Class abbreviation: upr
class UserPreferenceRepository {
 public:
  explicit UserPreferenceRepository(ConnectionPool& cpPool);
  ~UserPreferenceRepository();

  /// Get all preferences for a user as key→value map.
  std::map<std::string, nlohmann::json> getAll(int64_t iUserId);

  /// Get a single preference value. Returns nullopt if not set.
  std::optional<nlohmann::json> get(int64_t iUserId, const std::string& sKey);

  /// Upsert a single preference.
  void set(int64_t iUserId, const std::string& sKey, const nlohmann::json& jValue);

  /// Batch upsert multiple preferences.
  void setAll(int64_t iUserId, const std::map<std::string, nlohmann::json>& mPrefs);

  /// Delete a single preference key.
  void deleteKey(int64_t iUserId, const std::string& sKey);

 private:
  ConnectionPool& _cpPool;
};

}  // namespace dns::dal
