// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "core/DiffEngine.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "common/Types.hpp"

using dns::common::DiffAction;
using dns::common::DnsRecord;
using dns::common::RecordDiff;
using dns::core::DiffEngine;

namespace {
DnsRecord makeRecord(const std::string& sName, const std::string& sType,
                     const std::string& sValue, uint32_t uTtl = 300) {
  DnsRecord dr;
  dr.sName = sName;
  dr.sType = sType;
  dr.sValue = sValue;
  dr.uTtl = uTtl;
  return dr;
}
}  // namespace

TEST(DiffEngineComputeTest, BothEmpty) {
  auto vDiffs = DiffEngine::computeDiff({}, {});
  EXPECT_TRUE(vDiffs.empty());
}

TEST(DiffEngineComputeTest, AllNew) {
  std::vector<DnsRecord> vDesired = {makeRecord("www.example.com.", "A", "1.2.3.4")};
  std::vector<DnsRecord> vLive = {};
  auto vDiffs = DiffEngine::computeDiff(vDesired, vLive);
  ASSERT_EQ(vDiffs.size(), 1u);
  EXPECT_EQ(vDiffs[0].action, DiffAction::Add);
  EXPECT_EQ(vDiffs[0].sName, "www.example.com.");
  EXPECT_EQ(vDiffs[0].sType, "A");
  EXPECT_EQ(vDiffs[0].sSourceValue, "1.2.3.4");
  EXPECT_TRUE(vDiffs[0].sProviderValue.empty());
}

TEST(DiffEngineComputeTest, AllDrift) {
  std::vector<DnsRecord> vDesired = {};
  std::vector<DnsRecord> vLive = {makeRecord("rogue.example.com.", "A", "9.9.9.9")};
  auto vDiffs = DiffEngine::computeDiff(vDesired, vLive);
  ASSERT_EQ(vDiffs.size(), 1u);
  EXPECT_EQ(vDiffs[0].action, DiffAction::Drift);
  EXPECT_EQ(vDiffs[0].sProviderValue, "9.9.9.9");
  EXPECT_TRUE(vDiffs[0].sSourceValue.empty());
}

TEST(DiffEngineComputeTest, ExactMatch) {
  auto dr = makeRecord("www.example.com.", "A", "1.2.3.4");
  auto vDiffs = DiffEngine::computeDiff({dr}, {dr});
  EXPECT_TRUE(vDiffs.empty());
}

TEST(DiffEngineComputeTest, ValueChanged) {
  auto drDesired = makeRecord("www.example.com.", "A", "1.2.3.4");
  auto drLive = makeRecord("www.example.com.", "A", "5.6.7.8");
  auto vDiffs = DiffEngine::computeDiff({drDesired}, {drLive});
  ASSERT_EQ(vDiffs.size(), 1u);
  EXPECT_EQ(vDiffs[0].action, DiffAction::Update);
  EXPECT_EQ(vDiffs[0].sSourceValue, "1.2.3.4");
  EXPECT_EQ(vDiffs[0].sProviderValue, "5.6.7.8");
}

TEST(DiffEngineComputeTest, MixedActions) {
  std::vector<DnsRecord> vDesired = {
      makeRecord("www.example.com.", "A", "1.2.3.4"),       // matches live
      makeRecord("new.example.com.", "A", "10.0.0.1"),      // add
      makeRecord("mail.example.com.", "MX", "mail2.ex."),   // update (value differs)
  };
  std::vector<DnsRecord> vLive = {
      makeRecord("www.example.com.", "A", "1.2.3.4"),       // matches desired
      makeRecord("mail.example.com.", "MX", "mail1.ex."),   // update
      makeRecord("old.example.com.", "CNAME", "legacy."),   // drift
  };
  auto vDiffs = DiffEngine::computeDiff(vDesired, vLive);
  ASSERT_EQ(vDiffs.size(), 3u);

  // Sort by name for deterministic assertions
  std::sort(vDiffs.begin(), vDiffs.end(),
            [](const RecordDiff& a, const RecordDiff& b) { return a.sName < b.sName; });

  EXPECT_EQ(vDiffs[0].action, DiffAction::Update);   // mail
  EXPECT_EQ(vDiffs[1].action, DiffAction::Add);       // new
  EXPECT_EQ(vDiffs[2].action, DiffAction::Drift);     // old
}

TEST(DiffEngineComputeTest, MultipleRecordsSameNameType) {
  // Two A records for same name (e.g., round-robin)
  std::vector<DnsRecord> vDesired = {
      makeRecord("www.example.com.", "A", "1.2.3.4"),
      makeRecord("www.example.com.", "A", "5.6.7.8"),
  };
  std::vector<DnsRecord> vLive = {
      makeRecord("www.example.com.", "A", "1.2.3.4"),
      makeRecord("www.example.com.", "A", "5.6.7.8"),
  };
  auto vDiffs = DiffEngine::computeDiff(vDesired, vLive);
  EXPECT_TRUE(vDiffs.empty());
}

TEST(DiffEngineComputeTest, MultipleRecordsSameNameTypePartialDrift) {
  std::vector<DnsRecord> vDesired = {
      makeRecord("www.example.com.", "A", "1.2.3.4"),
  };
  std::vector<DnsRecord> vLive = {
      makeRecord("www.example.com.", "A", "1.2.3.4"),
      makeRecord("www.example.com.", "A", "9.9.9.9"),  // extra — drift
  };
  auto vDiffs = DiffEngine::computeDiff(vDesired, vLive);
  ASSERT_EQ(vDiffs.size(), 1u);
  EXPECT_EQ(vDiffs[0].action, DiffAction::Drift);
  EXPECT_EQ(vDiffs[0].sProviderValue, "9.9.9.9");
}

TEST(DiffEngineFilterTest, FilterSoaAndNs) {
  std::vector<DnsRecord> vRecords = {
      makeRecord("example.com.", "SOA",
                 "ns1.example.com. admin.example.com. 1 3600 900 604800 86400"),
      makeRecord("example.com.", "NS", "ns1.example.com."),
      makeRecord("example.com.", "NS", "ns2.example.com."),
      makeRecord("www.example.com.", "A", "1.2.3.4"),
  };

  // Filter both
  auto vFiltered = DiffEngine::filterRecordTypes(vRecords, false, false);
  ASSERT_EQ(vFiltered.size(), 1u);
  EXPECT_EQ(vFiltered[0].sType, "A");

  // Keep SOA, filter NS
  vFiltered = DiffEngine::filterRecordTypes(vRecords, true, false);
  ASSERT_EQ(vFiltered.size(), 2u);

  // Keep both
  vFiltered = DiffEngine::filterRecordTypes(vRecords, true, true);
  ASSERT_EQ(vFiltered.size(), 4u);
}

TEST(DiffEngineComputeTest, IgnoresProviderMetaInComparison) {
  // Same record with different provider metadata → no diff
  DnsRecord drDesired;
  drDesired.sName = "www.example.com";
  drDesired.sType = "A";
  drDesired.uTtl = 300;
  drDesired.sValue = "1.2.3.4";
  drDesired.jProviderMeta = {{"proxied", true}};

  DnsRecord drLive;
  drLive.sProviderRecordId = "rec-1";
  drLive.sName = "www.example.com";
  drLive.sType = "A";
  drLive.uTtl = 300;
  drLive.sValue = "1.2.3.4";
  drLive.jProviderMeta = {{"proxied", false}};

  auto vDiffs = DiffEngine::computeDiff({drDesired}, {drLive});
  EXPECT_TRUE(vDiffs.empty());
}

TEST(DiffEngineComputeTest, PropagatesProviderMetaInDiff) {
  DnsRecord drDesired;
  drDesired.sName = "app.example.com";
  drDesired.sType = "A";
  drDesired.uTtl = 300;
  drDesired.sValue = "10.0.0.1";
  drDesired.jProviderMeta = {{"proxied", true}};

  auto vDiffs = DiffEngine::computeDiff({drDesired}, {});
  ASSERT_EQ(vDiffs.size(), 1u);
  EXPECT_EQ(vDiffs[0].action, DiffAction::Add);
  EXPECT_TRUE(vDiffs[0].jProviderMeta.value("proxied", false));
}

TEST(DiffEngineComputeTest, MultipleTxtRecordsBothChanged) {
  // Two TXT records at @ (SPF + verification token), both values updated
  // Must produce correct 1:1 pairings, not cross-match
  std::vector<DnsRecord> vDesired = {
      makeRecord("example.com.", "TXT", "v=spf1 include:_spf.google.com ~all"),
      makeRecord("example.com.", "TXT", "google-site-verification=NEW_TOKEN"),
  };
  std::vector<DnsRecord> vLive = {
      makeRecord("example.com.", "TXT", "v=spf1 include:_spf.old.com ~all"),
      makeRecord("example.com.", "TXT", "google-site-verification=OLD_TOKEN"),
  };
  auto vDiffs = DiffEngine::computeDiff(vDesired, vLive);

  // Should produce exactly 2 updates with no drift
  ASSERT_EQ(vDiffs.size(), 2u);

  std::sort(vDiffs.begin(), vDiffs.end(),
            [](const RecordDiff& a, const RecordDiff& b) {
              return a.sSourceValue < b.sSourceValue;
            });

  // google-site-verification update
  EXPECT_EQ(vDiffs[0].action, DiffAction::Update);
  EXPECT_EQ(vDiffs[0].sSourceValue, "google-site-verification=NEW_TOKEN");
  EXPECT_EQ(vDiffs[0].sProviderValue, "google-site-verification=OLD_TOKEN");

  // SPF update
  EXPECT_EQ(vDiffs[1].action, DiffAction::Update);
  EXPECT_EQ(vDiffs[1].sSourceValue, "v=spf1 include:_spf.google.com ~all");
  EXPECT_EQ(vDiffs[1].sProviderValue, "v=spf1 include:_spf.old.com ~all");
}

TEST(DiffEngineComputeTest, MultipleTxtRecordsOneChanged) {
  // Two TXT records at @: SPF unchanged, verification token updated
  std::vector<DnsRecord> vDesired = {
      makeRecord("example.com.", "TXT", "v=spf1 include:_spf.google.com ~all"),
      makeRecord("example.com.", "TXT", "google-site-verification=NEW_TOKEN"),
  };
  std::vector<DnsRecord> vLive = {
      makeRecord("example.com.", "TXT", "v=spf1 include:_spf.google.com ~all"),
      makeRecord("example.com.", "TXT", "google-site-verification=OLD_TOKEN"),
  };
  auto vDiffs = DiffEngine::computeDiff(vDesired, vLive);

  // Only 1 update (verification token), SPF exact-matches
  ASSERT_EQ(vDiffs.size(), 1u);
  EXPECT_EQ(vDiffs[0].action, DiffAction::Update);
  EXPECT_EQ(vDiffs[0].sSourceValue, "google-site-verification=NEW_TOKEN");
  EXPECT_EQ(vDiffs[0].sProviderValue, "google-site-verification=OLD_TOKEN");
}

TEST(DiffEngineComputeTest, AutoTtlNormalizedDesiredMatchesLive) {
  // When DiffEngine::preview() normalizes desired TTL to 1 for Cloudflare,
  // the computeDiff should see no diff (name+type+value match)
  auto drDesired = makeRecord("www.example.com.", "A", "1.2.3.4", 1);  // normalized
  drDesired.jProviderMeta = {{"proxied", true}, {"auto_ttl", true}};

  auto drLive = makeRecord("www.example.com.", "A", "1.2.3.4", 1);  // Cloudflare returns 1
  drLive.jProviderMeta = {{"proxied", true}};

  auto vDiffs = DiffEngine::computeDiff({drDesired}, {drLive});
  EXPECT_TRUE(vDiffs.empty());
}

TEST(DiffEngineComputeTest, AutoTtlPropagatesInAddDiff) {
  DnsRecord drDesired;
  drDesired.sName = "cdn.example.com.";
  drDesired.sType = "CNAME";
  drDesired.uTtl = 1;  // normalized for Cloudflare
  drDesired.sValue = "cdn.provider.com.";
  drDesired.jProviderMeta = {{"proxied", true}, {"auto_ttl", true}};

  auto vDiffs = DiffEngine::computeDiff({drDesired}, {});
  ASSERT_EQ(vDiffs.size(), 1u);
  EXPECT_EQ(vDiffs[0].action, DiffAction::Add);
  EXPECT_TRUE(vDiffs[0].jProviderMeta.value("auto_ttl", false));
  EXPECT_EQ(vDiffs[0].uTtl, 1u);
}

TEST(DiffEngineComputeTest, PerProviderDiffIndependent) {
  // Same desired records, different live records per provider
  std::vector<DnsRecord> vDesired = {makeRecord("www.example.com.", "A", "1.2.3.4")};

  // Provider A has the record already
  std::vector<DnsRecord> vLiveA = {makeRecord("www.example.com.", "A", "1.2.3.4")};
  vLiveA[0].sProviderRecordId = "a1";

  // Provider B does not have the record
  std::vector<DnsRecord> vLiveB = {};

  auto vDiffsA = DiffEngine::computeDiff(vDesired, vLiveA);
  auto vDiffsB = DiffEngine::computeDiff(vDesired, vLiveB);

  EXPECT_TRUE(vDiffsA.empty());     // No changes needed for provider A
  ASSERT_EQ(vDiffsB.size(), 1u);    // Add needed for provider B
  EXPECT_EQ(vDiffsB[0].action, DiffAction::Add);
}

// ── fromFqdn (inverse of toFqdn) ────────────────────────────────────

TEST(DiffEngineFromFqdnTest, ApexWithTrailingDot) {
  // Zone apex: "test.mjhnosekai.com." in zone "test.mjhnosekai.com" → "@"
  EXPECT_EQ(DiffEngine::fromFqdn("test.mjhnosekai.com.", "test.mjhnosekai.com"), "@");
}

TEST(DiffEngineFromFqdnTest, ApexZoneAlsoHasDot) {
  // Zone name already has trailing dot
  EXPECT_EQ(DiffEngine::fromFqdn("example.com.", "example.com."), "@");
}

TEST(DiffEngineFromFqdnTest, SubdomainWithTrailingDot) {
  // "vip101.test.mjhnosekai.com." in zone "test.mjhnosekai.com" → "vip101"
  EXPECT_EQ(DiffEngine::fromFqdn("vip101.test.mjhnosekai.com.", "test.mjhnosekai.com"), "vip101");
}

TEST(DiffEngineFromFqdnTest, MultiLevelSubdomain) {
  // "a.b.example.com." in zone "example.com" → "a.b"
  EXPECT_EQ(DiffEngine::fromFqdn("a.b.example.com.", "example.com"), "a.b");
}

TEST(DiffEngineFromFqdnTest, AlreadyRelative) {
  // Already a relative name (no trailing dot, doesn't match zone)
  EXPECT_EQ(DiffEngine::fromFqdn("www", "example.com"), "www");
}

TEST(DiffEngineFromFqdnTest, ExternalFqdn) {
  // FQDN that doesn't belong to the zone — returned as-is
  EXPECT_EQ(DiffEngine::fromFqdn("other.domain.com.", "example.com"), "other.domain.com.");
}

TEST(DiffEngineFromFqdnTest, RoundTripRelative) {
  // toFqdn → fromFqdn should produce the original name
  std::string sZone = "example.com";
  EXPECT_EQ(DiffEngine::fromFqdn(DiffEngine::toFqdn("www", sZone), sZone), "www");
  EXPECT_EQ(DiffEngine::fromFqdn(DiffEngine::toFqdn("@", sZone), sZone), "@");
  EXPECT_EQ(DiffEngine::fromFqdn(DiffEngine::toFqdn("sub.host", sZone), sZone), "sub.host");
}

TEST(DiffEngineFromFqdnTest, EmptyInputs) {
  EXPECT_EQ(DiffEngine::fromFqdn("", "example.com"), "");
  EXPECT_EQ(DiffEngine::fromFqdn("example.com.", ""), "example.com.");
}
