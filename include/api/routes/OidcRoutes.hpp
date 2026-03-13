#pragma once

#include <crow.h>

namespace dns::dal {
class IdpRepository;
}
namespace dns::security {
class OidcService;
class FederatedAuthService;
}  // namespace dns::security

namespace dns::api::routes {

class OidcRoutes {
 public:
  OidcRoutes(dns::dal::IdpRepository& irRepo,
             dns::security::OidcService& osService,
             dns::security::FederatedAuthService& fasService);
  ~OidcRoutes();
  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::IdpRepository& _irRepo;
  dns::security::OidcService& _osService;
  dns::security::FederatedAuthService& _fasService;
};

}  // namespace dns::api::routes
