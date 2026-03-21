#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace dns::dal {

class ConnectionPool;

struct ProviderDefinitionRow {
  int64_t iId = 0;
  std::string sName;
  std::string sTypeSlug;
  std::string sVersion;
  nlohmann::json jDefinition = nlohmann::json::object();
  std::string sSourceUrl;
  int64_t iActiveInstanceCount = 0;  // populated by listAll() and findBy*()
  std::chrono::system_clock::time_point tpImportedAt;
  std::chrono::system_clock::time_point tpUpdatedAt;
};

/// Manages the provider_definitions table.
/// Class abbreviation: pdr
class ProviderDefinitionRepository {
 public:
  explicit ProviderDefinitionRepository(ConnectionPool& cpPool);
  ~ProviderDefinitionRepository();

  /// Create a new definition. Returns the new ID.
  /// Throws ConflictError if type_slug already exists.
  int64_t create(const std::string& sName, const std::string& sTypeSlug,
                 const std::string& sVersion, const nlohmann::json& jDefinition,
                 const std::string& sSourceUrl = "");

  /// List all definitions with active instance count.
  std::vector<ProviderDefinitionRow> listAll();

  /// Find by ID. Returns nullopt if not found.
  std::optional<ProviderDefinitionRow> findById(int64_t iId);

  /// Find by type_slug. Returns nullopt if not found.
  std::optional<ProviderDefinitionRow> findByTypeSlug(const std::string& sTypeSlug);

  /// Update in place (all fields). Throws NotFoundError if not found.
  void update(int64_t iId, const std::string& sName, const std::string& sVersion,
              const nlohmann::json& jDefinition, const std::string& sSourceUrl);

  /// Delete. Throws NotFoundError if not found.
  /// Throws ConflictError if any provider instances reference this definition.
  void deleteById(int64_t iId);

 private:
  ConnectionPool& _cpPool;

  ProviderDefinitionRow mapRow(const std::string& sId, const std::string& sName,
                               const std::string& sTypeSlug, const std::string& sVersion,
                               const std::string& sDefinitionStr,
                               const std::string& sSourceUrl,
                               int64_t iActiveCount,
                               int64_t iImportedEpoch, int64_t iUpdatedEpoch) const;
};

}  // namespace dns::dal
