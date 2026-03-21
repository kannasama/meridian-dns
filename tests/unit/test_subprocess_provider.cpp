// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "providers/SubprocessProvider.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "common/Errors.hpp"

using json = nlohmann::json;
using dns::providers::SubprocessProvider;

TEST(SubprocessProviderTest, ConstructionThrowsWithoutBinaryPath) {
  EXPECT_THROW(
      SubprocessProvider("", "token", json::object()),
      dns::common::ValidationError);
}

TEST(SubprocessProviderTest, ConstructionSucceedsWithBinaryPath) {
  json jDef = {{"binary_path", "/bin/echo"}};
  EXPECT_NO_THROW(SubprocessProvider("", "token", jDef));
}

TEST(SubprocessProviderTest, NameReturnsSubprocess) {
  json jDef = {{"binary_path", "/bin/echo"}};
  SubprocessProvider spp("", "token", jDef);
  EXPECT_EQ(spp.name(), "subprocess");
}
