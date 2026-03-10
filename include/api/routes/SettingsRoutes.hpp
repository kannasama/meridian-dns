#pragma once

#include <crow.h>

namespace dns::dal {
class SettingsRepository;
}

namespace dns::api {
class AuthMiddleware;
}

namespace dns::core {
class MaintenanceScheduler;
}

namespace dns::api::routes {

/// Handlers for /api/v1/settings
/// Class abbreviation: st
class SettingsRoutes {
 public:
  SettingsRoutes(dns::dal::SettingsRepository& srRepo,
                 const dns::api::AuthMiddleware& amMiddleware,
                 dns::core::MaintenanceScheduler* pScheduler = nullptr);
  ~SettingsRoutes();

  /// Register settings routes on the Crow app.
  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::SettingsRepository& _srRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
  dns::core::MaintenanceScheduler* _pScheduler;
};

}  // namespace dns::api::routes
