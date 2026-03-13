#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dns::dal {

class ConnectionPool;

struct GroupRow {
  int64_t iId = 0;
  std::string sName;
  std::string sDescription;
  int64_t iRoleId = 0;
  std::string sRoleName;
  int iMemberCount = 0;
  std::chrono::system_clock::time_point tpCreatedAt;
};

struct GroupMemberRow {
  int64_t iUserId = 0;
  std::string sUsername;
};

/// Manages groups and group membership.
/// Class abbreviation: gr
class GroupRepository {
 public:
  explicit GroupRepository(ConnectionPool& cpPool);

  std::vector<GroupRow> listAll();
  std::optional<GroupRow> findById(int64_t iGroupId);
  int64_t create(const std::string& sName, const std::string& sDescription, int64_t iRoleId);
  void update(int64_t iGroupId, const std::string& sName, const std::string& sDescription,
              int64_t iRoleId);
  void deleteGroup(int64_t iGroupId);
  std::vector<GroupMemberRow> listMembers(int64_t iGroupId);

  /// Add a member to a group.
  void addMember(int64_t iGroupId, int64_t iUserId);

  /// Remove a member from a group.
  void removeMember(int64_t iGroupId, int64_t iUserId);

 private:
  ConnectionPool& _cpPool;
};

}  // namespace dns::dal
