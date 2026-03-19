#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dns::dal {

class ConnectionPool;

/// A single record within a snippet.
struct SnippetRecordRow {
  int64_t iId         = 0;
  int64_t iSnippetId  = 0;
  std::string sName;
  std::string sType;
  int iTtl            = 300;
  std::string sValueTemplate;
  int iPriority       = 0;
  int iSortOrder      = 0;
};

/// A snippet with its records (populated by findById).
struct SnippetRow {
  int64_t iId = 0;
  std::string sName;
  std::string sDescription;
  std::vector<SnippetRecordRow> vRecords;
  std::chrono::system_clock::time_point tpCreatedAt;
  std::chrono::system_clock::time_point tpUpdatedAt;
};

/// Manages the snippets and snippet_records tables.
/// Class abbreviation: snr
class SnippetRepository {
 public:
  explicit SnippetRepository(ConnectionPool& cpPool);
  ~SnippetRepository();

  int64_t create(const std::string& sName, const std::string& sDescription);
  std::vector<SnippetRow> listAll();                    // without records
  std::optional<SnippetRow> findById(int64_t iId);     // with records
  void update(int64_t iId, const std::string& sName, const std::string& sDescription);
  void deleteById(int64_t iId);
  void replaceRecords(int64_t iSnippetId, const std::vector<SnippetRecordRow>& vRecords);
  std::vector<SnippetRecordRow> listRecords(int64_t iSnippetId);

 private:
  ConnectionPool& _cpPool;
};

}  // namespace dns::dal
