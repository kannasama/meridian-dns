#include "dal/SettingsRepository.hpp"

#include "dal/ConnectionPool.hpp"

#include <pqxx/pqxx>

namespace dns::dal {

SettingsRepository::SettingsRepository(ConnectionPool& cpPool) : _cpPool(cpPool) {}
SettingsRepository::~SettingsRepository() = default;

SettingRow SettingsRepository::mapRow(const auto& row) const {
  SettingRow sr;
  sr.sKey = row[0].template as<std::string>();
  sr.sValue = row[1].template as<std::string>();
  sr.sDescription = row[2].is_null() ? "" : row[2].template as<std::string>();
  sr.sUpdatedAt = row[3].is_null() ? "" : row[3].template as<std::string>();
  return sr;
}

std::optional<SettingRow> SettingsRepository::findByKey(const std::string& sKey) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT key, value, description, updated_at FROM system_config WHERE key = $1",
      pqxx::params{sKey});
  txn.commit();
  if (result.empty()) {
    return std::nullopt;
  }
  return mapRow(result[0]);
}

std::string SettingsRepository::getValue(const std::string& sKey, const std::string& sDefault) {
  auto oRow = findByKey(sKey);
  return oRow ? oRow->sValue : sDefault;
}

int SettingsRepository::getInt(const std::string& sKey, int iDefault) {
  auto oRow = findByKey(sKey);
  if (!oRow) return iDefault;
  try {
    return std::stoi(oRow->sValue);
  } catch (...) {
    return iDefault;
  }
}

bool SettingsRepository::getBool(const std::string& sKey, bool bDefault) {
  auto oRow = findByKey(sKey);
  if (!oRow) return bDefault;
  return oRow->sValue == "true" || oRow->sValue == "1";
}

std::vector<SettingRow> SettingsRepository::listAll() {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT key, value, description, updated_at FROM system_config ORDER BY key");
  txn.commit();

  std::vector<SettingRow> vRows;
  vRows.reserve(result.size());
  for (const auto& row : result) {
    vRows.push_back(mapRow(row));
  }
  return vRows;
}

void SettingsRepository::upsert(const std::string& sKey, const std::string& sValue,
                                const std::string& sDescription) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  txn.exec(
      "INSERT INTO system_config (key, value, description, updated_at) "
      "VALUES ($1, $2, $3, now()) "
      "ON CONFLICT (key) DO UPDATE SET value = $2, description = $3, updated_at = now()",
      pqxx::params{sKey, sValue,
                   sDescription.empty() ? std::optional<std::string>{} : sDescription});
  txn.commit();
}

bool SettingsRepository::seedIfMissing(const std::string& sKey, const std::string& sValue,
                                       const std::string& sDescription) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "INSERT INTO system_config (key, value, description, updated_at) "
      "VALUES ($1, $2, $3, now()) "
      "ON CONFLICT (key) DO NOTHING",
      pqxx::params{sKey, sValue,
                   sDescription.empty() ? std::optional<std::string>{} : sDescription});
  txn.commit();
  return result.affected_rows() > 0;
}

void SettingsRepository::deleteByKey(const std::string& sKey) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  txn.exec("DELETE FROM system_config WHERE key = $1", pqxx::params{sKey});
  txn.commit();
}

}  // namespace dns::dal
