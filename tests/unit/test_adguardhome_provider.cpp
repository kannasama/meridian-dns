// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "providers/AdGuardHomeProvider.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "common/Errors.hpp"
#include "common/Types.hpp"

using dns::common::DnsRecord;
using dns::providers::AdGuardHomeProvider;

// ---------------------------------------------------------------------------
// inferType
// ---------------------------------------------------------------------------

TEST(AdGuardInferTypeTest, IPv4IsA) {
  EXPECT_EQ(AdGuardHomeProvider::inferType("192.168.1.100"), "A");
  EXPECT_EQ(AdGuardHomeProvider::inferType("10.0.0.1"), "A");
  EXPECT_EQ(AdGuardHomeProvider::inferType("255.255.255.255"), "A");
}

TEST(AdGuardInferTypeTest, IPv6IsAAAA) {
  EXPECT_EQ(AdGuardHomeProvider::inferType("::1"), "AAAA");
  EXPECT_EQ(AdGuardHomeProvider::inferType("2001:db8::1"), "AAAA");
  EXPECT_EQ(AdGuardHomeProvider::inferType("fe80::1"), "AAAA");
}

TEST(AdGuardInferTypeTest, HostnameIsCNAME) {
  EXPECT_EQ(AdGuardHomeProvider::inferType("host.local"), "CNAME");
  EXPECT_EQ(AdGuardHomeProvider::inferType("nas.home.arpa"), "CNAME");
  EXPECT_EQ(AdGuardHomeProvider::inferType("printer"), "CNAME");
}

// ---------------------------------------------------------------------------
// encodeRecordId / decodeRecordId
// ---------------------------------------------------------------------------

TEST(AdGuardRecordIdTest, EncodeDecodeRoundTrip) {
  auto sId = AdGuardHomeProvider::encodeRecordId("www.example.com", "192.168.1.1");
  EXPECT_EQ(sId, "www.example.com|192.168.1.1");

  auto [sDomain, sAnswer] = AdGuardHomeProvider::decodeRecordId(sId);
  EXPECT_EQ(sDomain, "www.example.com");
  EXPECT_EQ(sAnswer, "192.168.1.1");
}

TEST(AdGuardRecordIdTest, DecodeThrowsOnMissingSeparator) {
  EXPECT_THROW(AdGuardHomeProvider::decodeRecordId("nodomain"), dns::common::ProviderError);
}

TEST(AdGuardRecordIdTest, EncodeWithCnameAnswer) {
  auto sId = AdGuardHomeProvider::encodeRecordId("www.example.com", "host.local");
  auto [sDomain, sAnswer] = AdGuardHomeProvider::decodeRecordId(sId);
  EXPECT_EQ(sDomain, "www.example.com");
  EXPECT_EQ(sAnswer, "host.local");
}

// ---------------------------------------------------------------------------
// parseRewritesResponse
// ---------------------------------------------------------------------------

TEST(AdGuardParseTest, EmptyArray) {
  auto vRecords = AdGuardHomeProvider::parseRewritesResponse("[]");
  EXPECT_TRUE(vRecords.empty());
}

TEST(AdGuardParseTest, SingleIPv4Rewrite) {
  std::string sJson = R"([{"domain": "www.example.com", "answer": "192.168.1.100"}])";
  auto vRecords = AdGuardHomeProvider::parseRewritesResponse(sJson);
  ASSERT_EQ(vRecords.size(), 1u);
  EXPECT_EQ(vRecords[0].sName, "www.example.com.");
  EXPECT_EQ(vRecords[0].sType, "A");
  EXPECT_EQ(vRecords[0].sValue, "192.168.1.100");
  EXPECT_EQ(vRecords[0].uTtl, 0u);
  EXPECT_EQ(vRecords[0].sProviderRecordId, "www.example.com|192.168.1.100");
}

TEST(AdGuardParseTest, SingleIPv6Rewrite) {
  std::string sJson = R"([{"domain": "ipv6.example.com", "answer": "::1"}])";
  auto vRecords = AdGuardHomeProvider::parseRewritesResponse(sJson);
  ASSERT_EQ(vRecords.size(), 1u);
  EXPECT_EQ(vRecords[0].sType, "AAAA");
  EXPECT_EQ(vRecords[0].sValue, "::1");
}

TEST(AdGuardParseTest, CnameRewriteGetsTrailingDot) {
  std::string sJson = R"([{"domain": "alias.example.com", "answer": "host.local"}])";
  auto vRecords = AdGuardHomeProvider::parseRewritesResponse(sJson);
  ASSERT_EQ(vRecords.size(), 1u);
  EXPECT_EQ(vRecords[0].sType, "CNAME");
  EXPECT_EQ(vRecords[0].sValue, "host.local.");
  EXPECT_EQ(vRecords[0].sName, "alias.example.com.");
  EXPECT_EQ(vRecords[0].sProviderRecordId, "alias.example.com|host.local");
}

TEST(AdGuardParseTest, MultipleRewrites) {
  std::string sJson = R"([
    {"domain": "a.example.com", "answer": "10.0.0.1"},
    {"domain": "b.example.com", "answer": "::2"},
    {"domain": "c.example.com", "answer": "nas.local"}
  ])";
  auto vRecords = AdGuardHomeProvider::parseRewritesResponse(sJson);
  ASSERT_EQ(vRecords.size(), 3u);
  EXPECT_EQ(vRecords[0].sType, "A");
  EXPECT_EQ(vRecords[1].sType, "AAAA");
  EXPECT_EQ(vRecords[2].sType, "CNAME");
}

TEST(AdGuardParseTest, SkipsEntryWithEmptyDomain) {
  std::string sJson = R"([{"domain": "", "answer": "192.168.1.1"}])";
  auto vRecords = AdGuardHomeProvider::parseRewritesResponse(sJson);
  EXPECT_TRUE(vRecords.empty());
}

TEST(AdGuardParseTest, SkipsEntryWithEmptyAnswer) {
  std::string sJson = R"([{"domain": "www.example.com", "answer": ""}])";
  auto vRecords = AdGuardHomeProvider::parseRewritesResponse(sJson);
  EXPECT_TRUE(vRecords.empty());
}

// ---------------------------------------------------------------------------
// prepareDesiredRecords
// ---------------------------------------------------------------------------

class AdGuardPrepareTest : public ::testing::Test {
 protected:
  AdGuardHomeProvider provider{"http://localhost:3000",
                                R"({"username":"admin","password":"secret"})"};

  DnsRecord makeRecord(const std::string& sName, const std::string& sType,
                       bool bEnabled, const std::string& sAnswer) {
    DnsRecord dr;
    dr.sName = sName;
    dr.sType = sType;
    dr.sValue = "original-value";
    dr.jProviderMeta = {{"aghdns_enabled", bEnabled}};
    if (!sAnswer.empty()) dr.jProviderMeta["aghdns_answer"] = sAnswer;
    return dr;
  }
};

TEST_F(AdGuardPrepareTest, FiltersOutDisabledRecords) {
  auto dr = makeRecord("www.example.com.", "A", false, "192.168.1.1");
  auto vResult = provider.prepareDesiredRecords({dr});
  EXPECT_TRUE(vResult.empty());
}

TEST_F(AdGuardPrepareTest, FiltersOutRecordsWithoutMeta) {
  DnsRecord dr;
  dr.sName = "www.example.com.";
  dr.sType = "A";
  auto vResult = provider.prepareDesiredRecords({dr});
  EXPECT_TRUE(vResult.empty());
}

TEST_F(AdGuardPrepareTest, FiltersOutUnsupportedTypes) {
  auto mx = makeRecord("example.com.", "MX", true, "mail.example.com");
  auto txt = makeRecord("example.com.", "TXT", true, "host.local");
  auto vResult = provider.prepareDesiredRecords({mx, txt});
  EXPECT_TRUE(vResult.empty());
}

TEST_F(AdGuardPrepareTest, TransformsIPv4Answer) {
  auto dr = makeRecord("www.example.com.", "CNAME", true, "192.168.1.100");
  auto vResult = provider.prepareDesiredRecords({dr});
  ASSERT_EQ(vResult.size(), 1u);
  EXPECT_EQ(vResult[0].sType, "A");
  EXPECT_EQ(vResult[0].sValue, "192.168.1.100");
  EXPECT_EQ(vResult[0].sName, "www.example.com.");
}

TEST_F(AdGuardPrepareTest, TransformsIPv6Answer) {
  auto dr = makeRecord("www.example.com.", "A", true, "::1");
  auto vResult = provider.prepareDesiredRecords({dr});
  ASSERT_EQ(vResult.size(), 1u);
  EXPECT_EQ(vResult[0].sType, "AAAA");
  EXPECT_EQ(vResult[0].sValue, "::1");
}

TEST_F(AdGuardPrepareTest, TransformsHostnameAnswer) {
  auto dr = makeRecord("www.example.com.", "A", true, "nas.home");
  auto vResult = provider.prepareDesiredRecords({dr});
  ASSERT_EQ(vResult.size(), 1u);
  EXPECT_EQ(vResult[0].sType, "CNAME");
  EXPECT_EQ(vResult[0].sValue, "nas.home");
}

TEST_F(AdGuardPrepareTest, PreservesNameAndProviderMeta) {
  auto dr = makeRecord("svc.example.com.", "A", true, "10.0.0.5");
  dr.sProviderRecordId = "svc.example.com|10.0.0.5";
  auto vResult = provider.prepareDesiredRecords({dr});
  ASSERT_EQ(vResult.size(), 1u);
  EXPECT_EQ(vResult[0].sName, "svc.example.com.");
  EXPECT_EQ(vResult[0].sProviderRecordId, "svc.example.com|10.0.0.5");
  EXPECT_EQ(vResult[0].jProviderMeta["aghdns_answer"], "10.0.0.5");
}

TEST_F(AdGuardPrepareTest, SkipsRecordWithMissingAnswer) {
  DnsRecord dr;
  dr.sName = "www.example.com.";
  dr.sType = "A";
  dr.jProviderMeta = {{"aghdns_enabled", true}};  // no aghdns_answer key
  auto vResult = provider.prepareDesiredRecords({dr});
  EXPECT_TRUE(vResult.empty());
}
