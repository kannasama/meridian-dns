#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dns::dal {

class ConnectionPool;

/// Row type returned from record queries.
struct RecordRow {
  int64_t iId = 0;
  int64_t iZoneId = 0;
  std::string sName;
  std::string sType;
  int iTtl = 300;
  std::string sValueTemplate;
  int iPriority = 0;
  std::optional<int64_t> oLastAuditId;
  std::string sCreatedAt;
  std::string sUpdatedAt;
};

/// Manages the records table (raw templates); upsert for rollback restore.
/// Class abbreviation: rr
class RecordRepository {
 public:
  explicit RecordRepository(ConnectionPool& cpPool);
  ~RecordRepository();

  /// Create a record. Returns the new record ID.
  int64_t create(int64_t iZoneId, const std::string& sName,
                 const std::string& sType, int iTtl,
                 const std::string& sValueTemplate, int iPriority);

  /// Find a record by ID. Returns nullopt if not found.
  std::optional<RecordRow> findById(int64_t iRecordId);

  /// List all records for a zone, ordered by name then type.
  std::vector<RecordRow> listByZone(int64_t iZoneId);

  /// Update a record's fields. Optionally sets last_audit_id.
  /// Throws NotFoundError if record doesn't exist.
  void update(int64_t iRecordId, const std::string& sName,
              const std::string& sType, int iTtl,
              const std::string& sValueTemplate, int iPriority,
              std::optional<int64_t> oLastAuditId);

  /// Delete a record by ID.
  /// Throws NotFoundError if record doesn't exist.
  void deleteById(int64_t iRecordId);

  /// Upsert a record for rollback restore.
  /// If a record with the given ID exists, update it.
  /// If not, insert with the specified fields (ID is NOT preserved from snapshot;
  /// a new ID is assigned).
  void upsert(int64_t iZoneId, const std::string& sName,
              const std::string& sType, int iTtl,
              const std::string& sValueTemplate, int iPriority);

 private:
  ConnectionPool& _cpPool;
};

}  // namespace dns::dal
