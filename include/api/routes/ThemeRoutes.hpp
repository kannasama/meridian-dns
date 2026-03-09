#pragma once

#include <crow.h>
#include <string>

namespace dns::api::routes {

/// Handler for GET /api/v1/themes (no auth required)
/// Reads custom theme preset JSON files from a configured directory.
class ThemeRoutes {
 public:
  explicit ThemeRoutes(const std::string& sCustomThemesDir);
  ~ThemeRoutes();

  /// Register theme route on the Crow app.
  void registerRoutes(crow::SimpleApp& app);

 private:
  std::string _sCustomThemesDir;
};

}  // namespace dns::api::routes
