// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "dal/ZoneTemplateRepository.hpp"

#include "common/Errors.hpp"
#include "dal/ConnectionPool.hpp"

#include <pqxx/pqxx>

namespace dns::dal {

static std::vector<int64_t> fetchSnippetIds(pqxx::work& txn, int64_t iTemplateId) {
  auto recs = txn.exec(
      "SELECT snippet_id FROM zone_template_snippets "
      "WHERE template_id = $1 ORDER BY sort_order",
      pqxx::params{iTemplateId});

  std::vector<int64_t> vIds;
  vIds.reserve(recs.size());
  for (const auto& row : recs) {
    vIds.push_back(row[0].as<int64_t>());
  }
  return vIds;
}

static ZoneTemplateRow parseRow(const pqxx::row& row) {
  ZoneTemplateRow ztr;
  ztr.iId          = row[0].as<int64_t>();
  ztr.sName        = row[1].as<std::string>();
  ztr.sDescription = row[2].as<std::string>();
  if (!row[3].is_null()) {
    ztr.oSoaPresetId = row[3].as<int64_t>();
  }
  ztr.tpCreatedAt = std::chrono::system_clock::time_point(
      std::chrono::seconds(row[4].as<int64_t>()));
  ztr.tpUpdatedAt = std::chrono::system_clock::time_point(
      std::chrono::seconds(row[5].as<int64_t>()));
  return ztr;
}

ZoneTemplateRepository::ZoneTemplateRepository(ConnectionPool& cpPool) : _cpPool(cpPool) {}
ZoneTemplateRepository::~ZoneTemplateRepository() = default;

int64_t ZoneTemplateRepository::create(const std::string& sName,
                                        const std::string& sDescription,
                                        std::optional<int64_t> oSoaPresetId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  try {
    auto result = txn.exec(
        "INSERT INTO zone_templates (name, description, soa_preset_id) "
        "VALUES ($1, $2, $3) RETURNING id",
        pqxx::params{sName, sDescription, oSoaPresetId});
    txn.commit();
    return result.one_row()[0].as<int64_t>();
  } catch (const pqxx::unique_violation&) {
    throw common::ConflictError("TEMPLATE_EXISTS",
                                "Zone template '" + sName + "' already exists");
  }
}

std::vector<ZoneTemplateRow> ZoneTemplateRepository::listAll() {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT id, name, description, soa_preset_id, "
      "EXTRACT(EPOCH FROM created_at)::bigint, "
      "EXTRACT(EPOCH FROM updated_at)::bigint "
      "FROM zone_templates ORDER BY name");
  txn.commit();

  std::vector<ZoneTemplateRow> vRows;
  vRows.reserve(result.size());
  for (const auto& row : result) {
    vRows.push_back(parseRow(row));
    // vSnippetIds intentionally left empty for list views
  }
  return vRows;
}

std::optional<ZoneTemplateRow> ZoneTemplateRepository::findById(int64_t iId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  auto result = txn.exec(
      "SELECT id, name, description, soa_preset_id, "
      "EXTRACT(EPOCH FROM created_at)::bigint, "
      "EXTRACT(EPOCH FROM updated_at)::bigint "
      "FROM zone_templates WHERE id = $1",
      pqxx::params{iId});

  if (result.empty()) {
    txn.commit();
    return std::nullopt;
  }

  ZoneTemplateRow ztr = parseRow(result[0]);
  ztr.vSnippetIds = fetchSnippetIds(txn, iId);
  txn.commit();

  return ztr;
}

void ZoneTemplateRepository::update(int64_t iId, const std::string& sName,
                                     const std::string& sDescription,
                                     std::optional<int64_t> oSoaPresetId) {
  {
    auto cg = _cpPool.checkout();
    pqxx::work txn(*cg);

    pqxx::result result;
    try {
      result = txn.exec(
          "UPDATE zone_templates SET name = $2, description = $3, soa_preset_id = $4, "
          "updated_at = NOW() WHERE id = $1",
          pqxx::params{iId, sName, sDescription, oSoaPresetId});
      txn.commit();
    } catch (const pqxx::unique_violation&) {
      throw common::ConflictError("TEMPLATE_EXISTS",
                                  "Zone template '" + sName + "' already exists");
    }

    if (result.affected_rows() == 0) {
      throw common::NotFoundError("TEMPLATE_NOT_FOUND",
                                  "Zone template with id " + std::to_string(iId) + " not found");
    }
  }

  flagLinkedZones(iId);
}

void ZoneTemplateRepository::deleteById(int64_t iId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  try {
    auto result = txn.exec("DELETE FROM zone_templates WHERE id = $1", pqxx::params{iId});
    txn.commit();

    if (result.affected_rows() == 0) {
      throw common::NotFoundError("TEMPLATE_NOT_FOUND",
                                  "Zone template with id " + std::to_string(iId) + " not found");
    }
  } catch (const pqxx::foreign_key_violation&) {
    throw common::ConflictError("TEMPLATE_IN_USE",
                                "Template is linked to one or more zones");
  }
}

void ZoneTemplateRepository::setSnippets(int64_t iTemplateId,
                                          const std::vector<int64_t>& vSnippetIds) {
  {
    auto cg = _cpPool.checkout();
    pqxx::work txn(*cg);

    txn.exec("DELETE FROM zone_template_snippets WHERE template_id = $1",
             pqxx::params{iTemplateId});

    for (std::size_t i = 0; i < vSnippetIds.size(); ++i) {
      txn.exec(
          "INSERT INTO zone_template_snippets (template_id, snippet_id, sort_order) "
          "VALUES ($1, $2, $3)",
          pqxx::params{iTemplateId, vSnippetIds[i], static_cast<int>(i)});
    }

    txn.commit();
  }

  flagLinkedZones(iTemplateId);
}

void ZoneTemplateRepository::flagLinkedZones(int64_t iTemplateId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  txn.exec("UPDATE zones SET template_check_pending = TRUE WHERE template_id = $1",
           pqxx::params{iTemplateId});
  txn.commit();
}

}  // namespace dns::dal
