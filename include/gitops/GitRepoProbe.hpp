#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace dns::gitops {

/// Outcome of a git remote reachability probe.
/// Class abbreviation: gpr
struct GitProbeResult {
  bool bReachable = false;
  std::string sError;  // failure reason; empty when reachable
  std::chrono::system_clock::time_point tpCheckedAt;
};

/// A git remote resolved to something probeable.
/// Class abbreviation: ge
struct GitEndpoint {
  std::string sScheme;
  std::string sHost;
  int iPort = 0;
  bool bLocal = false;      // file:// or bare filesystem path
  std::string sLocalPath;   // only meaningful when bLocal
};

/// Fast reachability check for git remotes, with a short-lived per-repo cache.
///
/// Deliberately a TCP-level probe rather than a libgit2 handshake: libgit2's
/// server timeouts only govern the HTTP(S) transport, while SSH runs through
/// libssh2 and ignores them, so a handshake-based check can still block for
/// minutes on an unreachable host. This answers "can I reach the remote at
/// all", which is the failure mode that silently breaks deployments. It does
/// NOT validate credentials — the "Test connection" action remains the
/// authoritative auth check.
///
/// Class abbreviation: grp
class GitRepoProbe {
 public:
  explicit GitRepoProbe(int iTimeoutMs = 3000, int iCacheTtlSeconds = 30);

  GitRepoProbe(const GitRepoProbe&) = delete;
  GitRepoProbe& operator=(const GitRepoProbe&) = delete;

  /// Parse a git remote URL into a probeable endpoint.
  /// Handles ssh://, scp-style (git@host:path), https://, http://, file://
  /// and bare filesystem paths. Returns nullopt when nothing can be derived.
  static std::optional<GitEndpoint> parseRemoteUrl(const std::string& sRemoteUrl);

  /// Probe a remote URL directly, bypassing any cache.
  static GitProbeResult probeEndpoint(const std::string& sRemoteUrl, int iTimeoutMs);

  /// Cache-aware probe. Returns a warm result when one is within the TTL.
  GitProbeResult probe(int64_t iRepoId, const std::string& sRemoteUrl);

  /// Forced probe; refreshes the cache entry.
  GitProbeResult probeNow(int64_t iRepoId, const std::string& sRemoteUrl);

  /// Drop a repo's cached result (e.g. after its config changes).
  void invalidate(int64_t iRepoId);

  int timeoutMs() const { return _iTimeoutMs; }

 private:
  int _iTimeoutMs;
  int _iCacheTtlSeconds;

  // Guards _mCache only. Held separately from GitRepoManager's mutex so a
  // probe never serializes behind an in-flight clone, fetch or push.
  std::mutex _mtxCache;
  std::unordered_map<int64_t, GitProbeResult> _mCache;
};

}  // namespace dns::gitops
