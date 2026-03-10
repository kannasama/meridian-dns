#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dns::dal {

class ConnectionPool;

/// Row type returned from system_config queries.
struct SettingRow {
  std::string sKey;
  std::string sValue;
  std::string sDescription;
  std::string sUpdatedAt;  // ISO 8601 timestamp string
};

/// Manages the system_config table for DB-backed settings.
/// Class abbreviation: sr (settings repo)
class SettingsRepository {
 public:
  explicit SettingsRepository(ConnectionPool& cpPool);
  ~SettingsRepository();

  /// Get a single setting by key. Returns nullopt if not found.
  std::optional<SettingRow> findByKey(const std::string& sKey);

  /// Get the value for a key, or return sDefault if not found.
  std::string getValue(const std::string& sKey, const std::string& sDefault = "");

  /// Get value as int, or return iDefault if not found or unparseable.
  int getInt(const std::string& sKey, int iDefault);

  /// Get value as bool (true/false/1/0), or return bDefault if not found.
  bool getBool(const std::string& sKey, bool bDefault);

  /// List all settings.
  std::vector<SettingRow> listAll();

  /// Insert or update a setting. Sets updated_at to now().
  void upsert(const std::string& sKey, const std::string& sValue,
              const std::string& sDescription = "");

  /// Insert a setting only if the key does not exist (seed behavior).
  /// Returns true if inserted, false if already existed.
  bool seedIfMissing(const std::string& sKey, const std::string& sValue,
                     const std::string& sDescription = "");

  /// Delete a setting by key.
  void deleteByKey(const std::string& sKey);

 private:
  ConnectionPool& _cpPool;

  SettingRow mapRow(const auto& row) const;
};

}  // namespace dns::dal
