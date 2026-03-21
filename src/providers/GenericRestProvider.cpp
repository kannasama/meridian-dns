// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "providers/GenericRestProvider.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "common/Errors.hpp"
#include "common/Logger.hpp"

namespace dns::providers {

using json = nlohmann::json;

GenericRestProvider::GenericRestProvider(std::string sApiEndpoint, std::string sToken,
                                         nlohmann::json jDefinition)
    : _sApiEndpoint(std::move(sApiEndpoint)),
      _sToken(std::move(sToken)),
      _jDef(std::move(jDefinition)),
      _upClient(nullptr) {
  while (!_sApiEndpoint.empty() && _sApiEndpoint.back() == '/')
    _sApiEndpoint.pop_back();

  _upClient = std::make_unique<httplib::Client>(_sApiEndpoint);
  _upClient->set_connection_timeout(5);
  _upClient->set_read_timeout(10);

  // Configure auth based on definition
  std::string sAuthType = "";
  if (_jDef.contains("auth") && _jDef["auth"].is_object()) {
    sAuthType = _jDef["auth"].value("type", "");
  }

  if (sAuthType == "bearer_token") {
    _upClient->set_default_headers({{"Authorization", "Bearer " + _sToken}});
  } else if (sAuthType == "api_key_header") {
    std::string sHeader = "X-Api-Key";
    if (_jDef["auth"].contains("header") && _jDef["auth"]["header"].is_string()) {
      sHeader = _jDef["auth"]["header"].get<std::string>();
    }
    _upClient->set_default_headers({{sHeader, _sToken}});
  } else if (sAuthType == "basic_auth") {
    auto iColon = _sToken.find(':');
    if (iColon != std::string::npos) {
      std::string sUser = _sToken.substr(0, iColon);
      std::string sPass = _sToken.substr(iColon + 1);
      _upClient->set_basic_auth(sUser, sPass);
    }
  }
}

GenericRestProvider::~GenericRestProvider() = default;

std::string GenericRestProvider::name() const { return "generic_rest"; }

std::string GenericRestProvider::applyTemplate(const std::string& sTemplate,
                                                const std::string& sZoneId,
                                                const std::string& sZoneName,
                                                const std::string& sRecordId) const {
  std::string sResult = sTemplate;

  auto replaceAll = [](std::string& sStr, const std::string& sFrom,
                       const std::string& sTo) {
    size_t iPos = 0;
    while ((iPos = sStr.find(sFrom, iPos)) != std::string::npos) {
      sStr.replace(iPos, sFrom.size(), sTo);
      iPos += sTo.size();
    }
  };

  replaceAll(sResult, "{zone_id}", sZoneId);
  replaceAll(sResult, "{zone_name}", sZoneName);
  replaceAll(sResult, "{record_id}", sRecordId);
  return sResult;
}

std::string GenericRestProvider::jsonPathGet(const nlohmann::json& jObj,
                                              const std::string& sPath) const {
  if (!jObj.contains(sPath)) return "";
  const auto& jVal = jObj[sPath];
  if (jVal.is_string()) return jVal.get<std::string>();
  return jVal.dump();
}

std::string GenericRestProvider::resolveZoneId(const std::string& sZoneName) {
  auto spLog = common::Logger::get();

  // Strip trailing dot for comparison
  std::string sNormName = sZoneName;
  if (!sNormName.empty() && sNormName.back() == '.') sNormName.pop_back();

  if (!_jDef.contains("endpoints") || !_jDef["endpoints"].contains("list_zones")) {
    throw common::ProviderError("GENERIC_REST_NO_LIST_ZONES",
                                "Definition missing endpoints.list_zones");
  }

  std::string sEndpoint = _jDef["endpoints"]["list_zones"].get<std::string>();
  auto res = _upClient->Get(sEndpoint);
  if (!res) {
    throw common::ProviderError("GENERIC_REST_UNREACHABLE",
                                "Failed to connect to " + _sApiEndpoint + sEndpoint);
  }
  if (res->status < 200 || res->status >= 300) {
    throw common::ProviderError("GENERIC_REST_LIST_ZONES_FAILED",
                                "list_zones returned status " + std::to_string(res->status));
  }

  std::string sZoneIdField = _jDef.value("zone_id_from", "id");
  std::string sZoneNameField = _jDef.value("zone_name_field", "name");

  json jZones = json::parse(res->body);
  if (!jZones.is_array()) {
    throw common::ProviderError("GENERIC_REST_INVALID_RESPONSE",
                                "list_zones did not return a JSON array");
  }

  for (const auto& jZone : jZones) {
    std::string sName = jsonPathGet(jZone, sZoneNameField);
    // Normalize: strip trailing dot
    if (!sName.empty() && sName.back() == '.') sName.pop_back();

    if (sName == sNormName) {
      std::string sId = jsonPathGet(jZone, sZoneIdField);
      if (sId.empty()) {
        throw common::ProviderError("GENERIC_REST_MISSING_ZONE_ID",
                                    "Zone found but zone_id_from field '" + sZoneIdField +
                                        "' is empty");
      }
      spLog->debug("GenericRest: resolved zone '{}' -> id '{}'", sNormName, sId);
      return sId;
    }
  }

  throw common::ProviderError("GENERIC_REST_ZONE_NOT_FOUND",
                              "Zone '" + sZoneName + "' not found in provider");
}

common::HealthStatus GenericRestProvider::testConnectivity() {
  auto spLog = common::Logger::get();

  std::string sEndpoint = "";
  if (_jDef.contains("endpoints")) {
    if (_jDef["endpoints"].contains("health")) {
      sEndpoint = _jDef["endpoints"]["health"].get<std::string>();
    } else if (_jDef["endpoints"].contains("list_zones")) {
      sEndpoint = _jDef["endpoints"]["list_zones"].get<std::string>();
    }
  }

  if (sEndpoint.empty()) {
    spLog->warn("GenericRest {}: no health or list_zones endpoint defined, probing /",
                _sApiEndpoint);
    sEndpoint = "/";
  }

  try {
    auto res = _upClient->Get(sEndpoint);
    if (!res) {
      spLog->warn("GenericRest {}: connection failed", _sApiEndpoint);
      return common::HealthStatus::Unreachable;
    }
    if (res->status >= 200 && res->status < 300) {
      return common::HealthStatus::Ok;
    }
    spLog->warn("GenericRest {}: unexpected status {}", _sApiEndpoint, res->status);
    return common::HealthStatus::Degraded;
  } catch (const std::exception& ex) {
    spLog->error("GenericRest {}: connectivity test failed: {}", _sApiEndpoint, ex.what());
    return common::HealthStatus::Unreachable;
  }
}

common::DnsRecord GenericRestProvider::mapRecord(const nlohmann::json& jRecord) const {
  json jMapping = json::object();
  if (_jDef.contains("response_mapping") && _jDef["response_mapping"].is_object()) {
    jMapping = _jDef["response_mapping"];
  }

  std::string sIdField = jMapping.value("record_id", "id");
  std::string sNameField = jMapping.value("name", "name");
  std::string sTypeField = jMapping.value("type", "type");
  std::string sTtlField = jMapping.value("ttl", "ttl");
  std::string sValueField = jMapping.value("value", "content");
  std::string sPriorityField = jMapping.value("priority", "priority");

  common::DnsRecord dr;
  dr.sProviderRecordId = jsonPathGet(jRecord, sIdField);
  dr.sName = jsonPathGet(jRecord, sNameField);
  dr.sType = jsonPathGet(jRecord, sTypeField);
  dr.sValue = jsonPathGet(jRecord, sValueField);

  std::string sTtlStr = jsonPathGet(jRecord, sTtlField);
  try {
    dr.uTtl = sTtlStr.empty() ? 300u : static_cast<uint32_t>(std::stoi(sTtlStr));
  } catch (const std::exception&) {
    auto spLog = dns::common::Logger::get();
    spLog->warn("GenericRestProvider::mapRecord: invalid TTL '{}', defaulting to 300", sTtlStr);
    dr.uTtl = 300u;
  }

  std::string sPriorityStr = jsonPathGet(jRecord, sPriorityField);
  try {
    dr.iPriority = sPriorityStr.empty() ? 0 : std::stoi(sPriorityStr);
  } catch (const std::exception&) {
    dr.iPriority = 0;
  }

  return dr;
}

std::vector<common::DnsRecord> GenericRestProvider::listRecords(const std::string& sZoneName) {
  auto spLog = common::Logger::get();

  if (!_jDef.contains("endpoints") || !_jDef["endpoints"].contains("list_records")) {
    throw common::ProviderError("GENERIC_REST_NO_LIST_RECORDS",
                                "Definition missing endpoints.list_records");
  }

  std::string sZoneId = resolveZoneId(sZoneName);
  std::string sEndpoint = applyTemplate(
      _jDef["endpoints"]["list_records"].get<std::string>(), sZoneId, sZoneName);

  auto res = _upClient->Get(sEndpoint);
  if (!res) {
    throw common::ProviderError("GENERIC_REST_UNREACHABLE",
                                "Failed to connect to " + _sApiEndpoint + sEndpoint);
  }
  if (res->status < 200 || res->status >= 300) {
    throw common::ProviderError("GENERIC_REST_LIST_RECORDS_FAILED",
                                "list_records returned status " + std::to_string(res->status));
  }

  auto jBody = nlohmann::json::parse(res->body, nullptr, false);
  if (jBody.is_discarded()) {
    throw common::ProviderError("GENERIC_REST_BAD_RECORDS_RESPONSE",
                                "Provider returned invalid JSON for records");
  }

  // Determine which part of the response contains the records array
  std::string sRecordsArray = "";
  if (_jDef.contains("response_mapping") && _jDef["response_mapping"].is_object() &&
      _jDef["response_mapping"].contains("records_array")) {
    sRecordsArray = _jDef["response_mapping"]["records_array"].get<std::string>();
  }

  json jRecords = json::array();
  if (!sRecordsArray.empty() && jBody.contains(sRecordsArray)) {
    jRecords = jBody[sRecordsArray];
  } else if (jBody.is_array()) {
    jRecords = jBody;
  }

  std::vector<common::DnsRecord> vRecords;
  vRecords.reserve(jRecords.size());
  for (const auto& jRecord : jRecords) {
    vRecords.push_back(mapRecord(jRecord));
  }

  spLog->debug("GenericRest: listed {} records for zone {}", vRecords.size(), sZoneName);
  return vRecords;
}

common::PushResult GenericRestProvider::createRecord(const std::string& sZoneName,
                                                      const common::DnsRecord& drRecord) {
  auto spLog = common::Logger::get();

  if (!_jDef.contains("endpoints") || !_jDef["endpoints"].contains("create_record")) {
    return {false, "", "Definition missing endpoints.create_record"};
  }

  std::string sZoneId;
  try {
    sZoneId = resolveZoneId(sZoneName);
  } catch (const common::ProviderError& ex) {
    return {false, "", ex.what()};
  }

  std::string sEndpoint = applyTemplate(
      _jDef["endpoints"]["create_record"].get<std::string>(), sZoneId, sZoneName);

  json jBody = {
      {"name", drRecord.sName},
      {"type", drRecord.sType},
      {"ttl", drRecord.uTtl},
      {"content", drRecord.sValue},
      {"priority", drRecord.iPriority},
  };

  auto res = _upClient->Post(sEndpoint, jBody.dump(), "application/json");
  if (!res) {
    return {false, "", "Failed to connect to " + _sApiEndpoint};
  }
  if (res->status != 200 && res->status != 201) {
    return {false, "", "create_record returned status " + std::to_string(res->status)};
  }

  // Extract created record ID from response
  std::string sIdField = "id";
  if (_jDef.contains("response_mapping") && _jDef["response_mapping"].contains("record_id")) {
    sIdField = _jDef["response_mapping"]["record_id"].get<std::string>();
  }

  std::string sNewId = "";
  try {
    json jResp = json::parse(res->body);
    sNewId = jsonPathGet(jResp, sIdField);
  } catch (...) {
    // Response may not be parseable JSON — leave sNewId empty
  }

  spLog->info("GenericRest: created record '{}' in zone {}", drRecord.sName, sZoneName);
  return {true, sNewId, ""};
}

common::PushResult GenericRestProvider::updateRecord(const std::string& sZoneName,
                                                      const common::DnsRecord& drRecord) {
  auto spLog = common::Logger::get();

  if (!_jDef.contains("endpoints") || !_jDef["endpoints"].contains("update_record")) {
    return {false, "", "Definition missing endpoints.update_record"};
  }

  std::string sZoneId;
  try {
    sZoneId = resolveZoneId(sZoneName);
  } catch (const common::ProviderError& ex) {
    return {false, "", ex.what()};
  }

  std::string sEndpoint = applyTemplate(
      _jDef["endpoints"]["update_record"].get<std::string>(), sZoneId, sZoneName,
      drRecord.sProviderRecordId);

  json jBody = {
      {"name", drRecord.sName},
      {"type", drRecord.sType},
      {"ttl", drRecord.uTtl},
      {"content", drRecord.sValue},
      {"priority", drRecord.iPriority},
  };

  auto res = _upClient->Put(sEndpoint, jBody.dump(), "application/json");
  if (!res) {
    return {false, "", "Failed to connect to " + _sApiEndpoint};
  }
  if (res->status != 200 && res->status != 204) {
    return {false, "", "update_record returned status " + std::to_string(res->status)};
  }

  spLog->info("GenericRest: updated record '{}' in zone {}", drRecord.sProviderRecordId,
              sZoneName);
  return {true, drRecord.sProviderRecordId, ""};
}

bool GenericRestProvider::deleteRecord(const std::string& sZoneName,
                                        const std::string& sProviderRecordId) {
  auto spLog = common::Logger::get();

  if (!_jDef.contains("endpoints") || !_jDef["endpoints"].contains("delete_record")) {
    return false;
  }

  std::string sZoneId;
  try {
    sZoneId = resolveZoneId(sZoneName);
  } catch (const common::ProviderError& e) {
    auto spLog = dns::common::Logger::get();
    spLog->warn("GenericRestProvider::deleteRecord: {}", e.what());
    return false;
  }

  std::string sEndpoint = applyTemplate(
      _jDef["endpoints"]["delete_record"].get<std::string>(), sZoneId, sZoneName,
      sProviderRecordId);

  auto res = _upClient->Delete(sEndpoint);
  if (!res) return false;
  if (res->status != 200 && res->status != 204) return false;

  spLog->info("GenericRest: deleted record '{}' from zone {}", sProviderRecordId, sZoneName);
  return true;
}

}  // namespace dns::providers
