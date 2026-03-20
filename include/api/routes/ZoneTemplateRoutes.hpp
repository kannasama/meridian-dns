// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.
#pragma once

#include <crow.h>

namespace dns::dal {
class ZoneTemplateRepository;
class SnippetRepository;
class SoaPresetRepository;
class ZoneRepository;
class RecordRepository;
class AuditRepository;
}  // namespace dns::dal

namespace dns::api {
class AuthMiddleware;
}  // namespace dns::api

namespace dns::api::routes {

/// Handlers for /api/v1/templates (CRUD) and
/// /api/v1/zones/<id>/template (push, check, apply, unlink).
/// Class abbreviation: ztr
class ZoneTemplateRoutes {
 public:
  ZoneTemplateRoutes(dns::dal::ZoneTemplateRepository& ztrRepo,
                     dns::dal::SnippetRepository&      snrRepo,
                     dns::dal::SoaPresetRepository&    sprRepo,
                     dns::dal::ZoneRepository&         zrRepo,
                     dns::dal::RecordRepository&       rrRepo,
                     dns::dal::AuditRepository&        arRepo,
                     const dns::api::AuthMiddleware&   amMiddleware);
  ~ZoneTemplateRoutes();

  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::ZoneTemplateRepository& _ztrRepo;
  dns::dal::SnippetRepository&      _snrRepo;
  dns::dal::SoaPresetRepository&    _sprRepo;
  dns::dal::ZoneRepository&         _zrRepo;
  dns::dal::RecordRepository&       _rrRepo;
  dns::dal::AuditRepository&        _arRepo;
  const dns::api::AuthMiddleware&   _amMiddleware;
};

}  // namespace dns::api::routes
