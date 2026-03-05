#pragma once

#include <crow.h>

namespace dns::api::routes {

/// Handler for GET /api/v1/health (no auth required)
class HealthRoutes {
 public:
  HealthRoutes();
  ~HealthRoutes();

  /// Register health route on the Crow app.
  void registerRoutes(crow::SimpleApp& app);
};

}  // namespace dns::api::routes
