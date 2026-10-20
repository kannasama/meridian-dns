// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "gitops/GitRepoProbe.hpp"

#include <gtest/gtest.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using dns::gitops::GitRepoProbe;

namespace {

/// Opens a listening TCP socket on an ephemeral loopback port.
/// Returns {fd, port}; the caller closes the fd.
std::pair<int, int> openLoopbackListener() {
  int iFd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (iFd < 0) return {-1, 0};

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;  // ephemeral

  if (::bind(iFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
      ::listen(iFd, 1) < 0) {
    ::close(iFd);
    return {-1, 0};
  }

  sockaddr_in bound{};
  socklen_t len = sizeof(bound);
  if (::getsockname(iFd, reinterpret_cast<sockaddr*>(&bound), &len) < 0) {
    ::close(iFd);
    return {-1, 0};
  }

  return {iFd, static_cast<int>(ntohs(bound.sin_port))};
}

}  // namespace

// ---------------------------------------------------------------------------
// parseRemoteUrl
// ---------------------------------------------------------------------------

TEST(GitRepoProbeParseTest, SshSchemeUrl) {
  auto oEp = GitRepoProbe::parseRemoteUrl("ssh://git@github.com/org/repo.git");
  ASSERT_TRUE(oEp.has_value());
  EXPECT_EQ(oEp->sHost, "github.com");
  EXPECT_EQ(oEp->iPort, 22);
  EXPECT_FALSE(oEp->bLocal);
}

TEST(GitRepoProbeParseTest, SshSchemeUrlWithExplicitPort) {
  auto oEp = GitRepoProbe::parseRemoteUrl("ssh://git@git.internal:2222/org/repo.git");
  ASSERT_TRUE(oEp.has_value());
  EXPECT_EQ(oEp->sHost, "git.internal");
  EXPECT_EQ(oEp->iPort, 2222);
}

TEST(GitRepoProbeParseTest, ScpStyleUrl) {
  auto oEp = GitRepoProbe::parseRemoteUrl("git@github.com:org/repo.git");
  ASSERT_TRUE(oEp.has_value());
  EXPECT_EQ(oEp->sHost, "github.com");
  EXPECT_EQ(oEp->iPort, 22);
  EXPECT_FALSE(oEp->bLocal);
}

TEST(GitRepoProbeParseTest, ScpStyleUrlWithoutUser) {
  auto oEp = GitRepoProbe::parseRemoteUrl("github.com:org/repo.git");
  ASSERT_TRUE(oEp.has_value());
  EXPECT_EQ(oEp->sHost, "github.com");
  EXPECT_EQ(oEp->iPort, 22);
}

TEST(GitRepoProbeParseTest, HttpsUrlDefaultsTo443) {
  auto oEp = GitRepoProbe::parseRemoteUrl("https://github.com/org/repo.git");
  ASSERT_TRUE(oEp.has_value());
  EXPECT_EQ(oEp->sHost, "github.com");
  EXPECT_EQ(oEp->iPort, 443);
}

TEST(GitRepoProbeParseTest, HttpsUrlWithCredentialsAndPort) {
  auto oEp = GitRepoProbe::parseRemoteUrl("https://user:token@git.internal:8443/org/repo.git");
  ASSERT_TRUE(oEp.has_value());
  EXPECT_EQ(oEp->sHost, "git.internal");
  EXPECT_EQ(oEp->iPort, 8443);
}

TEST(GitRepoProbeParseTest, HttpUrlDefaultsTo80) {
  auto oEp = GitRepoProbe::parseRemoteUrl("http://git.internal/org/repo.git");
  ASSERT_TRUE(oEp.has_value());
  EXPECT_EQ(oEp->iPort, 80);
}

TEST(GitRepoProbeParseTest, FileUrlIsLocal) {
  auto oEp = GitRepoProbe::parseRemoteUrl("file:///srv/git/repo.git");
  ASSERT_TRUE(oEp.has_value());
  EXPECT_TRUE(oEp->bLocal);
  EXPECT_EQ(oEp->sLocalPath, "/srv/git/repo.git");
}

TEST(GitRepoProbeParseTest, BarePathIsLocal) {
  auto oEp = GitRepoProbe::parseRemoteUrl("/srv/git/repo.git");
  ASSERT_TRUE(oEp.has_value());
  EXPECT_TRUE(oEp->bLocal);
  EXPECT_EQ(oEp->sLocalPath, "/srv/git/repo.git");
}

TEST(GitRepoProbeParseTest, EmptyUrlIsRejected) {
  EXPECT_FALSE(GitRepoProbe::parseRemoteUrl("").has_value());
}

// IPv6 literals are bracketed in URLs; the brackets must not leak into the host.
TEST(GitRepoProbeParseTest, Ipv6LiteralHost) {
  auto oEp = GitRepoProbe::parseRemoteUrl("ssh://git@[2001:db8::1]:2222/org/repo.git");
  ASSERT_TRUE(oEp.has_value());
  EXPECT_EQ(oEp->sHost, "2001:db8::1");
  EXPECT_EQ(oEp->iPort, 2222);
}

// ---------------------------------------------------------------------------
// probeEndpoint — reachability
// ---------------------------------------------------------------------------

TEST(GitRepoProbeTest, ListeningPortIsReachable) {
  auto [iFd, iPort] = openLoopbackListener();
  ASSERT_GT(iFd, 0) << "could not open loopback listener";

  auto sUrl = "ssh://git@127.0.0.1:" + std::to_string(iPort) + "/repo.git";
  auto result = GitRepoProbe::probeEndpoint(sUrl, 2000);
  ::close(iFd);

  EXPECT_TRUE(result.bReachable) << result.sError;
  EXPECT_TRUE(result.sError.empty());
}

TEST(GitRepoProbeTest, ClosedPortIsUnreachableWithReason) {
  // Bind then close, so the port is almost certainly free.
  auto [iFd, iPort] = openLoopbackListener();
  ASSERT_GT(iFd, 0);
  ::close(iFd);

  auto sUrl = "ssh://git@127.0.0.1:" + std::to_string(iPort) + "/repo.git";
  auto result = GitRepoProbe::probeEndpoint(sUrl, 2000);

  EXPECT_FALSE(result.bReachable);
  EXPECT_FALSE(result.sError.empty());
}

TEST(GitRepoProbeTest, UnresolvableHostIsUnreachableQuickly) {
  auto tStart = std::chrono::steady_clock::now();
  auto result = GitRepoProbe::probeEndpoint(
      "ssh://git@no-such-host.invalid/org/repo.git", 2000);
  auto iElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - tStart)
                        .count();

  EXPECT_FALSE(result.bReachable);
  EXPECT_FALSE(result.sError.empty());
  // Must not block anywhere near the OS default connect timeout.
  EXPECT_LT(iElapsedMs, 10000);
}

TEST(GitRepoProbeTest, ExistingLocalPathIsReachable) {
  auto tmp = std::filesystem::temp_directory_path() / "meridian-probe-local-test";
  std::filesystem::create_directories(tmp);

  auto result = GitRepoProbe::probeEndpoint("file://" + tmp.string(), 2000);
  std::filesystem::remove_all(tmp);

  EXPECT_TRUE(result.bReachable) << result.sError;
}

TEST(GitRepoProbeTest, MissingLocalPathIsUnreachable) {
  auto result = GitRepoProbe::probeEndpoint("file:///nonexistent/meridian/repo.git", 2000);
  EXPECT_FALSE(result.bReachable);
  EXPECT_FALSE(result.sError.empty());
}

TEST(GitRepoProbeTest, UnparseableUrlIsUnreachable) {
  auto result = GitRepoProbe::probeEndpoint("", 2000);
  EXPECT_FALSE(result.bReachable);
  EXPECT_FALSE(result.sError.empty());
}

// ---------------------------------------------------------------------------
// Caching
// ---------------------------------------------------------------------------

TEST(GitRepoProbeCacheTest, WarmResultIsReusedInsideTtl) {
  GitRepoProbe probe(2000, /*iCacheTtlSeconds=*/3600);

  auto [iFd, iPort] = openLoopbackListener();
  ASSERT_GT(iFd, 0);
  auto sUrl = "ssh://git@127.0.0.1:" + std::to_string(iPort) + "/repo.git";

  auto first = probe.probe(1, sUrl);
  ASSERT_TRUE(first.bReachable) << first.sError;

  // Take the listener away. A cached result must still report reachable.
  ::close(iFd);
  auto second = probe.probe(1, sUrl);
  EXPECT_TRUE(second.bReachable);
  EXPECT_EQ(second.tpCheckedAt, first.tpCheckedAt);
}

TEST(GitRepoProbeCacheTest, ZeroTtlAlwaysReprobes) {
  GitRepoProbe probe(2000, /*iCacheTtlSeconds=*/0);

  auto [iFd, iPort] = openLoopbackListener();
  ASSERT_GT(iFd, 0);
  auto sUrl = "ssh://git@127.0.0.1:" + std::to_string(iPort) + "/repo.git";

  auto first = probe.probe(1, sUrl);
  ASSERT_TRUE(first.bReachable) << first.sError;

  ::close(iFd);
  auto second = probe.probe(1, sUrl);
  EXPECT_FALSE(second.bReachable);
}

TEST(GitRepoProbeCacheTest, ProbeNowBypassesCache) {
  GitRepoProbe probe(2000, /*iCacheTtlSeconds=*/3600);

  auto [iFd, iPort] = openLoopbackListener();
  ASSERT_GT(iFd, 0);
  auto sUrl = "ssh://git@127.0.0.1:" + std::to_string(iPort) + "/repo.git";

  ASSERT_TRUE(probe.probe(1, sUrl).bReachable);
  ::close(iFd);

  EXPECT_FALSE(probe.probeNow(1, sUrl).bReachable);
  // The forced probe must refresh the cache, not leave the stale entry.
  EXPECT_FALSE(probe.probe(1, sUrl).bReachable);
}

TEST(GitRepoProbeCacheTest, InvalidateForcesReprobe) {
  GitRepoProbe probe(2000, /*iCacheTtlSeconds=*/3600);

  auto [iFd, iPort] = openLoopbackListener();
  ASSERT_GT(iFd, 0);
  auto sUrl = "ssh://git@127.0.0.1:" + std::to_string(iPort) + "/repo.git";

  ASSERT_TRUE(probe.probe(1, sUrl).bReachable);
  ::close(iFd);

  probe.invalidate(1);
  EXPECT_FALSE(probe.probe(1, sUrl).bReachable);
}

TEST(GitRepoProbeCacheTest, CacheIsKeyedPerRepo) {
  GitRepoProbe probe(2000, /*iCacheTtlSeconds=*/3600);

  auto [iFd, iPort] = openLoopbackListener();
  ASSERT_GT(iFd, 0);
  auto sUp = "ssh://git@127.0.0.1:" + std::to_string(iPort) + "/repo.git";

  auto [iFd2, iPort2] = openLoopbackListener();
  ASSERT_GT(iFd2, 0);
  ::close(iFd2);
  auto sDown = "ssh://git@127.0.0.1:" + std::to_string(iPort2) + "/repo.git";

  EXPECT_TRUE(probe.probe(1, sUp).bReachable);
  EXPECT_FALSE(probe.probe(2, sDown).bReachable);
  // Repo 1's warm entry must be unaffected by repo 2's failure.
  EXPECT_TRUE(probe.probe(1, sUp).bReachable);

  ::close(iFd);
}
