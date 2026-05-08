#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "providers/IProvider.hpp"

namespace httplib {
class Client;
}

namespace dns::providers {

/// AdGuard Home DNS Rewrite provider.
/// Token is JSON: {"username": "...", "password": "..."}.
/// Record ID is encoded as "domain|answer" (raw values, no trailing dots).
class AdGuardHomeProvider : public IProvider {
 public:
  AdGuardHomeProvider(std::string sApiEndpoint, std::string sToken,
                      nlohmann::json jConfig = nlohmann::json::object());
  ~AdGuardHomeProvider() override;

  std::string name() const override;
  common::HealthStatus testConnectivity() override;
  std::vector<common::DnsRecord> listRecords(const std::string& sZoneName) override;
  common::PushResult createRecord(const std::string& sZoneName,
                                  const common::DnsRecord& drRecord) override;
  common::PushResult updateRecord(const std::string& sZoneName,
                                  const common::DnsRecord& drRecord) override;
  common::PushResult deleteRecord(const std::string& sZoneName,
                                  const std::string& sProviderRecordId) override;

  std::vector<common::DnsRecord> prepareDesiredRecords(
      const std::vector<common::DnsRecord>& vDesired) const override;

  /// Parse GET /control/rewrite/list response. Public for unit testing.
  static std::vector<common::DnsRecord> parseRewritesResponse(const std::string& sJson);

  /// Encode provider record ID from domain + answer. Public for unit testing.
  static std::string encodeRecordId(const std::string& sDomain, const std::string& sAnswer);

  /// Decode provider record ID back to domain + answer. Public for unit testing.
  static std::pair<std::string, std::string> decodeRecordId(const std::string& sId);

  /// Infer DNS record type from an answer string. Public for unit testing.
  static std::string inferType(const std::string& sAnswer);

 private:
  std::string _sApiEndpoint;
  std::string _sUsername;
  std::string _sPassword;
  nlohmann::json _jConfig;
  std::unique_ptr<httplib::Client> _upClient;
};

}  // namespace dns::providers
