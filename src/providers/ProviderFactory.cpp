#include "providers/ProviderFactory.hpp"

#include <memory>
#include <string>

#include "common/Errors.hpp"
#include "providers/PowerDnsProvider.hpp"

namespace dns::providers {

std::unique_ptr<IProvider> ProviderFactory::create(const std::string& sType,
                                                   const std::string& sApiEndpoint,
                                                   const std::string& sDecryptedToken) {
  if (sType == "powerdns") {
    return std::make_unique<PowerDnsProvider>(sApiEndpoint, sDecryptedToken);
  }
  if (sType == "cloudflare" || sType == "digitalocean") {
    throw common::ValidationError(
        "PROVIDER_NOT_IMPLEMENTED",
        "Provider type '" + sType + "' is not yet implemented");
  }
  throw common::ValidationError(
      "UNKNOWN_PROVIDER_TYPE",
      "Unknown provider type: '" + sType + "'");
}

}  // namespace dns::providers
