// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "api/ApiServer.hpp"

#include "api/routes/AuditRoutes.hpp"
#include "api/routes/AuthRoutes.hpp"
#include "api/routes/DeploymentRoutes.hpp"
#include "api/routes/HealthRoutes.hpp"
#include "api/routes/ProviderRoutes.hpp"
#include "api/routes/RecordRoutes.hpp"
#include "api/routes/SetupRoutes.hpp"
#include "api/routes/VariableRoutes.hpp"
#include "api/routes/ViewRoutes.hpp"
#include "api/routes/ZoneRoutes.hpp"
#include "api/routes/SnippetRoutes.hpp"
#include "api/routes/SoaPresetRoutes.hpp"
#include "api/routes/ZoneTemplateRoutes.hpp"

namespace dns::api {

ApiServer::ApiServer(crow::SimpleApp& app,
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
                     routes::ZoneTemplateRoutes& ztrRoutes)
    : _app(app),
      _arRoutes(arRoutes),
      _audrRoutes(audrRoutes),
      _dplrRoutes(dplrRoutes),
      _hrRoutes(hrRoutes),
      _prRoutes(prRoutes),
      _srRoutes(srRoutes),
      _vrRoutes(vrRoutes),
      _zrRoutes(zrRoutes),
      _rrRoutes(rrRoutes),
      _varRoutes(varRoutes),
      _snrRoutes(snrRoutes),
      _sprRoutes(sprRoutes),
      _ztrRoutes(ztrRoutes) {}

ApiServer::~ApiServer() = default;

void ApiServer::registerRoutes() {
  _hrRoutes.registerRoutes(_app);
  _arRoutes.registerRoutes(_app);
  _audrRoutes.registerRoutes(_app);
  _dplrRoutes.registerRoutes(_app);
  _prRoutes.registerRoutes(_app);
  _srRoutes.registerRoutes(_app);
  _vrRoutes.registerRoutes(_app);
  _zrRoutes.registerRoutes(_app);
  _rrRoutes.registerRoutes(_app);
  _varRoutes.registerRoutes(_app);
  _snrRoutes.registerRoutes(_app);
  _sprRoutes.registerRoutes(_app);
  _ztrRoutes.registerRoutes(_app);
}

void ApiServer::start(int iPort, int iThreads) {
  _app.port(iPort).multithreaded().concurrency(iThreads).run();
}

void ApiServer::stop() {
  _app.stop();
}

}  // namespace dns::api
