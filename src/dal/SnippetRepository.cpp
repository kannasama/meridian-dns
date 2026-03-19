// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "dal/SnippetRepository.hpp"

#include "common/Errors.hpp"
#include "dal/ConnectionPool.hpp"

#include <pqxx/pqxx>

namespace dns::dal {

SnippetRepository::SnippetRepository(ConnectionPool& cpPool) : _cpPool(cpPool) {}
SnippetRepository::~SnippetRepository() = default;

int64_t SnippetRepository::create(const std::string& sName, const std::string& sDescription) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  try {
    auto result = txn.exec(
        "INSERT INTO snippets (name, description) VALUES ($1, $2) RETURNING id",
        pqxx::params{sName, sDescription});
    txn.commit();
    return result.one_row()[0].as<int64_t>();
  } catch (const pqxx::unique_violation&) {
    throw common::ConflictError("SNIPPET_EXISTS",
                                "Snippet '" + sName + "' already exists");
  }
}

std::vector<SnippetRow> SnippetRepository::listAll() {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT id, name, description, "
      "EXTRACT(EPOCH FROM created_at)::bigint, "
      "EXTRACT(EPOCH FROM updated_at)::bigint "
      "FROM snippets ORDER BY name");
  txn.commit();

  std::vector<SnippetRow> vRows;
  vRows.reserve(result.size());
  for (const auto& row : result) {
    SnippetRow sr;
    sr.iId          = row[0].as<int64_t>();
    sr.sName        = row[1].as<std::string>();
    sr.sDescription = row[2].as<std::string>();
    sr.tpCreatedAt  = std::chrono::system_clock::time_point(
        std::chrono::seconds(row[3].as<int64_t>()));
    sr.tpUpdatedAt  = std::chrono::system_clock::time_point(
        std::chrono::seconds(row[4].as<int64_t>()));
    vRows.push_back(std::move(sr));
  }
  return vRows;
}

std::optional<SnippetRow> SnippetRepository::findById(int64_t iId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  auto result = txn.exec(
      "SELECT id, name, description, "
      "EXTRACT(EPOCH FROM created_at)::bigint, "
      "EXTRACT(EPOCH FROM updated_at)::bigint "
      "FROM snippets WHERE id = $1",
      pqxx::params{iId});

  if (result.empty()) {
    txn.commit();
    return std::nullopt;
  }

  SnippetRow sr;
  sr.iId          = result[0][0].as<int64_t>();
  sr.sName        = result[0][1].as<std::string>();
  sr.sDescription = result[0][2].as<std::string>();
  sr.tpCreatedAt  = std::chrono::system_clock::time_point(
      std::chrono::seconds(result[0][3].as<int64_t>()));
  sr.tpUpdatedAt  = std::chrono::system_clock::time_point(
      std::chrono::seconds(result[0][4].as<int64_t>()));

  auto recs = txn.exec(
      "SELECT id, snippet_id, name, type, ttl, value_template, priority, sort_order "
      "FROM snippet_records WHERE snippet_id = $1 ORDER BY sort_order, id",
      pqxx::params{iId});
  txn.commit();

  sr.vRecords.reserve(recs.size());
  for (const auto& row : recs) {
    SnippetRecordRow rec;
    rec.iId             = row[0].as<int64_t>();
    rec.iSnippetId      = row[1].as<int64_t>();
    rec.sName           = row[2].as<std::string>();
    rec.sType           = row[3].as<std::string>();
    rec.iTtl            = row[4].as<int>();
    rec.sValueTemplate  = row[5].as<std::string>();
    rec.iPriority       = row[6].as<int>();
    rec.iSortOrder      = row[7].as<int>();
    sr.vRecords.push_back(std::move(rec));
  }

  return sr;
}

void SnippetRepository::update(int64_t iId, const std::string& sName,
                               const std::string& sDescription) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  pqxx::result result;
  try {
    result = txn.exec(
        "UPDATE snippets SET name = $2, description = $3 WHERE id = $1",
        pqxx::params{iId, sName, sDescription});
    txn.commit();
  } catch (const pqxx::unique_violation&) {
    throw common::ConflictError("SNIPPET_EXISTS",
                                "Snippet '" + sName + "' already exists");
  }

  if (result.affected_rows() == 0) {
    throw common::NotFoundError("SNIPPET_NOT_FOUND",
                                "Snippet with id " + std::to_string(iId) + " not found");
  }
}

void SnippetRepository::deleteById(int64_t iId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  try {
    auto result = txn.exec("DELETE FROM snippets WHERE id = $1", pqxx::params{iId});
    txn.commit();

    if (result.affected_rows() == 0) {
      throw common::NotFoundError("SNIPPET_NOT_FOUND",
                                  "Snippet with id " + std::to_string(iId) + " not found");
    }
  } catch (const pqxx::foreign_key_violation&) {
    throw common::ConflictError("SNIPPET_IN_USE",
                                "Snippet with id " + std::to_string(iId) +
                                " is referenced by a zone template");
  }
}

void SnippetRepository::replaceRecords(int64_t iSnippetId,
                                       const std::vector<SnippetRecordRow>& vRecords) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  txn.exec("DELETE FROM snippet_records WHERE snippet_id = $1", pqxx::params{iSnippetId});

  for (const auto& rec : vRecords) {
    txn.exec(
        "INSERT INTO snippet_records "
        "(snippet_id, name, type, ttl, value_template, priority, sort_order) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7)",
        pqxx::params{iSnippetId, rec.sName, rec.sType, rec.iTtl,
                     rec.sValueTemplate, rec.iPriority, rec.iSortOrder});
  }

  txn.commit();
}

std::vector<SnippetRecordRow> SnippetRepository::listRecords(int64_t iSnippetId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT id, snippet_id, name, type, ttl, value_template, priority, sort_order "
      "FROM snippet_records WHERE snippet_id = $1 ORDER BY sort_order, id",
      pqxx::params{iSnippetId});
  txn.commit();

  std::vector<SnippetRecordRow> vRecs;
  vRecs.reserve(result.size());
  for (const auto& row : result) {
    SnippetRecordRow rec;
    rec.iId            = row[0].as<int64_t>();
    rec.iSnippetId     = row[1].as<int64_t>();
    rec.sName          = row[2].as<std::string>();
    rec.sType          = row[3].as<std::string>();
    rec.iTtl           = row[4].as<int>();
    rec.sValueTemplate = row[5].as<std::string>();
    rec.iPriority      = row[6].as<int>();
    rec.iSortOrder     = row[7].as<int>();
    vRecs.push_back(std::move(rec));
  }
  return vRecs;
}

}  // namespace dns::dal
