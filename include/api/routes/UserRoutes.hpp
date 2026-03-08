#pragma once

#include <crow.h>

namespace dns::dal {
class UserRepository;
class GroupRepository;
}  // namespace dns::dal

namespace dns::api {
class AuthMiddleware;
}

namespace dns::api::routes {

/// Handlers for /api/v1/users
/// Class abbreviation: usr
class UserRoutes {
 public:
  UserRoutes(dns::dal::UserRepository& urRepo, dns::dal::GroupRepository& grRepo,
             const dns::api::AuthMiddleware& amMiddleware);
  ~UserRoutes();
  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::UserRepository& _urRepo;
  dns::dal::GroupRepository& _grRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
};

}  // namespace dns::api::routes
