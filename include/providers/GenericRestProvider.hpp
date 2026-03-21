#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "providers/IProvider.hpp"

namespace httplib {
class Client;
}

namespace dns::providers {

/// Implements IProvider by interpreting a JSON definition document.
/// The definition describes auth, endpoints, and response mappings.
/// Class abbreviation: grp
class GenericRestProvider : public IProvider {
 public:
  GenericRestProvider(std::string sApiEndpoint, std::string sToken,
                      nlohmann::json jDefinition);
  ~GenericRestProvider() override;

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
  std::string resolveZoneId(const std::string& sZoneName);
  std::string applyTemplate(const std::string& sTemplate,
                             const std::string& sZoneId,
                             const std::string& sZoneName,
                             const std::string& sRecordId = "") const;
  common::DnsRecord mapRecord(const nlohmann::json& jRecord) const;
  std::string jsonPathGet(const nlohmann::json& jObj,
                          const std::string& sPath) const;

  std::string _sApiEndpoint;
  std::string _sToken;
  nlohmann::json _jDef;
  std::unique_ptr<httplib::Client> _upClient;
};

}  // namespace dns::providers
