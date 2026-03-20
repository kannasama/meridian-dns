// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "dal/ZoneRepository.hpp"

#include "common/Errors.hpp"
#include "dal/ConnectionPool.hpp"

#include <pqxx/pqxx>

namespace dns::dal {

static dns::dal::ZoneRow parseZoneRow(const pqxx::row& row) {
  dns::dal::ZoneRow zr;
  zr.iId    = row[0].as<int64_t>();
  zr.sName  = row[1].as<std::string>();
  zr.iViewId = row[2].as<int64_t>();
  if (!row[3].is_null()) zr.oDeploymentRetention = row[3].as<int>();
  zr.bManageSoa = row[4].as<bool>();
  zr.bManageNs  = row[5].as<bool>();
  zr.tpCreatedAt = std::chrono::system_clock::time_point(
      std::chrono::seconds(row[6].as<int64_t>()));
  zr.sSyncStatus = row[7].as<std::string>();
  if (!row[8].is_null()) {
    zr.oSyncCheckedAt = std::chrono::system_clock::time_point(
        std::chrono::seconds(row[8].as<int64_t>()));
  }
  if (!row[9].is_null())  zr.oGitRepoId  = row[9].as<int64_t>();
  if (!row[10].is_null()) zr.oGitBranch  = row[10].as<std::string>();
  if (!row[11].is_null()) zr.oTemplateId = row[11].as<int64_t>();
  zr.bTemplateCheckPending = row[12].as<bool>();
  if (!row[13].is_null()) zr.oSoaPresetId = row[13].as<int64_t>();
  if (!row[14].is_null()) {
    pqxx::array_parser ap(row[14].as<std::string>());
    for (;;) {
      auto [j, v] = ap.get_next();
      if (j != pqxx::array_parser::juncture::string_value) break;
      zr.vTags.push_back(v);
    }
  }
  return zr;
}

ZoneRepository::ZoneRepository(ConnectionPool& cpPool) : _cpPool(cpPool) {}
ZoneRepository::~ZoneRepository() = default;

int64_t ZoneRepository::create(const std::string& sName, int64_t iViewId,
                               std::optional<int> oRetention,
                               bool bManageSoa, bool bManageNs,
                               std::optional<int64_t> oGitRepoId,
                               std::optional<std::string> oGitBranch,
                               std::optional<int64_t> oSoaPresetId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  try {
    pqxx::result result;
    if (oRetention.has_value()) {
      result = txn.exec(
          "INSERT INTO zones (name, view_id, deployment_retention, manage_soa, manage_ns, "
          "git_repo_id, git_branch, soa_preset_id) "
          "VALUES ($1, $2, $3, $4, $5, $6, $7, $8) RETURNING id",
          pqxx::params{sName, iViewId, *oRetention, bManageSoa, bManageNs,
                       oGitRepoId, oGitBranch, oSoaPresetId});
    } else {
      result = txn.exec(
          "INSERT INTO zones (name, view_id, manage_soa, manage_ns, "
          "git_repo_id, git_branch, soa_preset_id) "
          "VALUES ($1, $2, $3, $4, $5, $6, $7) RETURNING id",
          pqxx::params{sName, iViewId, bManageSoa, bManageNs,
                       oGitRepoId, oGitBranch, oSoaPresetId});
    }
    txn.commit();
    return result.one_row()[0].as<int64_t>();
  } catch (const pqxx::unique_violation&) {
    throw common::ConflictError("ZONE_EXISTS",
                                "Zone '" + sName + "' already exists in this view");
  } catch (const pqxx::foreign_key_violation&) {
    throw common::ValidationError("INVALID_VIEW_ID",
                                  "View with id " + std::to_string(iViewId) + " not found");
  }
}

std::vector<ZoneRow> ZoneRepository::listAll() {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT id, name, view_id, deployment_retention, manage_soa, manage_ns, "
      "EXTRACT(EPOCH FROM created_at)::bigint, "
      "sync_status, EXTRACT(EPOCH FROM sync_checked_at)::bigint, "
      "git_repo_id, git_branch, "
      "template_id, template_check_pending, soa_preset_id, tags::text "
      "FROM zones ORDER BY id");
  txn.commit();

  std::vector<ZoneRow> vRows;
  vRows.reserve(result.size());
  for (const auto& row : result) {
    vRows.push_back(parseZoneRow(row));
  }
  return vRows;
}

std::vector<ZoneRow> ZoneRepository::listByViewId(int64_t iViewId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT id, name, view_id, deployment_retention, manage_soa, manage_ns, "
      "EXTRACT(EPOCH FROM created_at)::bigint, "
      "sync_status, EXTRACT(EPOCH FROM sync_checked_at)::bigint, "
      "git_repo_id, git_branch, "
      "template_id, template_check_pending, soa_preset_id, tags::text "
      "FROM zones WHERE view_id = $1 ORDER BY id",
      pqxx::params{iViewId});
  txn.commit();

  std::vector<ZoneRow> vRows;
  vRows.reserve(result.size());
  for (const auto& row : result) {
    vRows.push_back(parseZoneRow(row));
  }
  return vRows;
}

std::optional<ZoneRow> ZoneRepository::findById(int64_t iId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT id, name, view_id, deployment_retention, manage_soa, manage_ns, "
      "EXTRACT(EPOCH FROM created_at)::bigint, "
      "sync_status, EXTRACT(EPOCH FROM sync_checked_at)::bigint, "
      "git_repo_id, git_branch, "
      "template_id, template_check_pending, soa_preset_id, tags::text "
      "FROM zones WHERE id = $1",
      pqxx::params{iId});
  txn.commit();

  if (result.empty()) return std::nullopt;
  return parseZoneRow(result[0]);
}

void ZoneRepository::update(int64_t iId, const std::string& sName, int64_t iViewId,
                            std::optional<int> oRetention,
                            bool bManageSoa, bool bManageNs,
                            std::optional<int64_t> oGitRepoId,
                            std::optional<std::string> oGitBranch,
                            std::optional<int64_t> oSoaPresetId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  pqxx::result result;
  try {
    if (oRetention.has_value()) {
      result = txn.exec(
          "UPDATE zones SET name = $2, view_id = $3, deployment_retention = $4, "
          "manage_soa = $5, manage_ns = $6, git_repo_id = $7, git_branch = $8, "
          "soa_preset_id = $9 "
          "WHERE id = $1",
          pqxx::params{iId, sName, iViewId, *oRetention, bManageSoa, bManageNs,
                       oGitRepoId, oGitBranch, oSoaPresetId});
    } else {
      result = txn.exec(
          "UPDATE zones SET name = $2, view_id = $3, deployment_retention = NULL, "
          "manage_soa = $4, manage_ns = $5, git_repo_id = $6, git_branch = $7, "
          "soa_preset_id = $8 "
          "WHERE id = $1",
          pqxx::params{iId, sName, iViewId, bManageSoa, bManageNs,
                       oGitRepoId, oGitBranch, oSoaPresetId});
    }
    txn.commit();
  } catch (const pqxx::unique_violation&) {
    throw common::ConflictError("ZONE_EXISTS",
                                "Zone '" + sName + "' already exists in this view");
  }

  if (result.affected_rows() == 0) {
    throw common::NotFoundError("ZONE_NOT_FOUND",
                                "Zone with id " + std::to_string(iId) + " not found");
  }
}

void ZoneRepository::updateSyncStatus(int64_t iZoneId, const std::string& sSyncStatus) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  txn.exec(
      "UPDATE zones SET sync_status = $1, sync_checked_at = NOW() WHERE id = $2",
      pqxx::params{sSyncStatus, iZoneId});
  txn.commit();
}

void ZoneRepository::deleteById(int64_t iId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec("DELETE FROM zones WHERE id = $1", pqxx::params{iId});
  txn.commit();

  if (result.affected_rows() == 0) {
    throw common::NotFoundError("ZONE_NOT_FOUND",
                                "Zone with id " + std::to_string(iId) + " not found");
  }
}

void ZoneRepository::setTemplateLink(int64_t iZoneId, std::optional<int64_t> oTemplateId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  bool bPending = oTemplateId.has_value();
  txn.exec("UPDATE zones SET template_id=$1, template_check_pending=$2 WHERE id=$3",
           pqxx::params{oTemplateId, bPending, iZoneId});
  txn.commit();
}

void ZoneRepository::clearTemplateCheckPending(int64_t iZoneId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  txn.exec("UPDATE zones SET template_check_pending=FALSE WHERE id=$1",
           pqxx::params{iZoneId});
  txn.commit();
}

void ZoneRepository::updateTags(int64_t iZoneId, const std::vector<std::string>& vTags) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  std::string sArr;
  if (vTags.empty()) {
    sArr = "ARRAY[]::TEXT[]";
  } else {
    sArr = "ARRAY[";
    for (size_t i = 0; i < vTags.size(); ++i) {
      if (i > 0) sArr += ",";
      sArr += txn.quote(vTags[i]);
    }
    sArr += "]";
  }
  txn.exec("UPDATE zones SET tags=" + sArr + " WHERE id=$1", pqxx::params{iZoneId});
  txn.commit();
}

int64_t ZoneRepository::cloneZone(int64_t iSourceId, const std::string& sName,
                                   int64_t iViewId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  // Fetch source zone
  auto src = txn.exec(
      "SELECT manage_soa, manage_ns, deployment_retention "
      "FROM zones WHERE id = $1",
      pqxx::params{iSourceId});
  if (src.empty()) {
    throw common::NotFoundError("ZONE_NOT_FOUND",
                                "Zone with id " + std::to_string(iSourceId) + " not found");
  }
  bool bManageSoa = src[0][0].as<bool>();
  bool bManageNs  = src[0][1].as<bool>();
  std::optional<int> oRetention;
  if (!src[0][2].is_null()) oRetention = src[0][2].as<int>();

  // Insert new zone
  pqxx::result newZone;
  if (oRetention.has_value()) {
    newZone = txn.exec(
        "INSERT INTO zones (name, view_id, manage_soa, manage_ns, deployment_retention, "
        "  template_id, soa_preset_id, tags, template_check_pending, sync_status) "
        "VALUES ($1, $2, $3, $4, $5, NULL, NULL, '{}', false, 'unknown') RETURNING id",
        pqxx::params{sName, iViewId, bManageSoa, bManageNs, *oRetention});
  } else {
    newZone = txn.exec(
        "INSERT INTO zones (name, view_id, manage_soa, manage_ns, "
        "  template_id, soa_preset_id, tags, template_check_pending, sync_status) "
        "VALUES ($1, $2, $3, $4, NULL, NULL, '{}', false, 'unknown') RETURNING id",
        pqxx::params{sName, iViewId, bManageSoa, bManageNs});
  }
  int64_t iNewId = newZone.one_row()[0].as<int64_t>();

  // Copy all non-pending-delete records
  txn.exec(
      "INSERT INTO records "
      "  (zone_id, name, type, ttl, value_template, priority, provider_meta, "
      "   created_at, updated_at) "
      "SELECT $1, name, type, ttl, value_template, priority, provider_meta, NOW(), NOW() "
      "FROM records WHERE zone_id = $2 AND NOT pending_delete",
      pqxx::params{iNewId, iSourceId});

  txn.commit();
  return iNewId;
}

}  // namespace dns::dal
