#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dns::dal {

class ConnectionPool;

/// Result of a purge operation.
struct PurgeResult {
  int64_t iDeletedCount = 0;
  std::optional<std::chrono::system_clock::time_point> oOldestRemaining;
};

/// Entry for insert/bulkInsert operations.
struct AuditEntry {
  std::string sEntityType;
  int64_t iEntityId = 0;
  std::string sOperation;
  std::string sOldValue;   // JSON string or empty
  std::string sNewValue;   // JSON string or empty
  std::string sIdentity;
  std::string sAuthMethod;
  std::string sIpAddress;
};

/// Row type returned from audit log queries.
struct AuditRow {
  int64_t iId = 0;
  std::string sEntityType;
  int64_t iEntityId = 0;
  std::string sOperation;
  std::string sOldValue;
  std::string sNewValue;
  std::string sIdentity;
  std::string sAuthMethod;
  std::string sIpAddress;
  std::string sTimestamp;
};

/// Manages the audit_log table; insert, bulk-insert, purgeOld.
/// Class abbreviation: ar
class AuditRepository {
 public:
  explicit AuditRepository(ConnectionPool& cpPool);
  ~AuditRepository();

  /// Insert a single audit entry. Returns the new audit log ID.
  int64_t insert(const AuditEntry& aeEntry);

  /// Bulk-insert multiple audit entries in a single transaction.
  void bulkInsert(const std::vector<AuditEntry>& vEntries);

  /// Query the audit log with optional filters.
  /// Results ordered by timestamp DESC.
  std::vector<AuditRow> query(std::optional<std::string> oEntityType,
                              std::optional<std::string> oIdentity,
                              std::optional<std::string> oFrom,
                              std::optional<std::string> oTo,
                              int iLimit, int iOffset);

  /// Purge entries older than iRetentionDays.
  /// Returns count of deleted rows and timestamp of oldest remaining.
  PurgeResult purgeOld(int iRetentionDays);

 private:
  ConnectionPool& _cpPool;
};

}  // namespace dns::dal
