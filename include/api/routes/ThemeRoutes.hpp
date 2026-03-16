#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <crow.h>
#include <string>

namespace dns::api::routes {

/// Handler for GET /api/v1/themes (no auth required)
/// Reads custom theme preset JSON files from /var/meridian-dns/custom_themes/.
class ThemeRoutes {
 public:
  ThemeRoutes();
  ~ThemeRoutes();

  /// Register theme route on the Crow app.
  void registerRoutes(crow::SimpleApp& app);

 private:
  static constexpr const char* kCustomThemesDir = "/var/meridian-dns/custom_themes";
};

}  // namespace dns::api::routes
