// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "core/VariableEngine.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using dns::core::VariableEngine;

class VariableEngineListDepsTest : public ::testing::Test {
 protected:
  VariableEngine _ve;
};

TEST_F(VariableEngineListDepsTest, NoPlaceholders) {
  auto vDeps = _ve.listDependencies("192.168.1.1");
  EXPECT_TRUE(vDeps.empty());
}

TEST_F(VariableEngineListDepsTest, SinglePlaceholder) {
  auto vDeps = _ve.listDependencies("192.168.1.{{octet}}");
  ASSERT_EQ(vDeps.size(), 1u);
  EXPECT_EQ(vDeps[0], "octet");
}

TEST_F(VariableEngineListDepsTest, MultiplePlaceholders) {
  auto vDeps = _ve.listDependencies("{{prefix}}.example.{{suffix}}");
  ASSERT_EQ(vDeps.size(), 2u);
  EXPECT_EQ(vDeps[0], "prefix");
  EXPECT_EQ(vDeps[1], "suffix");
}

TEST_F(VariableEngineListDepsTest, DuplicatePlaceholders) {
  auto vDeps = _ve.listDependencies("{{x}}.{{x}}");
  ASSERT_EQ(vDeps.size(), 1u);
  EXPECT_EQ(vDeps[0], "x");
}

TEST_F(VariableEngineListDepsTest, EntireValueIsVariable) {
  auto vDeps = _ve.listDependencies("{{server_ip}}");
  ASSERT_EQ(vDeps.size(), 1u);
  EXPECT_EQ(vDeps[0], "server_ip");
}

TEST_F(VariableEngineListDepsTest, EmptyString) {
  auto vDeps = _ve.listDependencies("");
  EXPECT_TRUE(vDeps.empty());
}

TEST_F(VariableEngineListDepsTest, MalformedBracesIgnored) {
  auto vDeps = _ve.listDependencies("{not_a_var}");
  EXPECT_TRUE(vDeps.empty());
}

TEST_F(VariableEngineListDepsTest, UnderscoreAndDigitsInName) {
  auto vDeps = _ve.listDependencies("{{my_var_2}}");
  ASSERT_EQ(vDeps.size(), 1u);
  EXPECT_EQ(vDeps[0], "my_var_2");
}
