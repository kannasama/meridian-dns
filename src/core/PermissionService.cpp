#include "core/PermissionService.hpp"

#include "dal/RoleRepository.hpp"
#include "dal/ZoneRepository.hpp"

namespace dns::core {

PermissionService::PermissionService(dns::dal::RoleRepository& rrRepo,
                                     dns::dal::ZoneRepository& zrRepo)
    : _rrRepo(rrRepo), _zrRepo(zrRepo) {}

PermissionService::~PermissionService() = default;

bool PermissionService::hasPermission(int64_t iUserId, std::string_view svPermission) {
  auto perms = _rrRepo.resolveUserPermissions(iUserId);
  return perms.count(std::string(svPermission)) > 0;
}

bool PermissionService::hasPermissionForZone(int64_t iUserId,
                                              std::string_view svPermission,
                                              int64_t /*iZoneId*/) {
  // With group→role model, all permissions are global.
  auto perms = _rrRepo.resolveUserPermissions(iUserId);
  return perms.count(std::string(svPermission)) > 0;
}

bool PermissionService::hasPermissionForView(int64_t iUserId,
                                              std::string_view svPermission,
                                              int64_t /*iViewId*/) {
  // With group→role model, all permissions are global.
  auto perms = _rrRepo.resolveUserPermissions(iUserId);
  return perms.count(std::string(svPermission)) > 0;
}

std::unordered_set<std::string> PermissionService::getEffectivePermissions(
    int64_t iUserId) {
  return _rrRepo.resolveUserPermissions(iUserId);
}

std::unordered_set<std::string> PermissionService::getEffectivePermissionsForZone(
    int64_t iUserId, int64_t /*iZoneId*/) {
  // With group→role model, all permissions are global.
  return _rrRepo.resolveUserPermissions(iUserId);
}

}  // namespace dns::core
