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
  std::string sRole;
  std::string sDescription;
  int iMemberCount = 0;
  std::chrono::system_clock::time_point tpCreatedAt;
};

/// Manages groups and group membership.
/// Class abbreviation: gr
class GroupRepository {
 public:
  explicit GroupRepository(ConnectionPool& cpPool);

  std::vector<GroupRow> listAll();
  std::optional<GroupRow> findById(int64_t iGroupId);
  int64_t create(const std::string& sName, const std::string& sRole,
                 const std::string& sDescription);
  void update(int64_t iGroupId, const std::string& sName, const std::string& sRole,
              const std::string& sDescription);
  void deleteGroup(int64_t iGroupId);
  std::vector<std::pair<int64_t, std::string>> listMembers(int64_t iGroupId);

 private:
  ConnectionPool& _cpPool;
};

}  // namespace dns::dal
