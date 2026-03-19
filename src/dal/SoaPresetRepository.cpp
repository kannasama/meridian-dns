// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "dal/SoaPresetRepository.hpp"

#include "common/Errors.hpp"
#include "dal/ConnectionPool.hpp"

#include <pqxx/pqxx>

namespace dns::dal {

static SoaPresetRow parseRow(const pqxx::row& row) {
  SoaPresetRow spr;
  spr.iId              = row[0].as<int64_t>();
  spr.sName            = row[1].as<std::string>();
  spr.sMnameTemplate   = row[2].as<std::string>();
  spr.sRnameTemplate   = row[3].as<std::string>();
  spr.iRefresh         = row[4].as<int>();
  spr.iRetry           = row[5].as<int>();
  spr.iExpire          = row[6].as<int>();
  spr.iMinimum         = row[7].as<int>();
  spr.iDefaultTtl      = row[8].as<int>();
  spr.tpCreatedAt      = std::chrono::system_clock::time_point(
      std::chrono::seconds(row[9].as<int64_t>()));
  spr.tpUpdatedAt      = std::chrono::system_clock::time_point(
      std::chrono::seconds(row[10].as<int64_t>()));
  return spr;
}

SoaPresetRepository::SoaPresetRepository(ConnectionPool& cpPool) : _cpPool(cpPool) {}
SoaPresetRepository::~SoaPresetRepository() = default;

int64_t SoaPresetRepository::create(const std::string& sName,
                                     const std::string& sMnameTemplate,
                                     const std::string& sRnameTemplate,
                                     int iRefresh, int iRetry, int iExpire,
                                     int iMinimum, int iDefaultTtl) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  try {
    auto result = txn.exec(
        "INSERT INTO soa_presets "
        "(name, mname_template, rname_template, refresh, retry, expire, minimum, default_ttl) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8) RETURNING id",
        pqxx::params{sName, sMnameTemplate, sRnameTemplate,
                     iRefresh, iRetry, iExpire, iMinimum, iDefaultTtl});
    txn.commit();
    return result.one_row()[0].as<int64_t>();
  } catch (const pqxx::unique_violation&) {
    throw common::ConflictError("SOA_PRESET_EXISTS",
                                "SOA preset '" + sName + "' already exists");
  }
}

std::vector<SoaPresetRow> SoaPresetRepository::listAll() {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT id, name, mname_template, rname_template, "
      "refresh, retry, expire, minimum, default_ttl, "
      "EXTRACT(EPOCH FROM created_at)::bigint, "
      "EXTRACT(EPOCH FROM updated_at)::bigint "
      "FROM soa_presets ORDER BY name");
  txn.commit();

  std::vector<SoaPresetRow> vRows;
  vRows.reserve(result.size());
  for (const auto& row : result) {
    vRows.push_back(parseRow(row));
  }
  return vRows;
}

std::optional<SoaPresetRow> SoaPresetRepository::findById(int64_t iId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  auto result = txn.exec(
      "SELECT id, name, mname_template, rname_template, "
      "refresh, retry, expire, minimum, default_ttl, "
      "EXTRACT(EPOCH FROM created_at)::bigint, "
      "EXTRACT(EPOCH FROM updated_at)::bigint "
      "FROM soa_presets WHERE id = $1",
      pqxx::params{iId});

  txn.commit();

  if (result.empty()) {
    return std::nullopt;
  }

  return parseRow(result[0]);
}

void SoaPresetRepository::update(int64_t iId,
                                  const std::string& sName,
                                  const std::string& sMnameTemplate,
                                  const std::string& sRnameTemplate,
                                  int iRefresh, int iRetry, int iExpire,
                                  int iMinimum, int iDefaultTtl) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  pqxx::result result;
  try {
    result = txn.exec(
        "UPDATE soa_presets SET "
        "name = $2, mname_template = $3, rname_template = $4, "
        "refresh = $5, retry = $6, expire = $7, minimum = $8, default_ttl = $9, "
        "updated_at = NOW() "
        "WHERE id = $1",
        pqxx::params{iId, sName, sMnameTemplate, sRnameTemplate,
                     iRefresh, iRetry, iExpire, iMinimum, iDefaultTtl});
    txn.commit();
  } catch (const pqxx::unique_violation&) {
    throw common::ConflictError("SOA_PRESET_EXISTS",
                                "SOA preset '" + sName + "' already exists");
  }

  if (result.affected_rows() == 0) {
    throw common::NotFoundError("SOA_PRESET_NOT_FOUND",
                                "SOA preset with id " + std::to_string(iId) + " not found");
  }
}

void SoaPresetRepository::deleteById(int64_t iId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  try {
    auto result = txn.exec("DELETE FROM soa_presets WHERE id = $1", pqxx::params{iId});
    txn.commit();

    if (result.affected_rows() == 0) {
      throw common::NotFoundError("SOA_PRESET_NOT_FOUND",
                                  "SOA preset with id " + std::to_string(iId) + " not found");
    }
  } catch (const pqxx::foreign_key_violation&) {
    throw common::ConflictError("SOA_PRESET_IN_USE",
                                "SOA preset is referenced by one or more zones or templates");
  }
}

}  // namespace dns::dal
