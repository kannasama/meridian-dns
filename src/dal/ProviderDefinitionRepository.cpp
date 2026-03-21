// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "dal/ProviderDefinitionRepository.hpp"

#include "common/Errors.hpp"
#include "dal/ConnectionPool.hpp"

#include <pqxx/pqxx>

namespace dns::dal {

static constexpr const char* kSelectBase =
    "SELECT pd.id, pd.name, pd.type_slug, pd.version, pd.definition::text, "
    "COALESCE(pd.source_url, ''), "
    "COUNT(p.id) AS active_count, "
    "EXTRACT(EPOCH FROM pd.imported_at)::bigint, "
    "EXTRACT(EPOCH FROM pd.updated_at)::bigint "
    "FROM provider_definitions pd "
    "LEFT JOIN providers p ON p.definition_id = pd.id ";

ProviderDefinitionRepository::ProviderDefinitionRepository(ConnectionPool& cpPool)
    : _cpPool(cpPool) {}

ProviderDefinitionRepository::~ProviderDefinitionRepository() = default;

ProviderDefinitionRow ProviderDefinitionRepository::mapRow(
    const std::string& sId, const std::string& sName, const std::string& sTypeSlug,
    const std::string& sVersion, const std::string& sDefinitionStr,
    const std::string& sSourceUrl, int64_t iActiveCount,
    int64_t iImportedEpoch, int64_t iUpdatedEpoch) const {
  ProviderDefinitionRow row;
  row.iId        = std::stoll(sId);
  row.sName      = sName;
  row.sTypeSlug  = sTypeSlug;
  row.sVersion   = sVersion;
  row.sSourceUrl = sSourceUrl;
  row.iActiveInstanceCount = iActiveCount;
  row.tpImportedAt = std::chrono::system_clock::time_point(
      std::chrono::seconds(iImportedEpoch));
  row.tpUpdatedAt = std::chrono::system_clock::time_point(
      std::chrono::seconds(iUpdatedEpoch));

  auto jParsed = nlohmann::json::parse(sDefinitionStr, nullptr, false);
  row.jDefinition = jParsed.is_discarded() ? nlohmann::json::object() : jParsed;

  return row;
}

int64_t ProviderDefinitionRepository::create(const std::string& sName,
                                              const std::string& sTypeSlug,
                                              const std::string& sVersion,
                                              const nlohmann::json& jDefinition,
                                              const std::string& sSourceUrl) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  try {
    auto result = txn.exec(
        "INSERT INTO provider_definitions (name, type_slug, version, definition, source_url) "
        "VALUES ($1, $2, $3, $4::jsonb, NULLIF($5, '')) RETURNING id",
        pqxx::params{sName, sTypeSlug, sVersion, jDefinition.dump(), sSourceUrl});
    txn.commit();
    return result.one_row()[0].as<int64_t>();
  } catch (const pqxx::unique_violation&) {
    throw common::ConflictError("DEFINITION_EXISTS",
                                "Provider definition with type_slug '" + sTypeSlug +
                                "' already exists");
  }
}

std::vector<ProviderDefinitionRow> ProviderDefinitionRepository::listAll() {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  auto result = txn.exec(
      std::string(kSelectBase) + "GROUP BY pd.id ORDER BY pd.name");
  txn.commit();

  std::vector<ProviderDefinitionRow> vRows;
  vRows.reserve(result.size());
  for (const auto& row : result) {
    vRows.push_back(mapRow(
        row[0].as<std::string>(),
        row[1].as<std::string>(),
        row[2].as<std::string>(),
        row[3].as<std::string>(),
        row[4].as<std::string>(),
        row[5].as<std::string>(),
        row[6].as<int64_t>(),
        row[7].as<int64_t>(),
        row[8].as<int64_t>()));
  }
  return vRows;
}

std::optional<ProviderDefinitionRow> ProviderDefinitionRepository::findById(int64_t iId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  auto result = txn.exec(
      std::string(kSelectBase) +
      "WHERE pd.id = $1 GROUP BY pd.id ORDER BY pd.name",
      pqxx::params{iId});

  if (result.empty()) {
    txn.commit();
    return std::nullopt;
  }

  auto row = mapRow(
      result[0][0].as<std::string>(),
      result[0][1].as<std::string>(),
      result[0][2].as<std::string>(),
      result[0][3].as<std::string>(),
      result[0][4].as<std::string>(),
      result[0][5].as<std::string>(),
      result[0][6].as<int64_t>(),
      result[0][7].as<int64_t>(),
      result[0][8].as<int64_t>());
  txn.commit();
  return row;
}

std::optional<ProviderDefinitionRow> ProviderDefinitionRepository::findByTypeSlug(
    const std::string& sTypeSlug) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  auto result = txn.exec(
      std::string(kSelectBase) +
      "WHERE pd.type_slug = $1 GROUP BY pd.id ORDER BY pd.name",
      pqxx::params{sTypeSlug});

  if (result.empty()) {
    txn.commit();
    return std::nullopt;
  }

  auto row = mapRow(
      result[0][0].as<std::string>(),
      result[0][1].as<std::string>(),
      result[0][2].as<std::string>(),
      result[0][3].as<std::string>(),
      result[0][4].as<std::string>(),
      result[0][5].as<std::string>(),
      result[0][6].as<int64_t>(),
      result[0][7].as<int64_t>(),
      result[0][8].as<int64_t>());
  txn.commit();
  return row;
}

void ProviderDefinitionRepository::update(int64_t iId, const std::string& sName,
                                           const std::string& sVersion,
                                           const nlohmann::json& jDefinition,
                                           const std::string& sSourceUrl) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  auto result = txn.exec(
      "UPDATE provider_definitions "
      "SET name = $2, version = $3, definition = $4::jsonb, "
      "source_url = NULLIF($5, ''), updated_at = NOW() "
      "WHERE id = $1",
      pqxx::params{iId, sName, sVersion, jDefinition.dump(), sSourceUrl});
  txn.commit();

  if (result.affected_rows() == 0) {
    throw common::NotFoundError("DEFINITION_NOT_FOUND",
                                "Provider definition with id " + std::to_string(iId) +
                                " not found");
  }
}

void ProviderDefinitionRepository::deleteById(int64_t iId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  try {
    auto result = txn.exec(
        "DELETE FROM provider_definitions WHERE id = $1", pqxx::params{iId});
    txn.commit();

    if (result.affected_rows() == 0) {
      throw common::NotFoundError("DEFINITION_NOT_FOUND",
                                  "Provider definition with id " + std::to_string(iId) +
                                  " not found");
    }
  } catch (const pqxx::foreign_key_violation&) {
    throw common::ConflictError("DEFINITION_IN_USE",
                                "Provider definition with id " + std::to_string(iId) +
                                " is referenced by one or more provider instances");
  }
}

}  // namespace dns::dal
