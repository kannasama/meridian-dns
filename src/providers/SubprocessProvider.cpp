// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "providers/SubprocessProvider.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "common/Errors.hpp"
#include "common/Logger.hpp"

namespace dns::providers {

using json = nlohmann::json;

SubprocessProvider::SubprocessProvider(std::string /*sApiEndpoint*/, std::string sToken,
                                       nlohmann::json jDefinition)
    : _iTimeoutMs(5000),
      _sToken(std::move(sToken)),
      _jDef(std::move(jDefinition)) {
  _sBinaryPath = _jDef.value("binary_path", "");
  _iTimeoutMs = _jDef.value("timeout_ms", 5000);

  if (_sBinaryPath.empty()) {
    throw common::ValidationError("SUBPROCESS_NO_BINARY",
                                  "Subprocess provider definition missing 'binary_path'");
  }
}

SubprocessProvider::~SubprocessProvider() = default;

std::string SubprocessProvider::name() const { return "subprocess"; }

nlohmann::json SubprocessProvider::invoke(const std::string& sMethod,
                                           const nlohmann::json& jParams) const {
  auto spLog = common::Logger::get();

  // Build request JSON
  json jRequest = {{"method", sMethod}, {"params", jParams}, {"id", 1}};
  std::string sInput = jRequest.dump();

  // Escape single quotes in the JSON payload for shell embedding
  std::string sEscaped = sInput;
  size_t iPos = 0;
  while ((iPos = sEscaped.find('\'', iPos)) != std::string::npos) {
    sEscaped.replace(iPos, 1, "'\"'\"'");
    iPos += 5;
  }

  std::string sCmd = "echo '" + sEscaped + "' | " + _sBinaryPath;

  FILE* pPipe = popen(sCmd.c_str(), "r");
  if (!pPipe) {
    throw common::ProviderError("SUBPROCESS_LAUNCH_FAILED",
                                "Failed to launch subprocess: " + _sBinaryPath);
  }

  std::string sResponse;
  char aBuf[4096];
  while (fgets(aBuf, sizeof(aBuf), pPipe) != nullptr) {
    sResponse += aBuf;
    // Stop at first newline (line-delimited protocol)
    if (!sResponse.empty() && sResponse.back() == '\n') {
      break;
    }
  }
  pclose(pPipe);

  if (sResponse.empty()) {
    throw common::ProviderError("SUBPROCESS_NO_RESPONSE",
                                "Subprocess returned no output for method: " + sMethod);
  }

  // Strip trailing newline
  while (!sResponse.empty() &&
         (sResponse.back() == '\n' || sResponse.back() == '\r')) {
    sResponse.pop_back();
  }

  json jResponse = json::parse(sResponse, nullptr, false);
  if (jResponse.is_discarded()) {
    throw common::ProviderError("SUBPROCESS_MALFORMED_JSON",
                                "Subprocess returned malformed JSON for method: " + sMethod);
  }

  if (jResponse.contains("error") && !jResponse["error"].is_null()) {
    std::string sErrMsg = jResponse["error"].value("message", "Unknown subprocess error");
    throw common::ProviderError("SUBPROCESS_ERROR", sErrMsg);
  }

  if (!jResponse.contains("result")) {
    throw common::ProviderError("SUBPROCESS_NO_RESULT",
                                "Subprocess response missing 'result' field for method: " +
                                    sMethod);
  }

  spLog->debug("SubprocessProvider::invoke: method={} completed", sMethod);
  return jResponse["result"];
}

common::DnsRecord SubprocessProvider::mapRecord(const nlohmann::json& jRecord) const {
  common::DnsRecord dr;

  if (jRecord.contains("id") && jRecord["id"].is_string()) {
    dr.sProviderRecordId = jRecord["id"].get<std::string>();
  } else if (jRecord.contains("id")) {
    dr.sProviderRecordId = jRecord["id"].dump();
  }

  dr.sName = jRecord.value("name", "");
  dr.sType = jRecord.value("type", "");
  dr.sValue = jRecord.value("value", "");
  dr.uTtl = jRecord.value("ttl", 300u);
  dr.iPriority = jRecord.value("priority", 0);

  return dr;
}

common::HealthStatus SubprocessProvider::testConnectivity() {
  try {
    invoke("testConnectivity", json::object());
    return common::HealthStatus::Ok;
  } catch (const std::exception& e) {
    auto spLog = common::Logger::get();
    spLog->warn("SubprocessProvider::testConnectivity failed: {}", e.what());
    return common::HealthStatus::Unreachable;
  }
}

std::vector<common::DnsRecord> SubprocessProvider::listRecords(const std::string& sZoneName) {
  json jResult = invoke("listRecords", {{"zone", sZoneName}});

  std::vector<common::DnsRecord> vRecords;
  if (!jResult.is_array()) {
    return vRecords;
  }

  vRecords.reserve(jResult.size());
  for (const auto& jRecord : jResult) {
    vRecords.push_back(mapRecord(jRecord));
  }
  return vRecords;
}

common::PushResult SubprocessProvider::createRecord(const std::string& sZoneName,
                                                     const common::DnsRecord& drRecord) {
  try {
    json jParams = {
        {"zone", sZoneName},
        {"name", drRecord.sName},
        {"type", drRecord.sType},
        {"value", drRecord.sValue},
        {"ttl", drRecord.uTtl},
        {"priority", drRecord.iPriority},
    };

    json jResult = invoke("createRecord", jParams);

    std::string sId;
    if (jResult.contains("id") && jResult["id"].is_string()) {
      sId = jResult["id"].get<std::string>();
    } else if (jResult.contains("id")) {
      sId = jResult["id"].dump();
    }

    return {true, sId, ""};
  } catch (const std::exception& e) {
    return {false, "", e.what()};
  }
}

common::PushResult SubprocessProvider::updateRecord(const std::string& sZoneName,
                                                     const common::DnsRecord& drRecord) {
  try {
    json jParams = {
        {"zone", sZoneName},
        {"id", drRecord.sProviderRecordId},
        {"name", drRecord.sName},
        {"type", drRecord.sType},
        {"value", drRecord.sValue},
        {"ttl", drRecord.uTtl},
        {"priority", drRecord.iPriority},
    };

    invoke("updateRecord", jParams);
    return {true, drRecord.sProviderRecordId, ""};
  } catch (const std::exception& e) {
    return {false, "", e.what()};
  }
}

bool SubprocessProvider::deleteRecord(const std::string& sZoneName,
                                       const std::string& sProviderRecordId) {
  try {
    invoke("deleteRecord", {{"zone", sZoneName}, {"id", sProviderRecordId}});
    return true;
  } catch (const std::exception& e) {
    auto spLog = common::Logger::get();
    spLog->warn("SubprocessProvider::deleteRecord failed: {}", e.what());
    return false;
  }
}

}  // namespace dns::providers
