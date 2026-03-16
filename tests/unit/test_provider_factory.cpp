// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "providers/ProviderFactory.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>

#include "common/Errors.hpp"
#include "providers/PowerDnsProvider.hpp"

using dns::providers::ProviderFactory;

TEST(ProviderFactoryTest, CreatesPowerDnsProvider) {
  auto upProvider = ProviderFactory::create("powerdns", "http://localhost:8081", "test-key");
  ASSERT_NE(upProvider, nullptr);
  EXPECT_EQ(upProvider->name(), "powerdns");
}

TEST(ProviderFactoryTest, CreatesPowerDnsProviderWithConfig) {
  nlohmann::json jConfig = {{"server_id", "localhost"}};
  auto upProvider = ProviderFactory::create("powerdns", "http://localhost:8081",
                                            "test-key", jConfig);
  ASSERT_NE(upProvider, nullptr);
  EXPECT_EQ(upProvider->name(), "powerdns");
}

TEST(ProviderFactoryTest, CreatesCloudflareProvider) {
  nlohmann::json jConfig = {{"account_id", "abc123"}};
  auto upProvider = ProviderFactory::create("cloudflare", "https://api.cloudflare.com",
                                            "key", jConfig);
  ASSERT_NE(upProvider, nullptr);
  EXPECT_EQ(upProvider->name(), "cloudflare");
}

TEST(ProviderFactoryTest, CreatesDigitalOceanProvider) {
  auto upProvider = ProviderFactory::create("digitalocean", "https://api.digitalocean.com",
                                            "key");
  ASSERT_NE(upProvider, nullptr);
  EXPECT_EQ(upProvider->name(), "digitalocean");
}

TEST(ProviderFactoryTest, UnknownTypeThrows) {
  EXPECT_THROW(ProviderFactory::create("unknown", "http://localhost", "key"),
               dns::common::ValidationError);
}
