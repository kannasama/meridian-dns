// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "dal/TagRepository.hpp"

#include "common/Errors.hpp"
#include "dal/ConnectionPool.hpp"

#include <pqxx/pqxx>

namespace dns::dal {

TagRepository::TagRepository(ConnectionPool& cpPool) : _cpPool(cpPool) {}
TagRepository::~TagRepository() = default;

std::vector<TagRow> TagRepository::listWithCounts() {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT t.id, t.name, EXTRACT(EPOCH FROM t.created_at)::bigint, "
      "  COUNT(z.id) AS zone_count "
      "FROM tags t "
      "LEFT JOIN zones z ON t.name = ANY(z.tags) "
      "GROUP BY t.id, t.name, t.created_at "
      "ORDER BY t.name");
  txn.commit();

  std::vector<TagRow> vRows;
  vRows.reserve(result.size());
  for (const auto& row : result) {
    TagRow tr;
    tr.iId       = row[0].as<int64_t>();
    tr.sName     = row[1].as<std::string>();
    tr.tpCreatedAt = std::chrono::system_clock::time_point(
        std::chrono::seconds(row[2].as<int64_t>()));
    tr.iZoneCount = row[3].as<int64_t>();
    vRows.push_back(std::move(tr));
  }
  return vRows;
}

void TagRepository::upsertVocabulary(const std::vector<std::string>& vTags) {
  if (vTags.empty()) return;
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  // Build a PostgreSQL ARRAY[] literal using txn.quote() for proper escaping
  std::string sArray = "ARRAY[";
  for (size_t i = 0; i < vTags.size(); ++i) {
    if (i > 0) sArray += ",";
    sArray += txn.quote(vTags[i]);
  }
  sArray += "]";

  txn.exec(
      "INSERT INTO tags (name, created_at) "
      "SELECT unnest(" + sArray + "::text[]), NOW() "
      "ON CONFLICT (name) DO NOTHING");
  txn.commit();
}

void TagRepository::rename(int64_t iId, const std::string& sNewName) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  auto res = txn.exec("SELECT name FROM tags WHERE id = $1", pqxx::params{iId});
  if (res.empty()) throw dns::common::NotFoundError("TAG_NOT_FOUND", "Tag not found");
  std::string sOldName = res[0][0].as<std::string>();

  // Cascade rename into all zones that carry the old tag
  txn.exec(
      "UPDATE zones SET tags = array_replace(tags, $1, $2) WHERE $1 = ANY(tags)",
      pqxx::params{sOldName, sNewName});

  try {
    txn.exec("UPDATE tags SET name = $1 WHERE id = $2",
             pqxx::params{sNewName, iId});
  } catch (const pqxx::unique_violation&) {
    throw dns::common::ConflictError("TAG_NAME_EXISTS", "Tag name already exists");
  }

  txn.commit();
}

void TagRepository::deleteTag(int64_t iId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  auto res = txn.exec("SELECT name FROM tags WHERE id = $1", pqxx::params{iId});
  if (res.empty()) throw dns::common::NotFoundError("TAG_NOT_FOUND", "Tag not found");
  std::string sName = res[0][0].as<std::string>();

  // Remove tag from all zones
  txn.exec(
      "UPDATE zones SET tags = array_remove(tags, $1) WHERE $1 = ANY(tags)",
      pqxx::params{sName});

  txn.exec("DELETE FROM tags WHERE id = $1", pqxx::params{iId});
  txn.commit();
}

std::optional<TagRow> TagRepository::findById(int64_t iId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT id, name, EXTRACT(EPOCH FROM created_at)::bigint "
      "FROM tags WHERE id = $1",
      pqxx::params{iId});
  txn.commit();

  if (result.empty()) return std::nullopt;
  TagRow tr;
  tr.iId       = result[0][0].as<int64_t>();
  tr.sName     = result[0][1].as<std::string>();
  tr.tpCreatedAt = std::chrono::system_clock::time_point(
      std::chrono::seconds(result[0][2].as<int64_t>()));
  return tr;
}

}  // namespace dns::dal
