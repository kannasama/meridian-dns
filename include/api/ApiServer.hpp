#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <crow.h>

namespace dns::api {
class AuthMiddleware;
}

namespace dns::api::routes {
class AuthRoutes;
class AuditRoutes;
class DeploymentRoutes;
class HealthRoutes;
class ProviderRoutes;
class SetupRoutes;
class ViewRoutes;
class ZoneRoutes;
class RecordRoutes;
class VariableRoutes;
class SnippetRoutes;
class SoaPresetRoutes;
class ZoneTemplateRoutes;
class SearchRoutes;
class TagRoutes;
class ProviderDefinitionRoutes;
class SystemLogRoutes;
}  // namespace dns::api::routes

namespace dns::api {

/// Owns the Crow application instance; registers all routes at startup.
/// Class abbreviation: api
class ApiServer {
 public:
  ApiServer(crow::SimpleApp& app,
            routes::AuthRoutes& arRoutes,
            routes::AuditRoutes& audrRoutes,
            routes::DeploymentRoutes& dplrRoutes,
            routes::HealthRoutes& hrRoutes,
            routes::ProviderRoutes& prRoutes,
            routes::SetupRoutes& srRoutes,
            routes::ViewRoutes& vrRoutes,
            routes::ZoneRoutes& zrRoutes,
            routes::RecordRoutes& rrRoutes,
            routes::VariableRoutes& varRoutes,
            routes::SnippetRoutes&      snrRoutes,
            routes::SoaPresetRoutes&    sprRoutes,
            routes::ZoneTemplateRoutes& ztrRoutes,
            routes::SearchRoutes&       srchRoutes,
            routes::TagRoutes&          tagrRoutes,
            routes::ProviderDefinitionRoutes& pdrRoutes,
            routes::SystemLogRoutes& slrrRoutes);
  ~ApiServer();

  /// Register all route handlers on the Crow app.
  void registerRoutes();

  /// Start the HTTP server. Blocks on the Crow event loop.
  void start(int iPort, int iThreads);

  /// Stop the HTTP server.
  void stop();

 private:
  crow::SimpleApp& _app;
  routes::AuthRoutes& _arRoutes;
  routes::AuditRoutes& _audrRoutes;
  routes::DeploymentRoutes& _dplrRoutes;
  routes::HealthRoutes& _hrRoutes;
  routes::ProviderRoutes& _prRoutes;
  routes::SetupRoutes& _srRoutes;
  routes::ViewRoutes& _vrRoutes;
  routes::ZoneRoutes& _zrRoutes;
  routes::RecordRoutes& _rrRoutes;
  routes::VariableRoutes& _varRoutes;
  routes::SnippetRoutes&      _snrRoutes;
  routes::SoaPresetRoutes&    _sprRoutes;
  routes::ZoneTemplateRoutes& _ztrRoutes;
  routes::SearchRoutes&       _srchRoutes;
  routes::TagRoutes&          _tagrRoutes;
  routes::ProviderDefinitionRoutes& _pdrRoutes;
  routes::SystemLogRoutes& _slrrRoutes;
};

}  // namespace dns::api
