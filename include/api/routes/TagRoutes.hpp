#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <crow.h>

namespace dns::dal {
class TagRepository;
}

namespace dns::api {
class AuthMiddleware;
}

namespace dns::api::routes {

/// Handlers for /api/v1/tags
/// Class abbreviation: tagr
class TagRoutes {
 public:
  TagRoutes(dns::dal::TagRepository& trRepo,
            const dns::api::AuthMiddleware& amMiddleware);
  ~TagRoutes();

  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::TagRepository& _trRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
};

}  // namespace dns::api::routes
