#pragma once

#include <crow.h>

namespace dns::dal {
class ApiKeyRepository;
}

namespace dns::api {
class AuthMiddleware;
}

namespace dns::api::routes {

/// Handlers for /api/v1/api-keys
/// Class abbreviation: akr
class ApiKeyRoutes {
 public:
  ApiKeyRoutes(dns::dal::ApiKeyRepository& akrRepo,
               const dns::api::AuthMiddleware& amMiddleware);
  ~ApiKeyRoutes();
  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::ApiKeyRepository& _akrRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
};

}  // namespace dns::api::routes
