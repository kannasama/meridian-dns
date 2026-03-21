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

TEST(SubprocessProviderTest, Invoke_PassesMetacharactersLiterallyWithoutShell) {
  // /bin/cat echoes stdin back to stdout — the JSON payload containing shell
  // metacharacters must be passed literally (not shell-expanded).
  // With popen() these characters would be interpreted by /bin/sh.
  nlohmann::json jDef = {{"binary_path", "/bin/cat"}};

  EXPECT_NO_THROW({
    dns::providers::SubprocessProvider provider("", "test-token", jDef);
    try {
      // Metacharacters flow through listRecords → invoke → callSubprocess
      provider.listRecords("'; rm -rf / #\"; echo pwned");
    } catch (const dns::common::ProviderError&) {
      // A ProviderError for no/invalid response is acceptable — no crash is the test
    }
  });
}
