// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.
#pragma once

#include <crow.h>

namespace dns::dal {
class SnippetRepository;
class ZoneRepository;
class RecordRepository;
class AuditRepository;
}  // namespace dns::dal

namespace dns::api {
class AuthMiddleware;
}  // namespace dns::api

namespace dns::api::routes {

/// Handlers for /api/v1/snippets and /api/v1/zones/<id>/snippets/<id>/apply
/// Class abbreviation: snr
class SnippetRoutes {
 public:
  SnippetRoutes(dns::dal::SnippetRepository& srRepo,
                dns::dal::ZoneRepository& zrRepo,
                dns::dal::RecordRepository& rrRepo,
                dns::dal::AuditRepository& arRepo,
                const dns::api::AuthMiddleware& amMiddleware);
  ~SnippetRoutes();

  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::SnippetRepository& _srRepo;
  dns::dal::ZoneRepository& _zrRepo;
  dns::dal::RecordRepository& _rrRepo;
  dns::dal::AuditRepository& _arRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
};

}  // namespace dns::api::routes
