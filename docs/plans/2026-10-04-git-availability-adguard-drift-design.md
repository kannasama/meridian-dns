<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- Copyright (c) 2026 Meridian DNS Contributors -->
<!-- This file is part of Meridian DNS. See LICENSE for details. -->

# v1.1.4 — Git Repo Availability Gate + AdGuard Trailing-Dot Drift

**Date:** 2026-10-04
**Status:** Approved for implementation
**Branch:** `v1.1.4`

Two independent defects, designed together because both surface in the deployment
preview/push flow.

---

## Issue 1 — Unavailable git repo blocks deployment silently

### Problem

`GitRepoManager::commitZoneSnapshot()` catches every exception and emits a log line, so a
deployment against an unreachable GitOps remote reports success while its snapshot was never
written. Worse, `GitRepoMirror` sets no libgit2 connect or server timeouts, so the push blocks
the deploy thread until the OS TCP timeout before that silent failure occurs.

The operator needs the unavailability detected *quickly* and surfaced *before* committing to a
deploy, with an explicit choice to proceed.

### Decisions

| Decision | Choice |
|---|---|
| Where the operator is informed | At preview time, via a warning banner |
| Push behaviour when proceeding | Skip the commit, flag the deployment, no blocking attempt |
| Push gate | Explicit `acknowledge_git_unavailable` flag, else 409 |
| Probe mechanism | TCP pre-probe + libgit2 server timeouts |
| Freshness | Per-repo cache with a short configurable TTL |

### Detection layer

New component `GitRepoProbe` (`include/gitops/GitRepoProbe.hpp`,
`src/gitops/GitRepoProbe.cpp`), owned by `GitRepoManager`.

```cpp
struct GitProbeResult {
  bool bReachable = false;
  std::string sError;
  std::chrono::system_clock::time_point tpCheckedAt;
};
```

- `probe(iRepoId)` — cache-aware; warm result inside TTL, otherwise probes.
- `probeNow(iRepoId)` — forced refresh, for the existing "Test connection" button.
- Cache guarded by its **own** mutex, separate from `GitRepoManager::_mtx`, so a probe never
  serializes behind an in-flight clone or push.

**Probe mechanics.** Parse scheme/host/port from `sRemoteUrl`, handling `ssh://`, scp-style
`git@host:path`, `https://`, `http://`, and `file://` (local paths short-circuit to
`fs::exists`, no socket). Default ports 22/443/80 by scheme. Then non-blocking `socket()` +
`connect()` + `poll()` with the configured timeout. Reachability only — no auth, no handshake.

**Hardening the real git calls.** At `GitRepoManager` construction, set
`GIT_OPT_SET_SERVER_CONNECT_TIMEOUT` and `GIT_OPT_SET_SERVER_TIMEOUT` from settings. These are
process-wide in libgit2 and cover HTTPS only; SSH goes through libssh2 and ignores them, so
`commitSnapshot`/`pull` call `probe()` first and bail fast with a typed error rather than
entering libssh2.

### New settings

DB-configurable per the project standard (`kSettings`, `sEnvVar = ""`):

| Key | Default | Purpose |
|---|---|---|
| `gitops.probe_timeout_ms` | `3000` | TCP connect timeout |
| `gitops.probe_cache_ttl_seconds` | `30` | Warm-result window |
| `gitops.server_timeout_ms` | `20000` | libgit2 HTTPS read timeout |

### API changes

`include/common/Types.hpp` gains:

```cpp
/// Git mirror availability for a zone's preview. Class abbreviation: gs
struct GitStatus {
  bool bConfigured = false;
  int64_t iRepoId = 0;
  std::string sRepoName;
  bool bReachable = false;
  std::string sError;
  std::string sCheckedAt;
};
```

and `GitStatus gsGit;` on `PreviewResult`.

`DeploymentEngine::preview()` resolves the zone's `oGitRepoId`. Unset → `bConfigured = false`
and nothing further happens (zero cost for non-GitOps zones). Set → `probe()`.

`POST /zones/{id}/preview` response gains a sibling of `providers`:

```json
"git_status": {
  "configured": true, "repo_id": 3, "repo_name": "infra-dns",
  "reachable": false, "error": "connect timeout after 3000ms",
  "checked_at": "2026-10-04T12:00:00Z"
}
```

`DeploymentEngine::push()` takes a trailing `bool bAckGitUnavailable = false`. When the zone has
a repo assigned:

1. `probe()` the repo.
2. Unreachable, no ack → `ConflictError("GIT_REPO_UNAVAILABLE", …)` thrown **before any
   provider call**, so nothing has changed when the operator sees the 409.
3. Unreachable, with ack → skip `commitZoneSnapshot()` entirely and record the skip.
4. Reachable → unchanged.

`POST /zones/{id}/push` reads `acknowledge_git_unavailable` from the body alongside
`drift_actions`.

### Recording the skip

No migration. `deployments.snapshot` JSON gains a `git_commit` object — `{"status":
"committed"}` or `{"status": "skipped", "reason": "…", "repo_id": 3}` — plus an audit entry and
a `system_logs` row under the existing deployment category. `listByZoneId` already returns the
snapshot, so the history UI can badge it with no schema change.

### UI

- `ui/src/types/index.ts` — `GitStatus` interface, `git_status?: GitStatus` on `PreviewResult`.
- `ui/src/api/deployments.ts` — `pushZone` gains `acknowledgeGitUnavailable = false`.
- `DeploymentsView.vue`:
  1. **Per-card warning banner** where `git_status.configured && !git_status.reachable` — amber
     `Message`, `severity="warn"`, not closable, above that zone's diff sections. Amber is the
     project's semantic colour for caution; red is reserved for errors and deletes.
  2. **Push confirmation** via `useConfirm()` when that zone's git is down, passing
     `acknowledgeGitUnavailable: true` only on confirm. The "Push all" path confirms **once**
     for the whole batch, listing affected zones.
  3. **409 recovery** — a `GIT_REPO_UNAVAILABLE` response (repo went down between preview and
     push) shows the toast and triggers `repreviewZone()` so the banner appears and the operator
     re-decides. No silent failure.
  4. **History badge** — rows whose `snapshot.git_commit.status === 'skipped'` get an amber
     "No snapshot" tag, reason in the existing detail expansion.

No new views, no new routes.

---

## Issue 2 — AdGuard rewrites report false drift on hostname answers

### Problem

`AdGuardHomeProvider::parseRewritesResponse()` FQDN-normalizes the live side, appending a
trailing dot to CNAME answers. `prepareDesiredRecords()` assigns `transformed.sValue = sAnswer`
straight from the `aghdns_answer` metadata with no normalization, and leaves `sName`
un-normalized too. Live `host.example.com.` versus desired `host.example.com` therefore produces
a permanent Add + Drift pair that no deployment can resolve.

### Constraint

`sProviderRecordId` encodes the **dotless** `domain|answer` pair, and `deleteRecord` decodes it
to build the delete payload. Normalization must touch only the diff-facing `sName`/`sValue`,
never the ID or the wire format.

### Shared helper

New `include/common/DnsNameUtils.hpp`, header-only `inline`:

```cpp
bool isHostnameValuedType(std::string_view sType);   // CNAME, NS, MX, SRV, PTR, DNAME
std::string stripTrailingDot(std::string_view s);
std::string ensureTrailingDot(std::string_view s);
/// Comparison form: name always dot-stripped; value dot-stripped only for
/// hostname-valued types. Does NOT lowercase — out of scope.
std::string canonicalName(std::string_view sName);
std::string canonicalValue(std::string_view sType, std::string_view sValue);
```

### Part A — provider fix

`prepareDesiredRecords()` mirrors what `parseRewritesResponse()` produces:
`transformed.sName = ensureTrailingDot(dr.sName)`, and when the inferred type is `CNAME`,
`transformed.sValue = ensureTrailingDot(sAnswer)`. A/AAAA values untouched.
`createRecord`/`deleteRecord` keep stripping dots on the wire; `encodeRecordId` keeps the
dotless pair.

### Part B — DiffEngine guard

`computeDiff()` is entirely string-key driven, so matching becomes dot-insensitive for all
providers by routing every key through one helper:

```cpp
std::string makeKey(name, type, value);      // canonicalName + type + canonicalValue
std::string makeNameTypeKey(name, type);
```

**Subtlety:** `mLiveByNameType` currently stores raw `sValue`, and those raw values are later
re-concatenated into exact keys and compared against `liveDr.sValue` to recover
`sProviderRecordId`. Mixing canonical keys with raw values would break that. The maps therefore
store **canonical** values, plus a new `map<string, const DnsRecord*> mLiveByKey` for recovering
the original live record — which also replaces the existing O(n) inner scan over `vLive` with a
lookup.

Emitted diffs keep the **original** strings in `sSourceValue`/`sProviderValue`, so the UI shows
exactly what each side holds. Canonicalization governs equality only, never display.

The behaviour change is deliberate and narrow: a record differing from live *only* by a trailing
dot on a hostname value stops producing an Add + Drift pair. That drift was always spurious.

---

## Testing

| File | Coverage |
|---|---|
| `tests/unit/test_dns_name_utils.cpp` *(new)* | `isHostnameValuedType` across all types; strip/ensure idempotence; `canonicalValue` leaves A/AAAA/TXT untouched; empty and bare-`"."` edges |
| `tests/unit/test_git_repo_probe.cpp` *(new)* | URL parsing for `ssh://`, scp-style, `https://` with and without port, `file://`; listening loopback socket → reachable; closed port → unreachable within budget; cache warm inside TTL, re-probes after |
| `tests/unit/test_diff_engine.cpp` | **Regression:** desired `CNAME host.example.com.` vs live `host.example.com` → zero diffs. Diffs still carry original un-canonicalized values; `sProviderRecordId` still recovered on Update; a genuine value change still yields exactly one Update |
| `tests/unit/test_adguardhome_provider.cpp` | `prepareDesiredRecords` emits FQDN name and dotted CNAME value; A/AAAA keep no dot; `encodeRecordId` output unchanged (guards the delete path) |
| `tests/integration/test_git_repo_manager.cpp` | Repo pointed at an unreachable host fails within the timeout budget, not the OS default; `commitSnapshot` bails with a typed error instead of entering libssh2 |

## Risks

1. **The DiffEngine change touches all 5 providers** — the one shared-critical-path edit.
   Mitigated by confining canonicalization to key construction, never to emitted values, and by
   regression tests asserting unrelated diff shapes are unchanged. Warrants a focused
   `requesting-code-review` pass before merge.
2. **`git_libgit2_opts` is process-wide.** The server timeouts also affect the legacy
   `GitOpsMirror` — fine, it benefits identically — but must be set once at `GitRepoManager`
   construction, not per-probe.
3. **A TCP probe cannot see auth failure.** A reachable host with bad credentials still fails at
   push time, logged and skipped as today. Unchanged behaviour; the "Test connection" button
   remains the authoritative auth check.
4. **Preview latency on a cold cache** — bounded at `probe_timeout_ms` per distinct *repo*, not
   per zone, since the cache is keyed by repo ID. A 20-zone preview sharing one repo pays it once.

## Out of scope

Queued snapshot retry; case-insensitive DNS name comparison; auth validation in the probe.
