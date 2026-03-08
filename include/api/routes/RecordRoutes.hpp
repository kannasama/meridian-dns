#pragma once

#include <crow.h>

namespace dns::dal {
class RecordRepository;
class ZoneRepository;
}

namespace dns::core {
class DeploymentEngine;
class DiffEngine;
}  // namespace dns::core

namespace dns::api {
class AuthMiddleware;
}

namespace dns::api::routes {

/// Handlers for /api/v1/zones/{id}/records and preview/push
/// Class abbreviation: rr
class RecordRoutes {
 public:
  RecordRoutes(dns::dal::RecordRepository& rrRepo,
               dns::dal::ZoneRepository& zrRepo,
               const dns::api::AuthMiddleware& amMiddleware,
               dns::core::DiffEngine& deEngine,
               dns::core::DeploymentEngine& depEngine);
  ~RecordRoutes();

  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::RecordRepository& _rrRepo;
  dns::dal::ZoneRepository& _zrRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
  dns::core::DiffEngine& _deEngine;
  dns::core::DeploymentEngine& _depEngine;
};

}  // namespace dns::api::routes
