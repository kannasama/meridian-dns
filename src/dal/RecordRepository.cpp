#include "dal/RecordRepository.hpp"

#include "common/Errors.hpp"
#include "dal/ConnectionPool.hpp"

#include <pqxx/pqxx>

namespace dns::dal {

RecordRepository::RecordRepository(ConnectionPool& cpPool) : _cpPool(cpPool) {}
RecordRepository::~RecordRepository() = default;

int64_t RecordRepository::create(int64_t iZoneId, const std::string& sName,
                                 const std::string& sType, int iTtl,
                                 const std::string& sValueTemplate,
                                 int iPriority) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  try {
    auto result = txn.exec(
        "INSERT INTO records (zone_id, name, type, ttl, value_template, priority) "
        "VALUES ($1, $2, $3, $4, $5, $6) RETURNING id",
        pqxx::params{iZoneId, sName, sType, iTtl, sValueTemplate, iPriority});
    txn.commit();
    return result.one_row()[0].as<int64_t>();
  } catch (const pqxx::foreign_key_violation&) {
    throw common::NotFoundError("zone_not_found", "Zone not found");
  }
}

std::optional<RecordRow> RecordRepository::findById(int64_t iRecordId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT id, zone_id, name, type, ttl, value_template, priority, "
      "last_audit_id, created_at::text, updated_at::text "
      "FROM records WHERE id = $1",
      pqxx::params{iRecordId});
  txn.commit();

  if (result.empty()) return std::nullopt;

  auto row = result[0];
  RecordRow rRow;
  rRow.iId = row[0].as<int64_t>();
  rRow.iZoneId = row[1].as<int64_t>();
  rRow.sName = row[2].as<std::string>();
  rRow.sType = row[3].as<std::string>();
  rRow.iTtl = row[4].as<int>();
  rRow.sValueTemplate = row[5].as<std::string>();
  rRow.iPriority = row[6].as<int>();
  if (!row[7].is_null()) {
    rRow.oLastAuditId = row[7].as<int64_t>();
  }
  rRow.sCreatedAt = row[8].as<std::string>();
  rRow.sUpdatedAt = row[9].as<std::string>();
  return rRow;
}

std::vector<RecordRow> RecordRepository::listByZone(int64_t iZoneId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT id, zone_id, name, type, ttl, value_template, priority, "
      "last_audit_id, created_at::text, updated_at::text "
      "FROM records WHERE zone_id = $1 ORDER BY name, type",
      pqxx::params{iZoneId});
  txn.commit();

  std::vector<RecordRow> vRows;
  vRows.reserve(result.size());
  for (const auto& row : result) {
    RecordRow rRow;
    rRow.iId = row[0].as<int64_t>();
    rRow.iZoneId = row[1].as<int64_t>();
    rRow.sName = row[2].as<std::string>();
    rRow.sType = row[3].as<std::string>();
    rRow.iTtl = row[4].as<int>();
    rRow.sValueTemplate = row[5].as<std::string>();
    rRow.iPriority = row[6].as<int>();
    if (!row[7].is_null()) {
      rRow.oLastAuditId = row[7].as<int64_t>();
    }
    rRow.sCreatedAt = row[8].as<std::string>();
    rRow.sUpdatedAt = row[9].as<std::string>();
    vRows.push_back(std::move(rRow));
  }
  return vRows;
}

void RecordRepository::update(int64_t iRecordId, const std::string& sName,
                              const std::string& sType, int iTtl,
                              const std::string& sValueTemplate, int iPriority,
                              std::optional<int64_t> oLastAuditId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  pqxx::result result;
  if (oLastAuditId.has_value()) {
    result = txn.exec(
        "UPDATE records SET name = $2, type = $3, ttl = $4, "
        "value_template = $5, priority = $6, last_audit_id = $7, "
        "updated_at = NOW() WHERE id = $1",
        pqxx::params{iRecordId, sName, sType, iTtl, sValueTemplate,
                     iPriority, *oLastAuditId});
  } else {
    result = txn.exec(
        "UPDATE records SET name = $2, type = $3, ttl = $4, "
        "value_template = $5, priority = $6, updated_at = NOW() "
        "WHERE id = $1",
        pqxx::params{iRecordId, sName, sType, iTtl, sValueTemplate,
                     iPriority});
  }
  txn.commit();
  if (result.affected_rows() == 0) {
    throw common::NotFoundError("record_not_found", "Record not found");
  }
}

void RecordRepository::deleteById(int64_t iRecordId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec("DELETE FROM records WHERE id = $1",
                         pqxx::params{iRecordId});
  txn.commit();
  if (result.affected_rows() == 0) {
    throw common::NotFoundError("record_not_found", "Record not found");
  }
}

void RecordRepository::upsert(int64_t iZoneId, const std::string& sName,
                              const std::string& sType, int iTtl,
                              const std::string& sValueTemplate,
                              int iPriority) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  // Use a simple delete-then-insert within the same transaction.
  txn.exec(
      "DELETE FROM records WHERE zone_id = $1 AND name = $2 AND type = $3",
      pqxx::params{iZoneId, sName, sType});
  txn.exec(
      "INSERT INTO records (zone_id, name, type, ttl, value_template, priority) "
      "VALUES ($1, $2, $3, $4, $5, $6)",
      pqxx::params{iZoneId, sName, sType, iTtl, sValueTemplate, iPriority});
  txn.commit();
}

}  // namespace dns::dal
