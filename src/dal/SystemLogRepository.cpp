// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "dal/SystemLogRepository.hpp"

#include "dal/ConnectionPool.hpp"

#include <pqxx/pqxx>
#include <sstream>

namespace dns::dal {

SystemLogRepository::SystemLogRepository(ConnectionPool& cpPool) : _cpPool(cpPool) {}
SystemLogRepository::~SystemLogRepository() = default;

int64_t SystemLogRepository::insert(const std::string& sCategory,
                                     const std::string& sSeverity,
                                     const std::string& sMessage,
                                     std::optional<int64_t> oZoneId,
                                     std::optional<int64_t> oProviderId,
                                     const std::optional<std::string>& osOperation,
                                     const std::optional<std::string>& osRecordName,
                                     const std::optional<std::string>& osRecordType,
                                     std::optional<bool> obSuccess,
                                     std::optional<int> oiStatusCode,
                                     const std::optional<std::string>& osDetail) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  // Convert optional values to optional strings for pqxx params
  std::optional<std::string> osZoneId, osProviderId, osSuccess, osStatusCode;
  if (oZoneId) osZoneId = std::to_string(*oZoneId);
  if (oProviderId) osProviderId = std::to_string(*oProviderId);
  if (obSuccess) osSuccess = *obSuccess ? "true" : "false";
  if (oiStatusCode) osStatusCode = std::to_string(*oiStatusCode);

  auto result = txn.exec(
      "INSERT INTO system_logs (category, severity, zone_id, provider_id, "
      "operation, record_name, record_type, success, status_code, message, detail) "
      "VALUES ($1, $2, $3::bigint, $4::bigint, $5, $6, $7, $8::boolean, "
      "$9::integer, $10, $11) RETURNING id",
      pqxx::params{sCategory, sSeverity, osZoneId, osProviderId,
                   osOperation, osRecordName, osRecordType, osSuccess,
                   osStatusCode, sMessage, osDetail});

  txn.commit();
  return result.one_row()[0].as<int64_t>();
}

std::vector<SystemLogRow> SystemLogRepository::query(
    const std::optional<std::string>& osCategory,
    const std::optional<std::string>& osSeverity,
    const std::optional<int64_t>& oZoneId,
    const std::optional<int64_t>& oProviderId,
    const std::optional<std::chrono::system_clock::time_point>& otpFrom,
    const std::optional<std::chrono::system_clock::time_point>& otpTo,
    int iLimit) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  // Build dynamic query with parameterised placeholders
  std::ostringstream sql;
  sql << "SELECT id, category, severity, zone_id, provider_id, "
         "operation, record_name, record_type, success, status_code, "
         "message, detail, EXTRACT(EPOCH FROM created_at)::bigint "
         "FROM system_logs WHERE 1=1";

  pqxx::params params;
  int iParamIdx = 1;

  if (osCategory.has_value()) {
    sql << " AND category = $" << iParamIdx++;
    params.append(*osCategory);
  }
  if (osSeverity.has_value()) {
    sql << " AND severity = $" << iParamIdx++;
    params.append(*osSeverity);
  }
  if (oZoneId.has_value()) {
    sql << " AND zone_id = $" << iParamIdx++;
    params.append(*oZoneId);
  }
  if (oProviderId.has_value()) {
    sql << " AND provider_id = $" << iParamIdx++;
    params.append(*oProviderId);
  }
  if (otpFrom.has_value()) {
    auto iEpoch = std::chrono::duration_cast<std::chrono::seconds>(
                      otpFrom->time_since_epoch())
                      .count();
    sql << " AND created_at >= to_timestamp($" << iParamIdx++ << ")";
    params.append(iEpoch);
  }
  if (otpTo.has_value()) {
    auto iEpoch = std::chrono::duration_cast<std::chrono::seconds>(
                      otpTo->time_since_epoch())
                      .count();
    sql << " AND created_at <= to_timestamp($" << iParamIdx++ << ")";
    params.append(iEpoch);
  }

  sql << " ORDER BY created_at DESC LIMIT $" << iParamIdx;
  params.append(iLimit);

  auto result = txn.exec(sql.str(), params);
  txn.commit();

  std::vector<SystemLogRow> vRows;
  vRows.reserve(result.size());
  for (const auto& row : result) {
    SystemLogRow slr;
    slr.iId = row[0].as<int64_t>();
    slr.sCategory = row[1].as<std::string>();
    slr.sSeverity = row[2].as<std::string>();
    if (!row[3].is_null()) slr.oZoneId = row[3].as<int64_t>();
    if (!row[4].is_null()) slr.oProviderId = row[4].as<int64_t>();
    if (!row[5].is_null()) slr.osOperation = row[5].as<std::string>();
    if (!row[6].is_null()) slr.osRecordName = row[6].as<std::string>();
    if (!row[7].is_null()) slr.osRecordType = row[7].as<std::string>();
    if (!row[8].is_null()) slr.obSuccess = row[8].as<bool>();
    if (!row[9].is_null()) slr.oiStatusCode = row[9].as<int>();
    slr.sMessage = row[10].as<std::string>();
    if (!row[11].is_null()) slr.osDetail = row[11].as<std::string>();
    slr.tpCreatedAt = std::chrono::system_clock::time_point(
        std::chrono::seconds(row[12].as<int64_t>()));
    vRows.push_back(std::move(slr));
  }
  return vRows;
}

int64_t SystemLogRepository::purge(int iRetentionDays) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  auto delResult = txn.exec(
      "DELETE FROM system_logs WHERE created_at < NOW() - make_interval(days => $1)",
      pqxx::params{iRetentionDays});
  int64_t iDeleted = static_cast<int64_t>(delResult.affected_rows());

  txn.commit();
  return iDeleted;
}

}  // namespace dns::dal
