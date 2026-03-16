#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace dns::dal {

class ConnectionPool;

/// Row type returned from role queries.
struct RoleRow {
  int64_t iId = 0;
  std::string sName;
  std::string sDescription;
  bool bIsSystem = false;
  std::string sCreatedAt;
};

/// Manages roles and role_permissions.
/// Class abbreviation: rr (role repo)
class RoleRepository {
 public:
  explicit RoleRepository(ConnectionPool& cpPool);
  ~RoleRepository();

  /// List all roles.
  std::vector<RoleRow> listAll();

  /// Find a role by ID. Returns nullopt if not found.
  std::optional<RoleRow> findById(int64_t iRoleId);

  /// Find a role by name. Returns nullopt if not found.
  std::optional<RoleRow> findByName(const std::string& sName);

  /// Create a custom role. Returns the new role ID.
  int64_t create(const std::string& sName, const std::string& sDescription);

  /// Update a role's name and description. Throws if is_system and name changed.
  void update(int64_t iRoleId, const std::string& sName, const std::string& sDescription);

  /// Delete a role. Throws if is_system.
  void deleteRole(int64_t iRoleId);

  /// Get all permissions for a role.
  std::unordered_set<std::string> getPermissions(int64_t iRoleId);

  /// Set permissions for a role (replaces all existing).
  void setPermissions(int64_t iRoleId, const std::vector<std::string>& vPermissions);

  /// Resolve all permissions for a user across all group memberships.
  /// Permissions come from groups.role_id → role_permissions.
  std::unordered_set<std::string> resolveUserPermissions(int64_t iUserId);

  /// Get the highest-privilege role name for a user (for display/JWT).
  /// Returns empty string if no group membership.
  std::string getHighestRoleName(int64_t iUserId);

 private:
  ConnectionPool& _cpPool;
};

}  // namespace dns::dal
