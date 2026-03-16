#include "api/routes/HealthRoutes.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <crow.h>

class HealthRoutesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    _healthRoutes = std::make_unique<dns::api::routes::HealthRoutes>();
    _app = std::make_unique<crow::SimpleApp>();
    _healthRoutes->registerRoutes(*_app);
    _app->validate();
  }

  crow::response handle(const std::string& sMethod, const std::string& sUrl) {
    crow::request req;
    req.url = sUrl;
    req.raw_url = sUrl;
    if (sMethod == "GET") req.method = crow::HTTPMethod::GET;
    crow::response resp;
    _app->handle_full(req, resp);
    return resp;
  }

  std::unique_ptr<dns::api::routes::HealthRoutes> _healthRoutes;
  std::unique_ptr<crow::SimpleApp> _app;
};

TEST_F(HealthRoutesTest, LiveProbeReturnsAliveAndVersion) {
  auto resp = handle("GET", "/api/v1/health/live");
  EXPECT_EQ(resp.code, 200);
  auto j = nlohmann::json::parse(resp.body);
  EXPECT_EQ(j["status"], "alive");
  EXPECT_FALSE(j["version"].get<std::string>().empty());
}
