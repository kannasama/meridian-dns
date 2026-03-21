#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "providers/IProvider.hpp"

namespace dns::providers {

/// Implements IProvider by spawning an external binary and communicating
/// via line-delimited JSON-RPC over stdin/stdout.
///
/// Protocol: write one JSON line to stdin, read one JSON line from stdout.
///   Request:  {"method": "listRecords", "params": {"zone": "example.com."}, "id": 1}
///   Response: {"result": [...], "id": 1}
///   Error:    {"error": {"message": "..."}, "id": 1}
///
/// Class abbreviation: spp
class SubprocessProvider : public IProvider {
 public:
  SubprocessProvider(std::string sApiEndpoint, std::string sToken,
                     nlohmann::json jDefinition);
  ~SubprocessProvider() override;

  std::string name() const override;
  common::HealthStatus testConnectivity() override;
  std::vector<common::DnsRecord> listRecords(const std::string& sZoneName) override;
  common::PushResult createRecord(const std::string& sZoneName,
                                  const common::DnsRecord& drRecord) override;
  common::PushResult updateRecord(const std::string& sZoneName,
                                  const common::DnsRecord& drRecord) override;
  bool deleteRecord(const std::string& sZoneName,
                    const std::string& sProviderRecordId) override;

 private:
  /// Invoke the binary with a single JSON-RPC call.
  /// Throws ProviderError on launch failure, write failure, no response, or malformed JSON.
  /// Returns the parsed `result` field, or throws ProviderError if `error` is present.
  nlohmann::json invoke(const std::string& sMethod,
                        const nlohmann::json& jParams) const;

  common::DnsRecord mapRecord(const nlohmann::json& jRecord) const;

  std::string _sBinaryPath;
  std::string _sToken;
  nlohmann::json _jDef;
};

}  // namespace dns::providers
