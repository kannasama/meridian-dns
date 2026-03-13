#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>

namespace dns::dal {
class RoleRepository;
class ZoneRepository;
}  // namespace dns::dal

namespace dns::core {

/// Resolves effective permissions for a user.
/// With the group→role model, all permissions are global (no resource scoping).
/// The zone/view variants are kept for API compatibility but resolve identically.
/// Class abbreviation: ps
class PermissionService {
 public:
  PermissionService(dns::dal::RoleRepository& rrRepo,
                    dns::dal::ZoneRepository& zrRepo);
  ~PermissionService();

  /// Check if a user has a specific permission.
  bool hasPermission(int64_t iUserId, std::string_view svPermission);

  /// Check if a user has a specific permission (zone context — resolves globally).
  bool hasPermissionForZone(int64_t iUserId, std::string_view svPermission,
                            int64_t iZoneId);

  /// Check if a user has a specific permission (view context — resolves globally).
  bool hasPermissionForView(int64_t iUserId, std::string_view svPermission,
                            int64_t iViewId);

  /// Get all effective permissions for a user.
  std::unordered_set<std::string> getEffectivePermissions(int64_t iUserId);

  /// Get all effective permissions for a user (zone context — resolves globally).
  std::unordered_set<std::string> getEffectivePermissionsForZone(
      int64_t iUserId, int64_t iZoneId);

 private:
  dns::dal::RoleRepository& _rrRepo;
  dns::dal::ZoneRepository& _zrRepo;
};

}  // namespace dns::core
