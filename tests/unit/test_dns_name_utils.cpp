// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "common/DnsNameUtils.hpp"

#include <gtest/gtest.h>
#include <string>

using dns::common::canonicalName;
using dns::common::canonicalValue;
using dns::common::ensureTrailingDot;
using dns::common::isHostnameValuedType;
using dns::common::stripTrailingDot;

// ---------------------------------------------------------------------------
// isHostnameValuedType
// ---------------------------------------------------------------------------

TEST(DnsNameUtilsTest, HostnameValuedTypesRecognised) {
  EXPECT_TRUE(isHostnameValuedType("CNAME"));
  EXPECT_TRUE(isHostnameValuedType("NS"));
  EXPECT_TRUE(isHostnameValuedType("MX"));
  EXPECT_TRUE(isHostnameValuedType("SRV"));
  EXPECT_TRUE(isHostnameValuedType("PTR"));
  EXPECT_TRUE(isHostnameValuedType("DNAME"));
}

TEST(DnsNameUtilsTest, AddressAndTextTypesAreNotHostnameValued) {
  EXPECT_FALSE(isHostnameValuedType("A"));
  EXPECT_FALSE(isHostnameValuedType("AAAA"));
  EXPECT_FALSE(isHostnameValuedType("TXT"));
  EXPECT_FALSE(isHostnameValuedType("CAA"));
  EXPECT_FALSE(isHostnameValuedType("SOA"));
  EXPECT_FALSE(isHostnameValuedType(""));
}

TEST(DnsNameUtilsTest, HostnameValuedTypeIsCaseInsensitive) {
  EXPECT_TRUE(isHostnameValuedType("cname"));
  EXPECT_TRUE(isHostnameValuedType("Mx"));
  EXPECT_FALSE(isHostnameValuedType("aaaa"));
}

// ---------------------------------------------------------------------------
// stripTrailingDot / ensureTrailingDot
// ---------------------------------------------------------------------------

TEST(DnsNameUtilsTest, StripTrailingDotRemovesExactlyOneDot) {
  EXPECT_EQ(stripTrailingDot("host.example.com."), "host.example.com");
  EXPECT_EQ(stripTrailingDot("host.example.com"), "host.example.com");
}

TEST(DnsNameUtilsTest, StripTrailingDotIsIdempotent) {
  auto sOnce = stripTrailingDot("host.example.com.");
  EXPECT_EQ(stripTrailingDot(sOnce), sOnce);
}

TEST(DnsNameUtilsTest, EnsureTrailingDotAppendsOnlyWhenMissing) {
  EXPECT_EQ(ensureTrailingDot("host.example.com"), "host.example.com.");
  EXPECT_EQ(ensureTrailingDot("host.example.com."), "host.example.com.");
}

TEST(DnsNameUtilsTest, EnsureTrailingDotIsIdempotent) {
  auto sOnce = ensureTrailingDot("host.example.com");
  EXPECT_EQ(ensureTrailingDot(sOnce), sOnce);
}

TEST(DnsNameUtilsTest, EmptyStringIsLeftAlone) {
  EXPECT_EQ(stripTrailingDot(""), "");
  EXPECT_EQ(ensureTrailingDot(""), "");
}

// The DNS root is a bare "." — stripping it to "" would lose meaning, so the
// root label is preserved by both helpers.
TEST(DnsNameUtilsTest, RootLabelIsPreserved) {
  EXPECT_EQ(stripTrailingDot("."), ".");
  EXPECT_EQ(ensureTrailingDot("."), ".");
  EXPECT_EQ(canonicalName("."), ".");
}

// ---------------------------------------------------------------------------
// canonicalName
// ---------------------------------------------------------------------------

TEST(DnsNameUtilsTest, CanonicalNameIsTrailingDotInsensitive) {
  EXPECT_EQ(canonicalName("host.example.com."), canonicalName("host.example.com"));
}

TEST(DnsNameUtilsTest, CanonicalNamePreservesCase) {
  // Case folding is deliberately out of scope — only the trailing dot differs.
  EXPECT_NE(canonicalName("Host.example.com"), canonicalName("host.example.com"));
}

// ---------------------------------------------------------------------------
// canonicalValue
// ---------------------------------------------------------------------------

TEST(DnsNameUtilsTest, CanonicalValueStripsDotForHostnameValuedTypes) {
  EXPECT_EQ(canonicalValue("CNAME", "target.example.com."),
            canonicalValue("CNAME", "target.example.com"));
  EXPECT_EQ(canonicalValue("MX", "mail.example.com."),
            canonicalValue("MX", "mail.example.com"));
}

TEST(DnsNameUtilsTest, CanonicalValueLeavesAddressValuesUntouched) {
  EXPECT_EQ(canonicalValue("A", "192.168.1.1"), "192.168.1.1");
  EXPECT_EQ(canonicalValue("AAAA", "2001:db8::1"), "2001:db8::1");
}

// A TXT record's trailing dot is real content, not DNS presentation syntax,
// so it must survive canonicalisation.
TEST(DnsNameUtilsTest, CanonicalValuePreservesTxtTrailingDot) {
  EXPECT_EQ(canonicalValue("TXT", "v=spf1 -all."), "v=spf1 -all.");
  EXPECT_NE(canonicalValue("TXT", "some text."), canonicalValue("TXT", "some text"));
}
