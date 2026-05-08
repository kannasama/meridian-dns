// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "providers/AdGuardHomeProvider.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "common/Errors.hpp"
#include "common/Logger.hpp"

namespace dns::providers {

using json = nlohmann::json;

AdGuardHomeProvider::AdGuardHomeProvider(std::string sApiEndpoint, std::string sToken,
                                         nlohmann::json jConfig)
    : _sApiEndpoint(std::move(sApiEndpoint)),
      _jConfig(std::move(jConfig)) {
  while (!_sApiEndpoint.empty() && _sApiEndpoint.back() == '/')
    _sApiEndpoint.pop_back();

  try {
    auto jCreds = json::parse(sToken);
    _sUsername = jCreds.value("username", "");
    _sPassword = jCreds.value("password", "");
  } catch (...) {
    _sUsername = "";
    _sPassword = "";
  }

  _upClient = std::make_unique<httplib::Client>(_sApiEndpoint);
  if (!_jConfig.value("verify_ssl", true)) {
    _upClient->enable_server_certificate_verification(false);
  }
  _upClient->set_basic_auth(_sUsername, _sPassword);
  _upClient->set_connection_timeout(10);
  _upClient->set_read_timeout(30);
  _upClient->set_default_headers({{"Content-Type", "application/json"}});
}

AdGuardHomeProvider::~AdGuardHomeProvider() = default;

std::string AdGuardHomeProvider::name() const { return "adguardhome"; }

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

std::string AdGuardHomeProvider::inferType(const std::string& sAnswer) {
  static const std::regex rxIpv4(R"(^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}$)");
  if (std::regex_match(sAnswer, rxIpv4)) return "A";
  if (sAnswer.find(':') != std::string::npos) return "AAAA";
  return "CNAME";
}

std::string AdGuardHomeProvider::encodeRecordId(const std::string& sDomain,
                                                const std::string& sAnswer) {
  return sDomain + "|" + sAnswer;
}

std::pair<std::string, std::string> AdGuardHomeProvider::decodeRecordId(
    const std::string& sId) {
  auto nSep = sId.find('|');
  if (nSep == std::string::npos) {
    throw common::ProviderError("AGH_INVALID_RECORD_ID",
                                "Invalid AdGuard Home record ID: " + sId);
  }
  return {sId.substr(0, nSep), sId.substr(nSep + 1)};
}

std::vector<common::DnsRecord> AdGuardHomeProvider::parseRewritesResponse(
    const std::string& sJson) {
  std::vector<common::DnsRecord> vRecords;
  auto jArr = json::parse(sJson);

  for (const auto& jRec : jArr) {
    std::string sDomain = jRec.value("domain", "");
    std::string sAnswer = jRec.value("answer", "");
    if (sDomain.empty() || sAnswer.empty()) continue;

    common::DnsRecord dr;
    dr.sProviderRecordId = encodeRecordId(sDomain, sAnswer);
    dr.sType = inferType(sAnswer);
    dr.uTtl = 0;

    // Normalise to FQDN
    dr.sName = sDomain;
    if (dr.sName.back() != '.') dr.sName += '.';

    if (dr.sType == "CNAME") {
      dr.sValue = sAnswer;
      if (dr.sValue.back() != '.') dr.sValue += '.';
    } else {
      dr.sValue = sAnswer;
    }

    vRecords.push_back(std::move(dr));
  }

  return vRecords;
}

// ---------------------------------------------------------------------------
// testConnectivity
// ---------------------------------------------------------------------------

common::HealthStatus AdGuardHomeProvider::testConnectivity() {
  auto spLog = common::Logger::get();
  try {
    auto res = _upClient->Get("/control/status");
    if (!res) {
      spLog->warn("AdGuard Home {}: connection failed", _sApiEndpoint);
      return common::HealthStatus::Unreachable;
    }
    auto jResp = json::parse(res->body);
    if (jResp.value("running", false)) return common::HealthStatus::Ok;
    spLog->warn("AdGuard Home {}: running=false", _sApiEndpoint);
    return common::HealthStatus::Degraded;
  } catch (const std::exception& ex) {
    spLog->error("AdGuard Home {}: connectivity test failed: {}", _sApiEndpoint, ex.what());
    return common::HealthStatus::Unreachable;
  }
}

// ---------------------------------------------------------------------------
// listRecords
// ---------------------------------------------------------------------------

std::vector<common::DnsRecord> AdGuardHomeProvider::listRecords(
    const std::string& /*sZoneName*/) {
  auto spLog = common::Logger::get();
  auto res = _upClient->Get("/control/rewrite/list");
  if (!res) {
    throw common::ProviderError("AGH_UNREACHABLE",
                                "Failed to connect to AdGuard Home at " + _sApiEndpoint);
  }
  if (res->status != 200) {
    throw common::ProviderError("AGH_LIST_FAILED",
                                "AdGuard Home returned status " + std::to_string(res->status));
  }

  auto vRecords = parseRewritesResponse(res->body);
  spLog->debug("AdGuard Home: listed {} rewrites from {}", vRecords.size(), _sApiEndpoint);
  return vRecords;
}

// ---------------------------------------------------------------------------
// createRecord
// ---------------------------------------------------------------------------

common::PushResult AdGuardHomeProvider::createRecord(const std::string& /*sZoneName*/,
                                                      const common::DnsRecord& drRecord) {
  auto spLog = common::Logger::get();

  // Strip trailing dots before sending
  std::string sDomain = drRecord.sName;
  if (!sDomain.empty() && sDomain.back() == '.') sDomain.pop_back();

  std::string sAnswer = drRecord.sValue;
  if (!sAnswer.empty() && sAnswer.back() == '.') sAnswer.pop_back();

  json jBody = {{"domain", sDomain}, {"answer", sAnswer}};
  auto res = _upClient->Post("/control/rewrite/add", jBody.dump(), "application/json");
  if (!res) {
    return {false, "", "Failed to connect to AdGuard Home"};
  }
  if (res->status != 200) {
    return {false, "", "AdGuard Home returned status " + std::to_string(res->status)};
  }

  std::string sNewId = encodeRecordId(sDomain, sAnswer);
  spLog->info("AdGuard Home: created rewrite {} -> {} at {}", sDomain, sAnswer, _sApiEndpoint);
  return {true, sNewId, ""};
}

// ---------------------------------------------------------------------------
// updateRecord (delete-then-add)
// ---------------------------------------------------------------------------

common::PushResult AdGuardHomeProvider::updateRecord(const std::string& sZoneName,
                                                      const common::DnsRecord& drRecord) {
  auto delResult = deleteRecord(sZoneName, drRecord.sProviderRecordId);
  if (!delResult.bSuccess) return delResult;
  return createRecord(sZoneName, drRecord);
}

// ---------------------------------------------------------------------------
// deleteRecord
// ---------------------------------------------------------------------------

common::PushResult AdGuardHomeProvider::deleteRecord(const std::string& /*sZoneName*/,
                                                      const std::string& sProviderRecordId) {
  auto spLog = common::Logger::get();

  auto [sDomain, sAnswer] = decodeRecordId(sProviderRecordId);

  json jBody = {{"domain", sDomain}, {"answer", sAnswer}};
  auto res = _upClient->Post("/control/rewrite/delete", jBody.dump(), "application/json");
  if (!res) {
    return {false, "", "Failed to connect to AdGuard Home"};
  }
  if (res->status != 200) {
    return {false, "", "AdGuard Home returned status " + std::to_string(res->status)};
  }

  spLog->info("AdGuard Home: deleted rewrite {} -> {} from {}", sDomain, sAnswer, _sApiEndpoint);
  return {true, sProviderRecordId, ""};
}

// ---------------------------------------------------------------------------
// prepareDesiredRecords
// ---------------------------------------------------------------------------

std::vector<common::DnsRecord> AdGuardHomeProvider::prepareDesiredRecords(
    const std::vector<common::DnsRecord>& vDesired) const {
  std::vector<common::DnsRecord> vResult;

  for (const auto& dr : vDesired) {
    if (dr.jProviderMeta.is_null() || !dr.jProviderMeta.value("aghdns_enabled", false))
      continue;
    if (dr.sType != "A" && dr.sType != "AAAA" && dr.sType != "CNAME") continue;

    std::string sAnswer = dr.jProviderMeta.value("aghdns_answer", "");
    if (sAnswer.empty()) continue;

    common::DnsRecord transformed = dr;
    transformed.sType = inferType(sAnswer);
    transformed.sValue = sAnswer;

    vResult.push_back(std::move(transformed));
  }

  return vResult;
}

}  // namespace dns::providers
