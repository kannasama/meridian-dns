// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "dal/UserPreferenceRepository.hpp"

#include "dal/ConnectionPool.hpp"

#include <pqxx/pqxx>

namespace dns::dal {

UserPreferenceRepository::UserPreferenceRepository(ConnectionPool& cpPool)
    : _cpPool(cpPool) {}
UserPreferenceRepository::~UserPreferenceRepository() = default;

std::map<std::string, nlohmann::json> UserPreferenceRepository::getAll(int64_t iUserId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT key, value FROM user_preferences WHERE user_id = $1",
      pqxx::params{iUserId});
  txn.commit();

  std::map<std::string, nlohmann::json> mPrefs;
  for (const auto& row : result) {
    mPrefs[row[0].as<std::string>()] = nlohmann::json::parse(row[1].as<std::string>());
  }
  return mPrefs;
}

std::optional<nlohmann::json> UserPreferenceRepository::get(
    int64_t iUserId, const std::string& sKey) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT value FROM user_preferences WHERE user_id = $1 AND key = $2",
      pqxx::params{iUserId, sKey});
  txn.commit();

  if (result.empty()) return std::nullopt;
  return nlohmann::json::parse(result[0][0].as<std::string>());
}

void UserPreferenceRepository::set(
    int64_t iUserId, const std::string& sKey, const nlohmann::json& jValue) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  txn.exec(
      "INSERT INTO user_preferences (user_id, key, value) VALUES ($1, $2, $3::jsonb) "
      "ON CONFLICT (user_id, key) DO UPDATE SET value = EXCLUDED.value",
      pqxx::params{iUserId, sKey, jValue.dump()});
  txn.commit();
}

void UserPreferenceRepository::setAll(
    int64_t iUserId, const std::map<std::string, nlohmann::json>& mPrefs) {
  if (mPrefs.empty()) return;
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  for (const auto& [sKey, jValue] : mPrefs) {
    txn.exec(
        "INSERT INTO user_preferences (user_id, key, value) VALUES ($1, $2, $3::jsonb) "
        "ON CONFLICT (user_id, key) DO UPDATE SET value = EXCLUDED.value",
        pqxx::params{iUserId, sKey, jValue.dump()});
  }
  txn.commit();
}

void UserPreferenceRepository::deleteKey(int64_t iUserId, const std::string& sKey) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  txn.exec(
      "DELETE FROM user_preferences WHERE user_id = $1 AND key = $2",
      pqxx::params{iUserId, sKey});
  txn.commit();
}

}  // namespace dns::dal
