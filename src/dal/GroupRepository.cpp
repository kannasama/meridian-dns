#include "dal/GroupRepository.hpp"

#include "common/Errors.hpp"
#include "dal/ConnectionPool.hpp"

#include <pqxx/pqxx>

namespace dns::dal {

GroupRepository::GroupRepository(ConnectionPool& cpPool) : _cpPool(cpPool) {}

std::vector<GroupRow> GroupRepository::listAll() {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT g.id, g.name, g.role, COALESCE(g.description, ''), "
      "EXTRACT(EPOCH FROM g.created_at)::bigint, "
      "COUNT(gm.user_id)::int "
      "FROM groups g "
      "LEFT JOIN group_members gm ON gm.group_id = g.id "
      "GROUP BY g.id ORDER BY g.name");
  txn.commit();

  std::vector<GroupRow> vGroups;
  vGroups.reserve(result.size());
  for (const auto& row : result) {
    vGroups.push_back({
        row[0].as<int64_t>(),
        row[1].as<std::string>(),
        row[2].as<std::string>(),
        row[3].as<std::string>(),
        row[5].as<int>(),
        std::chrono::system_clock::time_point(std::chrono::seconds(row[4].as<int64_t>())),
    });
  }
  return vGroups;
}

std::optional<GroupRow> GroupRepository::findById(int64_t iGroupId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT g.id, g.name, g.role, COALESCE(g.description, ''), "
      "EXTRACT(EPOCH FROM g.created_at)::bigint, "
      "COUNT(gm.user_id)::int "
      "FROM groups g "
      "LEFT JOIN group_members gm ON gm.group_id = g.id "
      "WHERE g.id = $1 GROUP BY g.id",
      pqxx::params{iGroupId});
  txn.commit();

  if (result.empty()) return std::nullopt;
  const auto& row = result[0];
  return GroupRow{
      row[0].as<int64_t>(),
      row[1].as<std::string>(),
      row[2].as<std::string>(),
      row[3].as<std::string>(),
      row[5].as<int>(),
      std::chrono::system_clock::time_point(std::chrono::seconds(row[4].as<int64_t>())),
  };
}

int64_t GroupRepository::create(const std::string& sName, const std::string& sRole,
                                const std::string& sDescription) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  try {
    auto result = txn.exec(
        "INSERT INTO groups (name, role, description) VALUES ($1, $2, $3) RETURNING id",
        pqxx::params{sName, sRole, sDescription});
    txn.commit();
    return result[0][0].as<int64_t>();
  } catch (const pqxx::unique_violation&) {
    throw common::ConflictError("GROUP_EXISTS", "Group name already exists");
  }
}

void GroupRepository::update(int64_t iGroupId, const std::string& sName,
                             const std::string& sRole, const std::string& sDescription) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "UPDATE groups SET name = $1, role = $2, description = $3 WHERE id = $4",
      pqxx::params{sName, sRole, sDescription, iGroupId});
  txn.commit();
  if (result.affected_rows() == 0)
    throw common::NotFoundError("GROUP_NOT_FOUND", "Group not found");
}

void GroupRepository::deleteGroup(int64_t iGroupId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  auto check = txn.exec(
      "SELECT gm.user_id FROM group_members gm "
      "WHERE gm.group_id = $1 "
      "AND NOT EXISTS ("
      "  SELECT 1 FROM group_members gm2 "
      "  WHERE gm2.user_id = gm.user_id AND gm2.group_id != $1"
      ")",
      pqxx::params{iGroupId});

  if (!check.empty())
    throw common::ConflictError("GROUP_SOLE_MEMBERSHIP",
        "Cannot delete: group is the only group for one or more users");

  auto result = txn.exec("DELETE FROM groups WHERE id = $1", pqxx::params{iGroupId});
  txn.commit();

  if (result.affected_rows() == 0)
    throw common::NotFoundError("GROUP_NOT_FOUND", "Group not found");
}

std::vector<std::pair<int64_t, std::string>> GroupRepository::listMembers(int64_t iGroupId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT u.id, u.username FROM users u "
      "JOIN group_members gm ON gm.user_id = u.id "
      "WHERE gm.group_id = $1 ORDER BY u.username",
      pqxx::params{iGroupId});
  txn.commit();

  std::vector<std::pair<int64_t, std::string>> vMembers;
  vMembers.reserve(result.size());
  for (const auto& row : result)
    vMembers.emplace_back(row[0].as<int64_t>(), row[1].as<std::string>());
  return vMembers;
}

}  // namespace dns::dal
