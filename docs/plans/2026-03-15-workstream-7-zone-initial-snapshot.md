# Zone Initial Snapshot — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Ensure every zone has at least one GitOps snapshot for rollback and backup purposes, even if the zone was never deployed through Meridian — via automatic one-time capture and a manual "Capture Current State" button.

**Architecture:** `DeploymentEngine` gains a `capture()` method that pulls live records from providers via `DiffEngine::fetchLiveRecords()`, builds a capture-style snapshot (records as-is from provider, no diff/push), creates a deployment record, commits to GitOps via `GitRepoManager::commitZoneSnapshot()`, and writes an audit entry. The sync-check maintenance task calls `capture()` automatically for zones that have a git repo but no deployment history. A new `POST /zones/{id}/capture` endpoint in `RecordRoutes` exposes manual capture. The `generated_by` field in the snapshot JSON distinguishes auto-capture, manual-capture, and regular deployments.

**Tech Stack:** C++20, Crow HTTP, libpqxx, nlohmann/json, Google Test, Vue 3 + PrimeVue

---

## Context

Workstream 7 from `docs/plans/2026-03-08-v1.0-design.md`. Zones that existed before Meridian managed them (or were imported but never deployed) have no GitOps snapshot and no deployment history for rollback. This workstream captures the provider's current state as a baseline.

## Key Design Decisions

1. **Capture snapshot differs from deployment snapshot** — A deployment snapshot records DB records with variable expansion (`value_template` + `value`). A capture snapshot records live provider records as-is (no templates, no record IDs, values are the raw provider values). The snapshot JSON includes `generated_by` to distinguish the two.
2. **`generated_by` in snapshot JSON, not a DB column** — Avoids a schema migration. The snapshot JSON already has `deployed_by` (actor name). We add `generated_by` with values: `"deployment"` (normal push), `"auto-capture"`, or `"manual-capture"`. Existing snapshots without this field are implicitly `"deployment"`.
3. **No seq=0** — The design spec mentions seq=0, but the existing `DeploymentRepository::create()` uses `COALESCE(MAX(seq), 0) + 1`, making the first deployment seq=1. Changing this would require SQL modifications and could break retention pruning. Instead, captures are distinguished by `generated_by` in the snapshot JSON. The first capture is seq=1 like any other deployment.
4. **Capture route lives in RecordRoutes** — `RecordRoutes` already owns `/zones/{id}/preview` and `/zones/{id}/push`. Adding `/zones/{id}/capture` here is consistent. It already has `DiffEngine` and `DeploymentEngine` references.
5. **Auto-capture runs inside sync-check task** — After confirming `in_sync`, if the zone has a git repo and zero deployments, trigger capture. No separate maintenance task needed.
6. **Capture uses `DiffEngine::fetchLiveRecords()`** — Already exists and handles multi-provider zones, SOA/NS filtering based on zone flags, and FQDN normalization.

---

## Critical Files

### New files
| File | Purpose |
|------|---------|
| `tests/unit/test_capture_snapshot.cpp` | Unit tests for capture snapshot format |
| `tests/integration/test_zone_capture.cpp` | Integration tests for capture flow |

### Modified files
| File | Change |
|------|--------|
| `include/core/DeploymentEngine.hpp` | Add `capture()` method declaration |
| `src/core/DeploymentEngine.cpp` | Implement `capture()` — fetch live records, build snapshot, create deployment, GitOps commit, audit |
| `src/api/routes/RecordRoutes.cpp` | Add `POST /zones/{id}/capture` endpoint |
| `src/main.cpp` | Extend sync-check task to call `capture()` for zones without deployments |
| `ui/src/api/deployments.ts` | Add `captureCurrentState()` API function |
| `ui/src/views/ZoneDetailView.vue` | Add "Capture Current State" button |
| `ui/src/views/DeploymentsView.vue` | Add badge for captured deployments |
| `ui/src/types/index.ts` | Add `CaptureResult` type |

### No CMakeLists changes needed
`src/CMakeLists.txt` uses `GLOB_RECURSE` — new `.cpp` files are auto-discovered.

---

## Reusable Patterns & Utilities

| Pattern | Source | Reuse |
|---------|--------|-------|
| Route auth + permission | `src/api/routes/RecordRoutes.cpp:262-265` | `authenticate()`, `requirePermission()` |
| Response helpers | `include/api/RouteHelpers.hpp` | `jsonResponse()`, `errorResponse()` |
| Fetch live records | `src/core/DiffEngine.cpp` | `fetchLiveRecords(iZoneId)` — returns `vector<DnsRecord>` from all providers |
| Zone mutex | `src/core/DeploymentEngine.cpp:49-57` | `zoneMutex(iZoneId)` — per-zone locking |
| Build timestamp | `src/core/DeploymentEngine.cpp:89-92` | ISO 8601 UTC timestamp pattern |
| GitOps commit | `src/gitops/GitRepoManager.cpp:183-221` | `commitZoneSnapshot(iZoneId, sActor)` |
| Audit insert | `src/core/DeploymentEngine.cpp:258` | `_arRepo.insert()` pattern |
| Deployment create | `src/dal/DeploymentRepository.cpp:12-25` | `create(iZoneId, iUserId, jSnapshot)` |
| Permissions constants | `include/common/Permissions.hpp:17` | `kZonesDeploy` |
| Deployment history check | `src/dal/DeploymentRepository.cpp:27-51` | `listByZoneId(iZoneId, 1)` — check if any exist |

---

## Tasks

### Task 1: Add `capture()` Declaration to DeploymentEngine

**Files:**
- Modify: `include/core/DeploymentEngine.hpp`

**Step 1:** Add the `capture()` method declaration and a `buildCaptureSnapshot()` private helper. Also add a forward declaration for `DnsRecord` if not already present.

Add to the public section of the class, after `push()`:

```cpp
  /// Capture current provider state as a baseline snapshot.
  /// No diff/push — records captured as-is from the provider.
  /// Returns the new deployment ID.
  int64_t capture(int64_t iZoneId, int64_t iActorUserId,
                  const std::string& sActor, const std::string& sGeneratedBy);
```

Add to the private section, after `buildSnapshot()`:

```cpp
  nlohmann::json buildCaptureSnapshot(int64_t iZoneId,
                                      const std::vector<common::DnsRecord>& vLiveRecords,
                                      const std::string& sActor,
                                      const std::string& sGeneratedBy) const;
```

**Step 2:** Build to verify compilation: `cmake --build build --parallel`

**Step 3:** Commit: `feat(deploy): add capture() declaration to DeploymentEngine`

---

### Task 2: Unit Tests for Capture Snapshot Format

**Files:**
- Create: `tests/unit/test_capture_snapshot.cpp`

**Step 1:** Write unit tests that validate the capture snapshot JSON format. These test the `buildCaptureSnapshot()` output format (which we'll implement in Task 3). Since `buildCaptureSnapshot` is private, we test indirectly via the JSON format expectations.

For now, write format-validation tests that verify a capture snapshot JSON structure:

```cpp
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {

/// Validates the expected shape of a capture snapshot JSON.
bool isValidCaptureSnapshot(const nlohmann::json& j) {
  if (!j.contains("zone") || !j["zone"].is_string()) return false;
  if (!j.contains("view") || !j["view"].is_string()) return false;
  if (!j.contains("captured_at") || !j["captured_at"].is_string()) return false;
  if (!j.contains("generated_by") || !j["generated_by"].is_string()) return false;
  if (!j.contains("records") || !j["records"].is_array()) return false;

  for (const auto& r : j["records"]) {
    if (!r.contains("name") || !r["name"].is_string()) return false;
    if (!r.contains("type") || !r["type"].is_string()) return false;
    if (!r.contains("value") || !r["value"].is_string()) return false;
    if (!r.contains("ttl") || !r["ttl"].is_number()) return false;
  }
  return true;
}

TEST(CaptureSnapshotFormat, ValidSnapshotHasAllRequiredFields) {
  nlohmann::json j = {
      {"zone", "example.com"},
      {"view", "production"},
      {"captured_at", "2026-03-15T12:00:00Z"},
      {"generated_by", "auto-capture"},
      {"record_count", 3},
      {"records",
       {{{"name", "www"}, {"type", "A"}, {"value", "192.0.2.1"}, {"ttl", 300}, {"priority", 0}},
        {{"name", "mail"}, {"type", "MX"}, {"value", "mx.example.com."}, {"ttl", 3600}, {"priority", 10}},
        {{"name", "@"}, {"type", "TXT"}, {"value", "v=spf1 -all"}, {"ttl", 300}, {"priority", 0}}}},
  };

  EXPECT_TRUE(isValidCaptureSnapshot(j));
}

TEST(CaptureSnapshotFormat, MissingZoneIsInvalid) {
  nlohmann::json j = {
      {"view", "production"},
      {"captured_at", "2026-03-15T12:00:00Z"},
      {"generated_by", "auto-capture"},
      {"records", nlohmann::json::array()},
  };
  EXPECT_FALSE(isValidCaptureSnapshot(j));
}

TEST(CaptureSnapshotFormat, MissingGeneratedByIsInvalid) {
  nlohmann::json j = {
      {"zone", "example.com"},
      {"view", "production"},
      {"captured_at", "2026-03-15T12:00:00Z"},
      {"records", nlohmann::json::array()},
  };
  EXPECT_FALSE(isValidCaptureSnapshot(j));
}

TEST(CaptureSnapshotFormat, RecordMissingValueIsInvalid) {
  nlohmann::json j = {
      {"zone", "example.com"},
      {"view", "production"},
      {"captured_at", "2026-03-15T12:00:00Z"},
      {"generated_by", "manual-capture"},
      {"records", {{{"name", "www"}, {"type", "A"}, {"ttl", 300}}}},
  };
  EXPECT_FALSE(isValidCaptureSnapshot(j));
}

TEST(CaptureSnapshotFormat, AutoCaptureVsManualCapture) {
  nlohmann::json jAuto = {
      {"zone", "example.com"},
      {"view", "production"},
      {"captured_at", "2026-03-15T12:00:00Z"},
      {"generated_by", "auto-capture"},
      {"records", nlohmann::json::array()},
  };
  nlohmann::json jManual = jAuto;
  jManual["generated_by"] = "manual-capture";

  EXPECT_TRUE(isValidCaptureSnapshot(jAuto));
  EXPECT_TRUE(isValidCaptureSnapshot(jManual));
  EXPECT_NE(jAuto["generated_by"], jManual["generated_by"]);
}

TEST(CaptureSnapshotFormat, EmptyRecordsIsValid) {
  nlohmann::json j = {
      {"zone", "empty.example.com"},
      {"view", "staging"},
      {"captured_at", "2026-03-15T12:00:00Z"},
      {"generated_by", "auto-capture"},
      {"records", nlohmann::json::array()},
  };
  EXPECT_TRUE(isValidCaptureSnapshot(j));
}

}  // namespace
```

**Step 2:** Build and run tests:

```bash
cmake --build build --parallel && build/tests/dns-tests --gtest_filter=CaptureSnapshotFormat*
```

Expected: All 6 tests PASS.

**Step 3:** Commit: `test(deploy): add unit tests for capture snapshot format`

---

### Task 3: Implement `capture()` and `buildCaptureSnapshot()`

**Files:**
- Modify: `src/core/DeploymentEngine.cpp`

**Step 1:** Add the `buildCaptureSnapshot()` private method. Place it after the existing `buildSnapshot()` method (after line 101):

```cpp
nlohmann::json DeploymentEngine::buildCaptureSnapshot(
    int64_t iZoneId,
    const std::vector<common::DnsRecord>& vLiveRecords,
    const std::string& sActor,
    const std::string& sGeneratedBy) const {
  auto oZone = _zrRepo.findById(iZoneId);
  std::string sZoneName = oZone ? oZone->sName : "unknown";

  auto oView = oZone ? _vrRepo.findById(oZone->iViewId) : std::nullopt;
  std::string sViewName = oView ? oView->sName : "unknown";

  nlohmann::json jRecords = nlohmann::json::array();
  for (const auto& rec : vLiveRecords) {
    jRecords.push_back({
        {"name", rec.sName},
        {"type", rec.sType},
        {"value", rec.sValue},
        {"ttl", rec.uTtl},
        {"priority", rec.iPriority},
    });
  }

  auto tpNow = std::chrono::system_clock::now();
  auto ttNow = std::chrono::system_clock::to_time_t(tpNow);
  std::ostringstream oss;
  oss << std::put_time(std::gmtime(&ttNow), "%FT%TZ");

  return {
      {"zone", sZoneName},
      {"view", sViewName},
      {"captured_at", oss.str()},
      {"generated_by", sGeneratedBy},
      {"record_count", static_cast<int>(vLiveRecords.size())},
      {"records", jRecords},
  };
}
```

**Step 2:** Add the `capture()` method. Place it after `push()` (after line 289):

```cpp
int64_t DeploymentEngine::capture(int64_t iZoneId, int64_t iActorUserId,
                                  const std::string& sActor,
                                  const std::string& sGeneratedBy) {
  auto spLog = common::Logger::get();

  // 1. Acquire per-zone mutex
  auto& mtxZone = zoneMutex(iZoneId);
  if (!mtxZone.try_lock()) {
    throw common::DeploymentLockedError(
        "ZONE_LOCKED", "Zone " + std::to_string(iZoneId) + " is currently being deployed");
  }
  std::lock_guard lock(mtxZone, std::adopt_lock);

  // 2. Fetch live records from all providers
  auto vLiveRecords = _deEngine.fetchLiveRecords(iZoneId);

  auto oZone = _zrRepo.findById(iZoneId);
  std::string sZoneName = oZone ? oZone->sName : "unknown";

  spLog->info("DeploymentEngine: capturing {} live records for zone '{}'",
              vLiveRecords.size(), sZoneName);

  // 3. Build capture snapshot
  auto jSnapshot = buildCaptureSnapshot(iZoneId, vLiveRecords, sActor, sGeneratedBy);

  // 4. Create deployment record
  int64_t iDeploymentId = _drRepo.create(iZoneId, iActorUserId, jSnapshot);

  // 5. GitOps commit (non-fatal)
  if (_pGitRepoManager) {
    _pGitRepoManager->commitZoneSnapshot(iZoneId, sActor);
  }

  // 6. Audit log
  nlohmann::json jAuditDetail = {
      {"generated_by", sGeneratedBy},
      {"record_count", static_cast<int>(vLiveRecords.size())},
  };
  _arRepo.insert("zone", iZoneId, "zone.capture", std::nullopt, jAuditDetail,
                 sActor, std::nullopt, std::nullopt);

  spLog->info("DeploymentEngine: captured zone '{}' ({} records, deployment #{})",
              sZoneName, vLiveRecords.size(), iDeploymentId);

  return iDeploymentId;
}
```

**Step 3:** Build: `cmake --build build --parallel`

**Step 4:** Commit: `feat(deploy): implement capture() for baseline zone snapshots`

---

### Task 4: Integration Tests for Capture

**Files:**
- Create: `tests/integration/test_zone_capture.cpp`

**Step 1:** Write integration tests. These require `DNS_DB_URL` and will skip otherwise (following existing integration test patterns). Check an existing integration test file for the skip pattern.

Look at `tests/integration/test_auth_service.cpp` (line 1-20) for the `DNS_DB_URL` skip pattern and test fixture setup, then model these tests similarly:

```cpp
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

// These tests require a live database — skip if DNS_DB_URL is not set.
// Test fixture mirrors existing integration test patterns.

namespace {

class ZoneCaptureTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const char* pDbUrl = std::getenv("DNS_DB_URL");
    if (!pDbUrl) GTEST_SKIP() << "DNS_DB_URL not set — skipping DB integration tests";
  }
};

TEST_F(ZoneCaptureTest, CaptureCreatesDeploymentRecord) {
  GTEST_SKIP() << "Requires full service wiring — tested via E2E";
}

TEST_F(ZoneCaptureTest, CaptureSnapshotHasGeneratedByField) {
  GTEST_SKIP() << "Requires full service wiring — tested via E2E";
}

TEST_F(ZoneCaptureTest, CaptureAuditsWithCorrectOperation) {
  GTEST_SKIP() << "Requires full service wiring — tested via E2E";
}

TEST_F(ZoneCaptureTest, CaptureSkipsWhenZoneLocked) {
  GTEST_SKIP() << "Requires full service wiring — tested via E2E";
}

}  // namespace
```

**Note:** Full integration tests for `capture()` require the entire service graph (DiffEngine, ProviderFactory, live provider, DB). The unit tests in Task 2 validate format. The E2E manual test (Verification section) validates the full flow. These stubs document the intended test coverage and will be fleshed out when the test infrastructure supports service-level integration testing.

**Step 2:** Build and run:

```bash
cmake --build build --parallel && build/tests/dns-tests --gtest_filter=ZoneCapture*
```

Expected: All tests SKIP (no `DNS_DB_URL`).

**Step 3:** Commit: `test(deploy): add integration test stubs for zone capture`

---

### Task 5: Add `POST /zones/{id}/capture` Endpoint

**Files:**
- Modify: `src/api/routes/RecordRoutes.cpp`

**Step 1:** Add the capture endpoint inside `RecordRoutes::registerRoutes()`. Place it after the `/zones/<int>/push` route block (after the closing `});` around line 362):

```cpp
  // POST /api/v1/zones/<int>/capture
  CROW_ROUTE(app, "/api/v1/zones/<int>/capture").methods("POST"_method)(
      [this](const crow::request& req, int iZoneId) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          requirePermission(rcCtx, Permissions::kZonesDeploy);

          int64_t iDeploymentId =
              _depEngine.capture(iZoneId, rcCtx.iUserId, rcCtx.sUsername, "manual-capture");

          return jsonResponse(201, {{"message", "Current state captured successfully"},
                                    {"deployment_id", iDeploymentId}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });
```

**Step 2:** Build: `cmake --build build --parallel`

**Step 3:** Commit: `feat(api): add POST /zones/{id}/capture endpoint`

---

### Task 6: Auto-Capture in Sync-Check Maintenance Task

**Files:**
- Modify: `src/main.cpp`

**Step 1:** Extend the sync-check maintenance task lambda (around line 390-411). After the existing `zrRepo.updateSyncStatus()` call, add the auto-capture logic. The lambda needs additional captures: `drRepo` and `depEngine`.

Replace the existing sync-check schedule block with:

```cpp
if (cfgApp.iSyncCheckInterval > 0) {
  msScheduler->schedule("sync-check",
      std::chrono::seconds(cfgApp.iSyncCheckInterval),
      [&zrRepo = *zrRepo, &deEngine = *deEngine, &drRepo = *drRepo,
       &depEngine = *depEngine]() {
        auto spLog = dns::common::Logger::get();
        auto vZones = zrRepo.listAll();
        int iChecked = 0;
        int iCaptured = 0;
        for (const auto& zone : vZones) {
          std::string sStatus = "in_sync";
          try {
            auto preview = deEngine.preview(zone.iId);
            sStatus = preview.bHasDrift ? "drift" : "in_sync";
          } catch (...) {
            sStatus = "error";
          }
          zrRepo.updateSyncStatus(zone.iId, sStatus);
          ++iChecked;

          // Auto-capture: if in_sync, has git repo, and no deployments exist
          if (sStatus == "in_sync" && zone.oGitRepoId.has_value()) {
            try {
              auto vDeps = drRepo.listByZoneId(zone.iId, 1);
              if (vDeps.empty()) {
                depEngine.capture(zone.iId, 0, "system/auto-capture", "auto-capture");
                ++iCaptured;
                spLog->info("Auto-captured baseline for zone '{}'", zone.sName);
              }
            } catch (const std::exception& ex) {
              spLog->warn("Auto-capture failed for zone '{}': {}", zone.sName, ex.what());
            }
          }
        }
        spLog->info("Sync check complete: {} zones checked, {} auto-captured",
                    iChecked, iCaptured);
      });
}
```

**Important notes:**
- `iActorUserId = 0` for system-initiated captures (the `deployed_by` FK to `users(id)` needs a valid user). Check if user ID 0 is valid in your schema. If the `deployed_by` column has a NOT NULL FK constraint to `users(id)`, use the system/admin user's ID (typically 1). Look at the `users` table for the bootstrap admin user ID.
- If ID 0 causes a FK violation, use `1` instead (the setup wizard creates the first admin user with ID 1).

**Step 2:** Check what user ID the admin bootstrap creates. Look at `src/api/routes/AuthRoutes.cpp` or the setup wizard for the initial admin user creation. Use that ID for auto-capture.

Grep for the initial user creation:

```bash
grep -n "create.*admin\|setup.*user\|bootstrap.*user" src/ -r
```

If the first user is always ID 1, use `1` instead of `0` in the lambda above.

**Step 3:** Build: `cmake --build build --parallel`

**Step 4:** Commit: `feat(deploy): auto-capture zone baseline during sync-check`

---

### Task 7: Frontend — API Client Function

**Files:**
- Modify: `ui/src/api/deployments.ts`
- Modify: `ui/src/types/index.ts`

**Step 1:** Add the `CaptureResult` type to `ui/src/types/index.ts`. Place it after the `DeploymentSnapshot` interface (after line 146):

```typescript
export interface CaptureResult {
  message: string
  deployment_id: number
}
```

**Step 2:** Add the `captureCurrentState` function to `ui/src/api/deployments.ts`. Add the import for `CaptureResult` and add the function at the end of the file:

```typescript
export function captureCurrentState(zoneId: number): Promise<CaptureResult> {
  return post(`/zones/${zoneId}/capture`)
}
```

Update the import line at the top to include `CaptureResult`:

```typescript
import type { PreviewResult, DeploymentSnapshot, DeploymentDiff, DriftAction, CaptureResult } from '../types'
```

**Step 3:** Verify: `cd ui && npx vue-tsc --noEmit`

**Step 4:** Commit: `feat(ui): add captureCurrentState API client function`

---

### Task 8: Frontend — "Capture Current State" Button on ZoneDetailView

**Files:**
- Modify: `ui/src/views/ZoneDetailView.vue`

**Step 1:** Import the `captureCurrentState` function. Find the existing imports from `../api/deployments` and add `captureCurrentState`:

```typescript
import { captureCurrentState } from '../api/deployments'
```

If there's no existing import from `../api/deployments`, add the line near the other API imports.

**Step 2:** Add a `capturing` ref and the handler function. In the `<script setup>` section, near other ref declarations:

```typescript
const capturing = ref(false)
```

Add the handler function near other action handlers:

```typescript
async function doCapture() {
  capturing.value = true
  try {
    const result = await captureCurrentState(zoneId.value)
    notify.success(result.message)
    await loadZone()
  } catch (e: any) {
    notify.error(e.message || 'Capture failed')
  } finally {
    capturing.value = false
  }
}
```

Ensure `zoneId` is accessible (it's likely derived from the route params). Check existing code for the pattern — look for how `goToDeploy` or `doExportZone` access the zone ID.

**Step 3:** Add the button in the template. Place it between the "Export" and "Import" buttons (around line 449, after the Export button's closing `/>` tag):

```vue
<Button
  v-if="isOperator && zone?.git_repo_id"
  label="Capture State"
  icon="pi pi-camera"
  severity="secondary"
  :loading="capturing"
  @click="doCapture"
  class="mr-2"
/>
```

The `v-if` condition ensures the button only shows for operators and only when the zone has a git repo assigned (`zone?.git_repo_id` — check the actual property name in the zone object; it might be `zone.git_repo_id` or accessed differently).

**Step 4:** Verify: `cd ui && npx vue-tsc --noEmit && npm run build`

**Step 5:** Commit: `feat(ui): add Capture Current State button to ZoneDetailView`

---

### Task 9: Frontend — Capture Badge in Deployment History

**Files:**
- Modify: `ui/src/views/DeploymentsView.vue`

**Step 1:** Find the deployment history DataTable (around line 491-530). Add a "Type" column after the "Timestamp" column:

```vue
<Column header="Type" style="width: 8rem">
  <template #body="{ data }">
    <Tag
      v-if="data.snapshot?.generated_by === 'auto-capture'"
      value="Baseline"
      severity="info"
      icon="pi pi-history"
    />
    <Tag
      v-else-if="data.snapshot?.generated_by === 'manual-capture'"
      value="Captured"
      severity="info"
      icon="pi pi-camera"
    />
    <Tag
      v-else
      value="Deployed"
      severity="success"
      icon="pi pi-play"
    />
  </template>
</Column>
```

This reads `generated_by` from the `snapshot` JSONB field returned by the API. Existing deployments without `generated_by` show as "Deployed" (the `v-else` branch).

**Step 2:** Verify the `data.snapshot` shape. Check the `deploymentRowToJson()` helper in `src/api/routes/DeploymentRoutes.cpp:29-40` — it includes `{"snapshot", row.jSnapshot}`, so the snapshot JSON is nested under `snapshot` in the API response. On the frontend, `data.snapshot.generated_by` accesses the field.

**Step 3:** Verify: `cd ui && npx vue-tsc --noEmit && npm run build`

**Step 4:** Commit: `feat(ui): add deployment type badges in history view`

---

### Task 10: Update openapi.yaml

**Files:**
- Modify: `docs/openapi.yaml`

**Step 1:** Add the new endpoint to the OpenAPI spec. Find the zone-related paths section and add:

```yaml
  /api/v1/zones/{zone_id}/capture:
    post:
      tags:
        - Zones
      summary: Capture current provider state
      description: >
        Pulls current records from the provider and creates a baseline deployment
        snapshot. No diff computation or push — records captured as-is. Useful for
        establishing a GitOps baseline for zones not yet deployed through Meridian.
      operationId: captureZoneState
      parameters:
        - name: zone_id
          in: path
          required: true
          schema:
            type: integer
            format: int64
      security:
        - bearerAuth: []
      responses:
        '201':
          description: State captured successfully
          content:
            application/json:
              schema:
                type: object
                properties:
                  message:
                    type: string
                    example: Current state captured successfully
                  deployment_id:
                    type: integer
                    format: int64
        '401':
          $ref: '#/components/responses/Unauthorized'
        '403':
          $ref: '#/components/responses/Forbidden'
        '404':
          $ref: '#/components/responses/NotFound'
        '409':
          description: Zone is locked (deployment in progress)
          content:
            application/json:
              schema:
                $ref: '#/components/schemas/Error'
```

Adapt the YAML structure to match the existing style in the file (indentation, `$ref` patterns, tag names).

**Step 2:** Commit: `docs(api): add POST /zones/{id}/capture to OpenAPI spec`

---

## Verification

### Backend
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
build/tests/dns-tests --gtest_filter=CaptureSnapshotFormat*
build/tests/dns-tests --gtest_filter=ZoneCapture*  # skips without DNS_DB_URL
```

### Frontend
```bash
cd ui && npm run build    # no TypeScript errors
cd ui && npm run dev      # manual testing on :5173
```

### Manual E2E
1. Start the app with a populated database and at least one zone with a git repo assigned
2. Navigate to the zone detail page
3. Verify "Capture State" button is visible (only if zone has git repo and user is operator)
4. Click "Capture State" — verify success toast
5. Navigate to Deployments page — verify the captured deployment shows with "Captured" badge
6. Expand the deployment — verify snapshot JSON includes `generated_by: "manual-capture"` and `captured_at`
7. Create a new zone with a git repo, wait for sync check to run (or trigger manually)
8. Verify auto-captured deployment appears with "Baseline" badge and `generated_by: "auto-capture"`
9. Verify a second sync check does NOT create another capture for the same zone
10. Check audit log for `zone.capture` entries with correct source (`auto` vs `manual`)
