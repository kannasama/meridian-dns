#include "providers/ProviderFactory.hpp"

#include <memory>
#include <string>

#include "common/Errors.hpp"
#include "providers/CloudflareProvider.hpp"
#include "providers/DigitalOceanProvider.hpp"
#include "providers/PowerDnsProvider.hpp"

namespace dns::providers {

std::unique_ptr<IProvider> ProviderFactory::create(const std::string& sType,
                                                   const std::string& sApiEndpoint,
                                                   const std::string& sDecryptedToken,
                                                   const nlohmann::json& jConfig) {
  if (sType == "powerdns") {
    return std::make_unique<PowerDnsProvider>(sApiEndpoint, sDecryptedToken, jConfig);
  }
  if (sType == "cloudflare") {
    return std::make_unique<CloudflareProvider>(sApiEndpoint, sDecryptedToken, jConfig);
  }
  if (sType == "digitalocean") {
    return std::make_unique<DigitalOceanProvider>(sApiEndpoint, sDecryptedToken, jConfig);
  }
  throw common::ValidationError(
      "UNKNOWN_PROVIDER_TYPE",
      "Unknown provider type: '" + sType + "'");
}

}  // namespace dns::providers
