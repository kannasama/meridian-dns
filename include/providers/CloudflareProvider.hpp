#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "providers/IProvider.hpp"

namespace dns::providers {

/// Cloudflare API v4 provider implementation.
class CloudflareProvider : public IProvider {
 public:
  CloudflareProvider(std::string sApiEndpoint, std::string sToken,
                     nlohmann::json jConfig = nlohmann::json::object());
  ~CloudflareProvider() override;

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
  std::string _sApiEndpoint;
  std::string _sToken;
  std::string _sAccountId;
  nlohmann::json _jConfig;
};

}  // namespace dns::providers
