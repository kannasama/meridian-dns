// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "providers/GenericRestProvider.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using dns::providers::GenericRestProvider;

TEST(GenericRestProviderTest, ConstructsWithMinimalDefinition) {
  json jDef = {
    {"auth", {{"type", "api_key_header"}, {"header", "X-Auth"}}},
    {"endpoints", {{"list_zones", "/zones"}, {"list_records", "/zones/{zone_id}/records"}}},
  };
  EXPECT_NO_THROW(GenericRestProvider("http://localhost:9999", "token", jDef));
}

TEST(GenericRestProviderTest, NameReturnsGenericRest) {
  GenericRestProvider grp("http://localhost:9999", "token", json::object());
  EXPECT_EQ(grp.name(), "generic_rest");
}
