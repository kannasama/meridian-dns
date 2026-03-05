#include "api/routes/HealthRoutes.hpp"

#include <crow.h>
#include <nlohmann/json.hpp>

namespace dns::api::routes {

HealthRoutes::HealthRoutes() = default;
HealthRoutes::~HealthRoutes() = default;

void HealthRoutes::registerRoutes(crow::SimpleApp& app) {
  CROW_ROUTE(app, "/api/v1/health")
      .methods(crow::HTTPMethod::GET)([](const crow::request& /*req*/) {
        nlohmann::json jResp = {{"status", "ok"}};
        auto resp = crow::response(200, jResp.dump());
        resp.set_header("Content-Type", "application/json");
        return resp;
      });
}

}  // namespace dns::api::routes
