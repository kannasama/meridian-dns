#pragma once

#include <crow.h>

namespace dns::dal {
class GroupRepository;
}

namespace dns::api {
class AuthMiddleware;
}

namespace dns::api::routes {

/// Handlers for /api/v1/groups
/// Class abbreviation: grr
class GroupRoutes {
 public:
  GroupRoutes(dns::dal::GroupRepository& grRepo, const dns::api::AuthMiddleware& amMiddleware);
  ~GroupRoutes();
  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::GroupRepository& _grRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
};

}  // namespace dns::api::routes
