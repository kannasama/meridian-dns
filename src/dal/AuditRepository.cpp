#include "dal/AuditRepository.hpp"

#include "dal/ConnectionPool.hpp"

#include <pqxx/pqxx>

namespace dns::dal {

AuditRepository::AuditRepository(ConnectionPool& cpPool) : _cpPool(cpPool) {}
AuditRepository::~AuditRepository() = default;

int64_t AuditRepository::insert(const AuditEntry& aeEntry) {
  std::optional<std::string> oOldValue;
  if (!aeEntry.sOldValue.empty()) oOldValue = aeEntry.sOldValue;
  std::optional<std::string> oNewValue;
  if (!aeEntry.sNewValue.empty()) oNewValue = aeEntry.sNewValue;
  std::optional<std::string> oAuthMethod;
  if (!aeEntry.sAuthMethod.empty()) oAuthMethod = aeEntry.sAuthMethod;
  std::optional<std::string> oIpAddress;
  if (!aeEntry.sIpAddress.empty()) oIpAddress = aeEntry.sIpAddress;

  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "INSERT INTO audit_log (entity_type, entity_id, operation, "
      "old_value, new_value, identity, auth_method, ip_address) "
      "VALUES ($1, $2, $3, $4::jsonb, $5::jsonb, $6, "
      "$7::auth_method, $8::inet) RETURNING id",
      pqxx::params{
          aeEntry.sEntityType,
          aeEntry.iEntityId,
          aeEntry.sOperation,
          oOldValue,
          oNewValue,
          aeEntry.sIdentity,
          oAuthMethod,
          oIpAddress,
      });
  txn.commit();
  return result.one_row()[0].as<int64_t>();
}

void AuditRepository::bulkInsert(const std::vector<AuditEntry>& vEntries) {
  if (vEntries.empty()) return;

  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  for (const auto& ae : vEntries) {
    std::optional<std::string> oOldValue;
    if (!ae.sOldValue.empty()) oOldValue = ae.sOldValue;
    std::optional<std::string> oNewValue;
    if (!ae.sNewValue.empty()) oNewValue = ae.sNewValue;
    std::optional<std::string> oAuthMethod;
    if (!ae.sAuthMethod.empty()) oAuthMethod = ae.sAuthMethod;
    std::optional<std::string> oIpAddress;
    if (!ae.sIpAddress.empty()) oIpAddress = ae.sIpAddress;

    txn.exec(
        "INSERT INTO audit_log (entity_type, entity_id, operation, "
        "old_value, new_value, identity, auth_method, ip_address) "
        "VALUES ($1, $2, $3, $4::jsonb, $5::jsonb, $6, "
        "$7::auth_method, $8::inet)",
        pqxx::params{
            ae.sEntityType,
            ae.iEntityId,
            ae.sOperation,
            oOldValue,
            oNewValue,
            ae.sIdentity,
            oAuthMethod,
            oIpAddress,
        });
  }
  txn.commit();
}

std::vector<AuditRow> AuditRepository::query(
    std::optional<std::string> oEntityType,
    std::optional<std::string> oIdentity,
    std::optional<std::string> oFrom,
    std::optional<std::string> oTo,
    int iLimit, int iOffset) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  // Build query dynamically based on filters
  std::string sSql =
      "SELECT id, entity_type, COALESCE(entity_id, 0), operation, "
      "COALESCE(old_value::text, ''), COALESCE(new_value::text, ''), "
      "identity, COALESCE(auth_method::text, ''), "
      "COALESCE(host(ip_address), ''), timestamp::text "
      "FROM audit_log WHERE 1=1";
  std::vector<std::string> vParams;
  int iParamIdx = 1;

  if (oEntityType.has_value()) {
    sSql += " AND entity_type = $" + std::to_string(iParamIdx++);
    vParams.push_back(*oEntityType);
  }
  if (oIdentity.has_value()) {
    sSql += " AND identity = $" + std::to_string(iParamIdx++);
    vParams.push_back(*oIdentity);
  }
  if (oFrom.has_value()) {
    sSql += " AND timestamp >= $" + std::to_string(iParamIdx++) + "::timestamptz";
    vParams.push_back(*oFrom);
  }
  if (oTo.has_value()) {
    sSql += " AND timestamp <= $" + std::to_string(iParamIdx++) + "::timestamptz";
    vParams.push_back(*oTo);
  }
  sSql += " ORDER BY timestamp DESC";
  sSql += " LIMIT $" + std::to_string(iParamIdx++);
  vParams.push_back(std::to_string(iLimit));
  sSql += " OFFSET $" + std::to_string(iParamIdx++);
  vParams.push_back(std::to_string(iOffset));

  pqxx::params params;
  for (const auto& p : vParams) {
    params.append(p);
  }

  auto result = txn.exec(sSql, params);
  txn.commit();

  std::vector<AuditRow> vRows;
  vRows.reserve(result.size());
  for (const auto& row : result) {
    AuditRow aRow;
    aRow.iId = row[0].as<int64_t>();
    aRow.sEntityType = row[1].as<std::string>();
    aRow.iEntityId = row[2].as<int64_t>();
    aRow.sOperation = row[3].as<std::string>();
    aRow.sOldValue = row[4].as<std::string>();
    aRow.sNewValue = row[5].as<std::string>();
    aRow.sIdentity = row[6].as<std::string>();
    aRow.sAuthMethod = row[7].as<std::string>();
    aRow.sIpAddress = row[8].as<std::string>();
    aRow.sTimestamp = row[9].as<std::string>();
    vRows.push_back(std::move(aRow));
  }
  return vRows;
}

PurgeResult AuditRepository::purgeOld(int iRetentionDays) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto delResult = txn.exec(
      "DELETE FROM audit_log "
      "WHERE timestamp < NOW() - make_interval(days => $1)",
      pqxx::params{iRetentionDays});

  PurgeResult pr;
  pr.iDeletedCount = static_cast<int64_t>(delResult.affected_rows());

  auto oldestResult = txn.exec(
      "SELECT EXTRACT(EPOCH FROM MIN(timestamp))::bigint FROM audit_log");
  txn.commit();

  if (!oldestResult.empty() && !oldestResult[0][0].is_null()) {
    pr.oOldestRemaining = std::chrono::system_clock::time_point(
        std::chrono::seconds(oldestResult[0][0].as<int64_t>()));
  }
  return pr;
}

}  // namespace dns::dal
