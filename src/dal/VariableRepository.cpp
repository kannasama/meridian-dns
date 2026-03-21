// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "dal/VariableRepository.hpp"

#include "common/Errors.hpp"
#include "dal/ConnectionPool.hpp"

#include <pqxx/pqxx>

namespace dns::dal {

VariableRepository::VariableRepository(ConnectionPool& cpPool) : _cpPool(cpPool) {}
VariableRepository::~VariableRepository() = default;

int64_t VariableRepository::create(const std::string& sName, const std::string& sValue,
                                   const std::string& sType, const std::string& sScope,
                                   std::optional<int64_t> oZoneId,
                                   const std::string& sVariableKind,
                                   std::optional<std::string> osDynamicFormat) {
  // Validate scope/zone_id consistency before touching the database.
  if (sScope == "global" && oZoneId.has_value()) {
    throw common::ValidationError("SCOPE_ZONE_MISMATCH",
                                  "scope='global' requires zone_id to be NULL");
  }
  if (sScope == "zone" && !oZoneId.has_value()) {
    throw common::ValidationError("SCOPE_ZONE_MISMATCH",
                                  "scope='zone' requires a zone_id");
  }

  // PostgreSQL UNIQUE(name, zone_id) treats NULL as distinct, so enforce
  // global uniqueness in application logic.
  if (sScope == "global") {
    auto cg = _cpPool.checkout();
    pqxx::work txn(*cg);
    auto existing = txn.exec(
        "SELECT id FROM variables WHERE name = $1 AND zone_id IS NULL",
        pqxx::params{sName});
    if (!existing.empty()) {
      txn.commit();
      throw common::ConflictError("VARIABLE_EXISTS",
                                  "Global variable '" + sName + "' already exists");
    }

    try {
      pqxx::result result;
      if (osDynamicFormat.has_value()) {
        result = txn.exec(
            "INSERT INTO variables (name, value, type, scope, variable_kind, dynamic_format) "
            "VALUES ($1, $2, $3::variable_type, $4::variable_scope, "
            "$5::variable_kind, $6) RETURNING id",
            pqxx::params{sName, sValue, sType, sScope, sVariableKind, *osDynamicFormat});
      } else {
        result = txn.exec(
            "INSERT INTO variables (name, value, type, scope, variable_kind) "
            "VALUES ($1, $2, $3::variable_type, $4::variable_scope, "
            "$5::variable_kind) RETURNING id",
            pqxx::params{sName, sValue, sType, sScope, sVariableKind});
      }
      txn.commit();
      return result.one_row()[0].as<int64_t>();
    } catch (const pqxx::check_violation&) {
      throw common::ValidationError("SCOPE_ZONE_MISMATCH",
                                    "scope='global' requires zone_id to be NULL");
    }
  }

  // Zone-scoped variable
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  try {
    pqxx::result result;
    if (osDynamicFormat.has_value()) {
      result = txn.exec(
          "INSERT INTO variables (name, value, type, scope, zone_id, variable_kind, "
          "dynamic_format) "
          "VALUES ($1, $2, $3::variable_type, $4::variable_scope, $5, "
          "$6::variable_kind, $7) RETURNING id",
          pqxx::params{sName, sValue, sType, sScope, *oZoneId, sVariableKind,
                       *osDynamicFormat});
    } else {
      result = txn.exec(
          "INSERT INTO variables (name, value, type, scope, zone_id, variable_kind) "
          "VALUES ($1, $2, $3::variable_type, $4::variable_scope, $5, "
          "$6::variable_kind) RETURNING id",
          pqxx::params{sName, sValue, sType, sScope, *oZoneId, sVariableKind});
    }
    txn.commit();
    return result.one_row()[0].as<int64_t>();
  } catch (const pqxx::unique_violation&) {
    throw common::ConflictError("VARIABLE_EXISTS",
                                "Variable '" + sName + "' already exists in this zone");
  } catch (const pqxx::check_violation&) {
    throw common::ValidationError("SCOPE_ZONE_MISMATCH",
                                  "scope/zone_id mismatch: scope='zone' requires zone_id");
  } catch (const pqxx::foreign_key_violation&) {
    throw common::ValidationError("INVALID_ZONE_ID",
                                  "Zone with id " + std::to_string(*oZoneId) + " not found");
  }
}

namespace {

VariableRow mapVariableRow(const pqxx::row& row) {
  VariableRow vr;
  vr.iId = row[0].as<int64_t>();
  vr.sName = row[1].as<std::string>();
  vr.sValue = row[2].as<std::string>();
  vr.sType = row[3].as<std::string>();
  vr.sScope = row[4].as<std::string>();
  if (!row[5].is_null()) vr.oZoneId = row[5].as<int64_t>();
  vr.sVariableKind = row[6].as<std::string>();
  if (!row[7].is_null()) vr.osDynamicFormat = row[7].as<std::string>();
  vr.tpCreatedAt = std::chrono::system_clock::time_point(
      std::chrono::seconds(row[8].as<int64_t>()));
  vr.tpUpdatedAt = std::chrono::system_clock::time_point(
      std::chrono::seconds(row[9].as<int64_t>()));
  return vr;
}

const char* kSelectVariables =
    "SELECT id, name, value, type::text, scope::text, zone_id, "
    "variable_kind::text, dynamic_format, "
    "EXTRACT(EPOCH FROM created_at)::bigint, "
    "EXTRACT(EPOCH FROM updated_at)::bigint "
    "FROM variables";

}  // namespace

std::vector<VariableRow> VariableRepository::listAll() {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      std::string(kSelectVariables) +
          " ORDER BY CASE WHEN scope = 'global' THEN 0 ELSE 1 END, name");
  txn.commit();

  std::vector<VariableRow> vRows;
  vRows.reserve(result.size());
  for (const auto& row : result) {
    vRows.push_back(mapVariableRow(row));
  }
  return vRows;
}

std::vector<VariableRow> VariableRepository::listByScope(const std::string& sScope) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      std::string(kSelectVariables) + " WHERE scope = $1::variable_scope ORDER BY name",
      pqxx::params{sScope});
  txn.commit();

  std::vector<VariableRow> vRows;
  vRows.reserve(result.size());
  for (const auto& row : result) {
    vRows.push_back(mapVariableRow(row));
  }
  return vRows;
}

std::vector<VariableRow> VariableRepository::listByZoneId(int64_t iZoneId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      std::string(kSelectVariables) +
          " WHERE zone_id = $1 OR scope = 'global'"
          " ORDER BY CASE WHEN scope = 'global' THEN 0 ELSE 1 END, name",
      pqxx::params{iZoneId});
  txn.commit();

  std::vector<VariableRow> vRows;
  vRows.reserve(result.size());
  for (const auto& row : result) {
    vRows.push_back(mapVariableRow(row));
  }
  return vRows;
}

std::optional<VariableRow> VariableRepository::findById(int64_t iId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      std::string(kSelectVariables) + " WHERE id = $1",
      pqxx::params{iId});
  txn.commit();

  if (result.empty()) return std::nullopt;
  return mapVariableRow(result[0]);
}

void VariableRepository::update(int64_t iId, const std::string& sValue,
                                std::optional<std::string> osDynamicFormat) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  pqxx::result result;
  if (osDynamicFormat.has_value()) {
    result = txn.exec(
        "UPDATE variables SET value = $2, dynamic_format = $3, updated_at = NOW() "
        "WHERE id = $1",
        pqxx::params{iId, sValue, *osDynamicFormat});
  } else {
    result = txn.exec(
        "UPDATE variables SET value = $2, dynamic_format = NULL, updated_at = NOW() "
        "WHERE id = $1",
        pqxx::params{iId, sValue});
  }
  txn.commit();

  if (result.affected_rows() == 0) {
    throw common::NotFoundError("VARIABLE_NOT_FOUND",
                                "Variable with id " + std::to_string(iId) + " not found");
  }
}

std::vector<VariableRow> VariableRepository::listDynamic() {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      std::string(kSelectVariables) + " WHERE variable_kind = 'dynamic' ORDER BY name");
  txn.commit();

  std::vector<VariableRow> vRows;
  vRows.reserve(result.size());
  for (const auto& row : result) {
    vRows.push_back(mapVariableRow(row));
  }
  return vRows;
}

void VariableRepository::deleteById(int64_t iId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec("DELETE FROM variables WHERE id = $1", pqxx::params{iId});
  txn.commit();

  if (result.affected_rows() == 0) {
    throw common::NotFoundError("VARIABLE_NOT_FOUND",
                                "Variable with id " + std::to_string(iId) + " not found");
  }
}

}  // namespace dns::dal
