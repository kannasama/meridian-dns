// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "dal/SystemConfigRepository.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include <pqxx/pqxx>

#include "dal/ConnectionPool.hpp"

namespace dns::dal {

SystemConfigRepository::SystemConfigRepository(ConnectionPool& cpPool)
    : _cpPool(cpPool) {}

SystemConfigRepository::~SystemConfigRepository() = default;

std::string SystemConfigRepository::getAndIncrementSerial() {
  // Compute today's UTC date as YYYYMMDD
  auto now = std::chrono::system_clock::now();
  auto tt  = std::chrono::system_clock::to_time_t(now);
  std::tm tmUtc{};
  gmtime_r(&tt, &tmUtc);
  char sDateBuf[9];
  std::strftime(sDateBuf, sizeof(sDateBuf), "%Y%m%d", &tmUtc);
  std::string sTodayDate(sDateBuf);

  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  // Lock both rows atomically
  auto result = txn.exec(
      "SELECT key, value FROM system_config "
      "WHERE key IN ('serial_counter_date', 'serial_counter_seq') "
      "ORDER BY key FOR UPDATE");

  std::string sStoredDate;
  int iSeq = 0;
  for (const auto& row : result) {
    std::string sKey = row["key"].as<std::string>();
    if (sKey == "serial_counter_date") sStoredDate = row["value"].as<std::string>();
    if (sKey == "serial_counter_seq")  iSeq = row["value"].as<int>();
  }

  // New day → reset; same day → increment
  if (sStoredDate != sTodayDate) {
    sStoredDate = sTodayDate;
    iSeq = 0;
  } else {
    iSeq++;
  }
  if (iSeq > 99) {
    throw std::runtime_error("SOA serial suffix exhausted for today (max 99 per day)");
  }

  txn.exec("UPDATE system_config SET value=$1, updated_at=NOW() "
           "WHERE key='serial_counter_date'",
           pqxx::params{sTodayDate});
  txn.exec("UPDATE system_config SET value=$1, updated_at=NOW() "
           "WHERE key='serial_counter_seq'",
           pqxx::params{std::to_string(iSeq)});
  txn.commit();

  // Format: YYYYMMDDNN (10 chars, NN zero-padded to 2 digits)
  std::ostringstream oss;
  oss << sTodayDate << std::setfill('0') << std::setw(2) << iSeq;
  return oss.str();
}

}  // namespace dns::dal
