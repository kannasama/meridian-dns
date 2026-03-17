#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <crow.h>
#include <string>

namespace dns::api {

/// Serves static files from the UI build directory.
/// Falls back to index.html for SPA history mode routing.
/// Class abbreviation: sfh
class StaticFileHandler {
 public:
  /// @param sUiDir Absolute path to the directory containing built UI assets.
  explicit StaticFileHandler(const std::string& sUiDir);

  /// Register catch-all route on the Crow app.
  void registerRoutes(crow::SimpleApp& app);

  /// Set the setup JWT token to inject into index.html during setup mode.
  void setSetupToken(const std::string& sToken);

  /// Clear the setup token (called after setup completes).
  void clearSetupToken() { _sSetupToken.clear(); }

 private:
  std::string _sUiDir;
  std::string _sSetupToken;  ///< Empty = no injection.

  /// Read a file from disk and return its contents (empty string on failure).
  static std::string readFile(const std::string& sPath);

  /// Apply baseline security headers to static file responses.
  static void applyStaticSecurityHeaders(crow::response& res);

  /// Guess MIME type from file extension.
  static std::string mimeType(const std::string& sPath);
};

}  // namespace dns::api
