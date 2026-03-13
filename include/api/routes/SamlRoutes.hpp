#pragma once

#include <crow.h>

namespace dns::dal {
class IdpRepository;
}
namespace dns::security {
class SamlService;
class FederatedAuthService;
}  // namespace dns::security

namespace dns::api::routes {

class SamlRoutes {
 public:
  SamlRoutes(dns::dal::IdpRepository& irRepo,
             dns::security::SamlService& ssService,
             dns::security::FederatedAuthService& fasService);
  ~SamlRoutes();
  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::IdpRepository& _irRepo;
  dns::security::SamlService& _ssService;
  dns::security::FederatedAuthService& _fasService;
};

}  // namespace dns::api::routes
