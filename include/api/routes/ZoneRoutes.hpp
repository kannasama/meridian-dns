#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <crow.h>

namespace dns::dal {
class ZoneRepository;
class RecordRepository;
class AuditRepository;
class TagRepository;
}

namespace dns::core {
class DiffEngine;
class BindExporter;
}

namespace dns::api {
class AuthMiddleware;
}

namespace dns::api::routes {

/// Handlers for /api/v1/zones
/// Class abbreviation: zr
class ZoneRoutes {
 public:
  ZoneRoutes(dns::dal::ZoneRepository& zrRepo,
             const dns::api::AuthMiddleware& amMiddleware,
             dns::core::DiffEngine& deEngine,
             dns::dal::RecordRepository& rrRepo,
             dns::dal::AuditRepository& arRepo,
             dns::core::BindExporter& beExporter,
             dns::dal::TagRepository& trRepo);
  ~ZoneRoutes();

  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::ZoneRepository& _zrRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
  dns::core::DiffEngine& _deEngine;
  dns::dal::RecordRepository& _rrRepo;
  dns::dal::AuditRepository& _arRepo;
  dns::core::BindExporter& _beExporter;
  dns::dal::TagRepository& _trRepo;
};

}  // namespace dns::api::routes
