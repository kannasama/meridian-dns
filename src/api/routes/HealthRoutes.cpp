#include "api/routes/HealthRoutes.hpp"

#include "api/RouteHelpers.hpp"

#include <crow.h>
#include <nlohmann/json.hpp>

namespace dns::api::routes {

HealthRoutes::HealthRoutes() = default;
HealthRoutes::~HealthRoutes() = default;

void HealthRoutes::registerRoutes(crow::SimpleApp& app) {
  CROW_ROUTE(app, "/api/v1/health")
      .methods(crow::HTTPMethod::GET)([](const crow::request& /*req*/) {
        return jsonResponse(200, {{"status", "ok"}});
      });
}

}  // namespace dns::api::routes
