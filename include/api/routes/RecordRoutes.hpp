#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <crow.h>

namespace dns::dal {
class AuditRepository;
class RecordRepository;
class ZoneRepository;
}

namespace dns::core {
class DeploymentEngine;
class DiffEngine;
class RecordValidator;
}  // namespace dns::core

namespace dns::api {
class AuthMiddleware;
}

namespace dns::api::routes {

/// Handlers for /api/v1/zones/{id}/records and preview/push
/// Class abbreviation: rr
class RecordRoutes {
 public:
  RecordRoutes(dns::dal::RecordRepository& rrRepo,
               dns::dal::ZoneRepository& zrRepo,
               dns::dal::AuditRepository& arRepo,
               const dns::api::AuthMiddleware& amMiddleware,
               dns::core::DiffEngine& deEngine,
               dns::core::DeploymentEngine& depEngine,
               dns::core::RecordValidator& rvValidator);
  ~RecordRoutes();

  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::RecordRepository& _rrRepo;
  dns::dal::ZoneRepository& _zrRepo;
  dns::dal::AuditRepository& _arRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
  dns::core::DiffEngine& _deEngine;
  dns::core::DeploymentEngine& _depEngine;
  dns::core::RecordValidator& _rvValidator;
};

}  // namespace dns::api::routes
