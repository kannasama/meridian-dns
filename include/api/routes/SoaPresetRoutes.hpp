// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.
#pragma once

#include <crow.h>

namespace dns::dal {
class SoaPresetRepository;
}  // namespace dns::dal

namespace dns::api {
class AuthMiddleware;
}  // namespace dns::api

namespace dns::api::routes {

/// Handlers for /api/v1/soa-presets
/// Class abbreviation: spr
class SoaPresetRoutes {
 public:
  SoaPresetRoutes(dns::dal::SoaPresetRepository& sprRepo,
                  const dns::api::AuthMiddleware& amMiddleware);
  ~SoaPresetRoutes();

  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::SoaPresetRepository& _sprRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
};

}  // namespace dns::api::routes
