// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dns::dal {

class ConnectionPool;

/// Row type returned from tag queries.
struct TagRow {
  int64_t iId = 0;
  std::string sName;
  std::chrono::system_clock::time_point tpCreatedAt;
  int64_t iZoneCount = 0;  // populated by listWithCounts()
};

/// Manages the tags vocabulary table.
/// Class abbreviation: tr
class TagRepository {
 public:
  explicit TagRepository(ConnectionPool& cpPool);
  ~TagRepository();

  /// List all tags with per-tag zone usage counts.
  std::vector<TagRow> listWithCounts();

  /// Insert tag names that don't yet exist in the vocabulary; no-op for duplicates.
  void upsertVocabulary(const std::vector<std::string>& vTags);

  /// Rename a tag by ID, cascading the rename into all zones.tags arrays.
  /// Throws NotFoundError if ID not found, ConflictError on duplicate name.
  void rename(int64_t iId, const std::string& sNewName);

  /// Delete a tag by ID, removing it from all zones.tags arrays.
  /// Throws NotFoundError if ID not found.
  void deleteTag(int64_t iId);

  /// Find a tag by ID. Returns nullopt if not found.
  std::optional<TagRow> findById(int64_t iId);

 private:
  ConnectionPool& _cpPool;
};

}  // namespace dns::dal
