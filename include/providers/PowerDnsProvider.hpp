#pragma once

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "providers/IProvider.hpp"

namespace httplib {
class Client;
}

namespace dns::providers {

/// PowerDNS REST API v1 provider implementation.
/// Class abbreviation: pdns
class PowerDnsProvider : public IProvider {
 public:
  PowerDnsProvider(std::string sApiEndpoint, std::string sToken,
                   nlohmann::json jConfig = nlohmann::json::object());
  ~PowerDnsProvider() override;

  std::string name() const override;
  common::HealthStatus testConnectivity() override;
  std::vector<common::DnsRecord> listRecords(const std::string& sZoneName) override;
  common::PushResult createRecord(const std::string& sZoneName,
                                  const common::DnsRecord& drRecord) override;
  common::PushResult updateRecord(const std::string& sZoneName,
                                  const common::DnsRecord& drRecord) override;
  bool deleteRecord(const std::string& sZoneName,
                    const std::string& sProviderRecordId) override;

  /// Parse a PowerDNS zone JSON response into DnsRecord entries.
  /// Public for unit testing; called internally by listRecords().
  static std::vector<common::DnsRecord> parseZoneResponse(const std::string& sJson);

  /// Build a synthetic provider_record_id: "name/type/value".
  static std::string makeRecordId(const std::string& sName, const std::string& sType,
                                  const std::string& sValue);

  /// Parse a synthetic provider_record_id into (name, type, value).
  /// Returns false if the format is invalid.
  static bool parseRecordId(const std::string& sId, std::string& sName,
                            std::string& sType, std::string& sValue);

 private:
  std::string _sApiEndpoint;
  std::string _sToken;
  std::string _sServerId;
  nlohmann::json _jConfig;
  std::unique_ptr<httplib::Client> _upClient;

  /// Ensure zone name ends with a dot (PowerDNS requirement).
  static std::string canonicalZone(const std::string& sZoneName);
};

}  // namespace dns::providers
