#include "dal/RoleRepository.hpp"

#include "common/Errors.hpp"
#include "dal/ConnectionPool.hpp"

#include <pqxx/pqxx>

namespace dns::dal {

RoleRepository::RoleRepository(ConnectionPool& cpPool) : _cpPool(cpPool) {}

RoleRepository::~RoleRepository() = default;

std::vector<RoleRow> RoleRepository::listAll() {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT id, name, COALESCE(description, ''), is_system, "
      "TO_CHAR(created_at, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') "
      "FROM roles ORDER BY is_system DESC, name");
  txn.commit();

  std::vector<RoleRow> vRoles;
  vRoles.reserve(result.size());
  for (const auto& row : result) {
    vRoles.push_back({
        row[0].as<int64_t>(),
        row[1].as<std::string>(),
        row[2].as<std::string>(),
        row[3].as<bool>(),
        row[4].as<std::string>(),
    });
  }
  return vRoles;
}

std::optional<RoleRow> RoleRepository::findById(int64_t iRoleId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT id, name, COALESCE(description, ''), is_system, "
      "TO_CHAR(created_at, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') "
      "FROM roles WHERE id = $1",
      pqxx::params{iRoleId});
  txn.commit();

  if (result.empty()) return std::nullopt;
  const auto& row = result[0];
  return RoleRow{
      row[0].as<int64_t>(),
      row[1].as<std::string>(),
      row[2].as<std::string>(),
      row[3].as<bool>(),
      row[4].as<std::string>(),
  };
}

std::optional<RoleRow> RoleRepository::findByName(const std::string& sName) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT id, name, COALESCE(description, ''), is_system, "
      "TO_CHAR(created_at, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') "
      "FROM roles WHERE name = $1",
      pqxx::params{sName});
  txn.commit();

  if (result.empty()) return std::nullopt;
  const auto& row = result[0];
  return RoleRow{
      row[0].as<int64_t>(),
      row[1].as<std::string>(),
      row[2].as<std::string>(),
      row[3].as<bool>(),
      row[4].as<std::string>(),
  };
}

int64_t RoleRepository::create(const std::string& sName, const std::string& sDescription) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  try {
    auto result = txn.exec(
        "INSERT INTO roles (name, description) VALUES ($1, $2) RETURNING id",
        pqxx::params{sName, sDescription});
    txn.commit();
    return result[0][0].as<int64_t>();
  } catch (const pqxx::unique_violation&) {
    throw common::ConflictError("ROLE_EXISTS", "Role name already exists");
  }
}

void RoleRepository::update(int64_t iRoleId, const std::string& sName,
                             const std::string& sDescription) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  // Check if system role — prevent name change
  auto check = txn.exec(
      "SELECT is_system, name FROM roles WHERE id = $1",
      pqxx::params{iRoleId});
  if (check.empty())
    throw common::NotFoundError("ROLE_NOT_FOUND", "Role not found");

  if (check[0][0].as<bool>() && sName != check[0][1].as<std::string>()) {
    throw common::ValidationError("SYSTEM_ROLE_RENAME",
                                   "Cannot rename a system role");
  }

  auto result = txn.exec(
      "UPDATE roles SET name = $1, description = $2 WHERE id = $3",
      pqxx::params{sName, sDescription, iRoleId});
  txn.commit();

  if (result.affected_rows() == 0)
    throw common::NotFoundError("ROLE_NOT_FOUND", "Role not found");
}

void RoleRepository::deleteRole(int64_t iRoleId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  // Prevent deletion of system roles
  auto check = txn.exec("SELECT is_system FROM roles WHERE id = $1",
                         pqxx::params{iRoleId});
  if (check.empty())
    throw common::NotFoundError("ROLE_NOT_FOUND", "Role not found");

  if (check[0][0].as<bool>()) {
    throw common::ConflictError("SYSTEM_ROLE_DELETE",
                                 "Cannot delete a system role");
  }

  // Check if role is in use
  auto usage = txn.exec(
      "SELECT COUNT(*) FROM group_members WHERE role_id = $1",
      pqxx::params{iRoleId});
  if (usage[0][0].as<int>() > 0) {
    throw common::ConflictError("ROLE_IN_USE",
                                 "Cannot delete role: still assigned to group members");
  }

  txn.exec("DELETE FROM roles WHERE id = $1", pqxx::params{iRoleId});
  txn.commit();
}

std::unordered_set<std::string> RoleRepository::getPermissions(int64_t iRoleId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT permission FROM role_permissions WHERE role_id = $1 ORDER BY permission",
      pqxx::params{iRoleId});
  txn.commit();

  std::unordered_set<std::string> vPerms;
  for (const auto& row : result) {
    vPerms.insert(row[0].as<std::string>());
  }
  return vPerms;
}

void RoleRepository::setPermissions(int64_t iRoleId,
                                     const std::vector<std::string>& vPermissions) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  // Verify role exists
  auto check = txn.exec("SELECT 1 FROM roles WHERE id = $1", pqxx::params{iRoleId});
  if (check.empty())
    throw common::NotFoundError("ROLE_NOT_FOUND", "Role not found");

  // Replace all permissions
  txn.exec("DELETE FROM role_permissions WHERE role_id = $1", pqxx::params{iRoleId});
  for (const auto& sPerm : vPermissions) {
    txn.exec(
        "INSERT INTO role_permissions (role_id, permission) VALUES ($1, $2)",
        pqxx::params{iRoleId, sPerm});
  }
  txn.commit();
}

std::unordered_set<std::string> RoleRepository::resolveUserPermissions(
    int64_t iUserId, int64_t iViewId, int64_t iZoneId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  // Collect permissions from all matching group memberships:
  // - Global scope (scope_type IS NULL)
  // - View-level scope matching iViewId
  // - Zone-level scope matching iZoneId
  auto result = txn.exec(
      "SELECT DISTINCT rp.permission "
      "FROM group_members gm "
      "JOIN role_permissions rp ON rp.role_id = gm.role_id "
      "WHERE gm.user_id = $1 "
      "AND ("
      "  gm.scope_type IS NULL "
      "  OR (gm.scope_type = 'view' AND gm.scope_id = $2 AND $2 > 0) "
      "  OR (gm.scope_type = 'zone' AND gm.scope_id = $3 AND $3 > 0)"
      ")",
      pqxx::params{iUserId, iViewId, iZoneId});
  txn.commit();

  std::unordered_set<std::string> vPerms;
  for (const auto& row : result) {
    vPerms.insert(row[0].as<std::string>());
  }
  return vPerms;
}

std::string RoleRepository::getHighestRoleName(int64_t iUserId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  // Find the role with the most permissions among the user's global assignments
  auto result = txn.exec(
      "SELECT r.name, COUNT(rp.permission) AS perm_count "
      "FROM group_members gm "
      "JOIN roles r ON r.id = gm.role_id "
      "LEFT JOIN role_permissions rp ON rp.role_id = r.id "
      "WHERE gm.user_id = $1 AND gm.scope_type IS NULL "
      "GROUP BY r.name "
      "ORDER BY perm_count DESC "
      "LIMIT 1",
      pqxx::params{iUserId});
  txn.commit();

  if (result.empty()) return "";
  return result[0][0].as<std::string>();
}

}  // namespace dns::dal
