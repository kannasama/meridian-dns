#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <crow.h>

namespace dns::dal {
class UserPreferenceRepository;
}

namespace dns::api {
class AuthMiddleware;
}

namespace dns::api::routes {

/// Handlers for /api/v1/preferences
/// Class abbreviation: prefr
class PreferenceRoutes {
 public:
  PreferenceRoutes(dns::dal::UserPreferenceRepository& uprRepo,
                   const dns::api::AuthMiddleware& amMiddleware);
  ~PreferenceRoutes();

  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::UserPreferenceRepository& _uprRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
};

}  // namespace dns::api::routes
