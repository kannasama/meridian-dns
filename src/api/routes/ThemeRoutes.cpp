#include "api/routes/ThemeRoutes.hpp"

#include "api/RouteHelpers.hpp"

#include <crow.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace dns::api::routes {

ThemeRoutes::ThemeRoutes(const std::string& sCustomThemesDir)
    : _sCustomThemesDir(sCustomThemesDir) {}

ThemeRoutes::~ThemeRoutes() = default;

void ThemeRoutes::registerRoutes(crow::SimpleApp& app) {
  CROW_ROUTE(app, "/api/v1/themes")
      .methods(crow::HTTPMethod::GET)([this](const crow::request& /*req*/) {
        nlohmann::json jPresets = nlohmann::json::array();

        if (_sCustomThemesDir.empty() || !fs::is_directory(_sCustomThemesDir)) {
          return jsonResponse(200, jPresets);
        }

        for (const auto& entry : fs::directory_iterator(_sCustomThemesDir)) {
          if (!entry.is_regular_file()) continue;
          if (entry.path().extension() != ".json") continue;

          std::ifstream ifs(entry.path(), std::ios::binary);
          if (!ifs) continue;

          std::ostringstream oss;
          oss << ifs.rdbuf();

          try {
            auto jPreset = nlohmann::json::parse(oss.str());

            // Validate required fields
            if (!jPreset.contains("name") || !jPreset.contains("label") ||
                !jPreset.contains("mode") || !jPreset.contains("defaultAccent") ||
                !jPreset.contains("surface")) {
              continue;  // Skip invalid files
            }

            jPresets.push_back(std::move(jPreset));
          } catch (...) {
            continue;  // Skip files that fail to parse
          }
        }

        return jsonResponse(200, jPresets);
      });
}

}  // namespace dns::api::routes
