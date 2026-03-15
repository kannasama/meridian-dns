# Config Backup & Restore — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Full system configuration export (JSON) and transactional restore with two restore paths (file upload and GitOps), plus zone-level export/import.

**Architecture:** `BackupService` orchestrates all repository queries for export and runs raw SQL through a single `pqxx::work` transaction for restore. `BackupRoutes` exposes 5 HTTP endpoints. GitOps backup is handled at the route/maintenance level, not inside BackupService. All references in the export use **names, not IDs** for cross-instance portability.

**Tech Stack:** C++20, Crow HTTP, libpqxx, nlohmann/json, Vue 3 + PrimeVue

---

## Context

Workstream 6 from `docs/plans/2026-03-08-v1.0-design.md`. Enables disaster recovery, environment cloning, and auditable configuration snapshots. Encrypted credentials are excluded from exports — admin must re-enter them after restore.

## Key Design Decisions

1. **Raw JSON body for restore** (not multipart) — the codebase has zero multipart precedent and the payload is already JSON. UI reads the file via `FileReader` and POSTs the contents.
2. **Direct SQL in `applyRestore`** — existing repositories each checkout their own connection, making single-transaction restore impossible through them. `applyRestore` uses raw SQL via a shared `pqxx::work`.
3. **BackupService does NOT own GitRepoManager** — GitOps commit is handled at the route layer and in the maintenance task, keeping the service focused on serialization/deserialization.
4. **Name-based references** — IDs are internal; export uses entity names for portability. Restore builds name→ID maps as it processes entities in dependency order.
5. **Routes registered directly on `crowApp`** — follows the pattern of newer routes (UserRoutes, GroupRoutes, etc.) that register outside `ApiServer`.

---

## Critical Files

### New files
| File | Purpose |
|------|---------|
| `include/core/BackupService.hpp` | Service header with `RestoreResult` struct |
| `src/core/BackupService.cpp` | Export serialization, restore validation/preview/apply |
| `include/api/routes/BackupRoutes.hpp` | Route handler header |
| `src/api/routes/BackupRoutes.cpp` | 5 endpoints: export, restore, restore-from-repo, zone export, zone import |
| `ui/src/api/backup.ts` | Typed API client for backup endpoints |
| `ui/src/views/BackupRestoreView.vue` | Admin-only page with export/restore UI |
| `tests/unit/test_backup_service.cpp` | Format validation + export structure tests |
| `tests/integration/test_backup_restore.cpp` | Round-trip export→restore DB tests |

### Modified files
| File | Change |
|------|--------|
| `src/main.cpp` | Construct `BackupService` + `BackupRoutes`, schedule backup task |
| `include/gitops/GitRepoManager.hpp` | Add `readFile()` + `writeAndCommit()` |
| `src/gitops/GitRepoManager.cpp` | Implement file read/write methods |
| `ui/src/router/index.ts` | Add `/admin/backup` route |
| `ui/src/components/layout/AppSidebar.vue` | Add "Backup & Restore" to admin nav |

### No CMakeLists changes needed
`src/CMakeLists.txt` uses `GLOB_RECURSE` — new `.cpp` files in `src/core/` and `src/api/routes/` are auto-discovered.

---

## Reusable Patterns & Utilities

| Pattern | Source | Reuse |
|---------|--------|-------|
| Route auth + permission | `src/api/routes/AuditRoutes.cpp:66-67` | `authenticate()`, `requirePermission()` |
| Response helpers | `include/api/RouteHelpers.hpp` | `jsonResponse()`, `errorResponse()` |
| File download | `src/api/routes/AuditRoutes.cpp:129-131` | `Content-Disposition` + `Content-Type` headers |
| Timestamp formatting | `src/api/routes/AuditRoutes.cpp:27-32` | `formatTimestamp()` pattern |
| Maintenance task | `src/main.cpp:298-306` | `msScheduler->schedule()` pattern |
| Permissions constants | `include/common/Permissions.hpp` | `kBackupCreate`, `kBackupRestore` (already defined) |
| Settings read | `include/dal/SettingsRepository.hpp:31` | `getValue(key, default)` |
| All repo `listAll()` | Every `*Repository.hpp` | Used to collect export data |

---

## Tasks

### Task 1: BackupService Header + RestoreResult Type

**Files:**
- Create: `include/core/BackupService.hpp`

**Step 1:** Create header with `RestoreResult` struct and `BackupService` class declaration.

The class takes references to all 12 repositories (ConnectionPool, SettingsRepository, RoleRepository, GroupRepository, UserRepository, IdpRepository, GitRepoRepository, ProviderRepository, ViewRepository, ZoneRepository, RecordRepository, VariableRepository).

Public interface:
```cpp
nlohmann::json exportSystem(const std::string& sExportedBy) const;
nlohmann::json exportZone(int64_t iZoneId) const;
RestoreResult previewRestore(const nlohmann::json& jBackup) const;
RestoreResult applyRestore(const nlohmann::json& jBackup);
```

Private: `void validateBackupFormat(const nlohmann::json& jBackup) const;`

**Step 2:** Verify it compiles: `cmake --build build --parallel`

**Step 3:** Commit: `feat(backup): add BackupService header with export/restore interface`

---

### Task 2: Export Format Unit Tests

**Files:**
- Create: `tests/unit/test_backup_service.cpp`

**Step 1:** Write tests for backup JSON format validation:
- `BackupFormatTest/ValidBackupHasAllRequiredFields` — constructs valid JSON, asserts helper returns true
- `BackupFormatTest/MissingVersionIsInvalid`
- `BackupFormatTest/WrongVersionIsInvalid`
- `BackupFormatTest/MissingSectionIsInvalid`
- `BackupFormatTest/ZoneExportHasTypeField`

Use a local `isValidBackupJson()` helper that checks: version==1, all 11 section keys present (settings as object, rest as arrays), plus metadata fields (exported_at, exported_by, meridian_version).

**Step 2:** Run tests: `cmake --build build --parallel && build/tests/dns-tests --gtest_filter=BackupFormat*`
Expected: PASS

**Step 3:** Commit: `test(backup): add unit tests for backup JSON format validation`

---

### Task 3: Implement exportSystem

**Files:**
- Create: `src/core/BackupService.cpp`

**Step 1:** Implement constructor storing all repository references.

**Step 2:** Implement `exportSystem()`:

1. Build ID→name lookup maps for views, zones, providers, roles, git repos (needed to resolve FK references)
2. Export each entity type:

| Entity | Source | Included Fields | Excluded |
|--------|--------|----------------|----------|
| settings | `_stRepo.listAll()` | key, value | — |
| roles | `_rlRepo.listAll()` + `getPermissions()` | name, description, is_system, permissions[] | id |
| groups | `_grRepo.listAll()` | name, description, role_name (via role ID→name map) | id |
| users | `_urRepo.listAll()` | username, email, auth_method, is_active | password_hash, oidc_sub, saml_name_id |
| identity_providers | `_irRepo.listAll()` | name, type, is_enabled, config, group_mappings, default_group_name | encrypted_secret |
| git_repos | `_grRepoGit.listAll()` | name, remote_url, auth_type, default_branch | encrypted_credentials, known_hosts |
| providers | `_prRepo.listAll()` | name, type, api_endpoint | encrypted token |
| views | `_vrRepo.listAll()` + `findWithProviders()` per view | name, description, provider_names[] | id |
| zones | `_zrRepo.listAll()` | name, view_name, git_repo_name, git_branch, manage_soa, manage_ns, deployment_retention | sync_status, sync_checked_at |
| records | `_rrRepo.listByZoneId()` per zone | zone_name, name, type, ttl, value_template, priority | id, provider_meta |
| variables | `_varRepo.listAll()` | name, value, type, scope, zone_name (if zone-scoped) | id |

3. Add metadata: `version=1`, `exported_at` (ISO 8601 UTC), `exported_by`, `meridian_version` (from `kVersion`).

**Note:** For views, need to call `findWithProviders()` for each view to get `vProviderIds`, then map to provider names. This is N+1 but views count is typically small.

**Step 3:** Build and verify: `cmake --build build --parallel`

**Step 4:** Commit: `feat(backup): implement BackupService::exportSystem`

---

### Task 4: Implement exportZone

**Files:**
- Modify: `src/core/BackupService.cpp`

**Step 1:** Implement `exportZone(iZoneId)`:

1. `_zrRepo.findById(iZoneId)` — throw `NotFoundError` if not found
2. Look up view name via `_vrRepo.findById(zone.iViewId)`
3. Look up git repo name if `zone.oGitRepoId` is set
4. `_rrRepo.listByZoneId(iZoneId)` — serialize records (exclude provider_meta)
5. `_varRepo.listByZoneId(iZoneId)` — serialize zone-scoped + global variables

Output format:
```json
{
  "version": 1,
  "type": "zone",
  "exported_at": "...",
  "zone": { "name": "...", "view_name": "...", ... },
  "records": [...],
  "variables": [...]
}
```

**Step 2:** Build: `cmake --build build --parallel`

**Step 3:** Commit: `feat(backup): implement BackupService::exportZone`

---

### Task 5: Implement validateBackupFormat

**Files:**
- Modify: `src/core/BackupService.cpp`

**Step 1:** Implement `validateBackupFormat()`:
- Check `version` field exists and equals 1 (throw `ValidationError` otherwise)
- Check all required sections present and correct type (object for settings, array for rest)
- Check metadata fields present (exported_at, exported_by, meridian_version)

**Step 2:** Run format tests: `build/tests/dns-tests --gtest_filter=BackupFormat*`

**Step 3:** Commit: `feat(backup): implement backup format validation`

---

### Task 6: Implement previewRestore

**Files:**
- Modify: `src/core/BackupService.cpp`

**Step 1:** Implement `previewRestore()`:

1. Call `validateBackupFormat()`
2. For each entity type in dependency order, compare backup entries against existing DB state:
   - settings: compare by key
   - roles: compare by name via `_rlRepo.findByName()`
   - groups: compare by name (need `findByName` — use listAll + filter if no findByName exists)
   - users: compare by username via `_urRepo.findByUsername()`
   - identity_providers: compare by name (listAll + filter)
   - git_repos: compare by name via `_grRepoGit.findByName()`
   - providers: compare by name (listAll + filter)
   - views: compare by name (listAll + filter)
   - zones: compare by name (listAll + filter)
   - records: compare by (zone_name, name, type) composite
   - variables: compare by (name, scope, zone_name) composite
3. For each: classify as create/update/skip, count per entity type
4. For providers, git_repos, identity_providers: add to `vCredentialWarnings` if creating
5. Return `RestoreResult` with `bApplied = false`

**Step 2:** Build: `cmake --build build --parallel`

**Step 3:** Commit: `feat(backup): implement restore preview logic`

---

### Task 7: Implement applyRestore

**Files:**
- Modify: `src/core/BackupService.cpp`

This is the largest task. The restore runs inside a single `pqxx::work` transaction using raw SQL.

**Step 1:** Implement `applyRestore()`:

```cpp
auto cg = _cpPool.checkout();
pqxx::work txn(*cg);
// ... restore all entities ...
txn.commit();
```

Dependency order (each step builds name→ID maps for subsequent steps):

1. **Settings:** `INSERT ... ON CONFLICT (key) DO UPDATE SET value = $2`
2. **Roles:** Find by name → update or insert. Then set permissions via delete+insert on `role_permissions`. Build role name→ID map.
3. **Groups:** Find by name → update or insert with `role_id` from role name→ID map. Build group name→ID map.
4. **Users:** Find by username → update (email, is_active, auth_method) or create (no password hash, `force_password_change = true`). Build username→ID map. Restore group memberships.
5. **Providers:** Find by name → update non-credential fields or create with empty token. Flag for credential re-entry. Build provider name→ID map.
6. **Git Repos:** Same pattern as providers. Build git repo name→ID map.
7. **Identity Providers:** Find by name → update non-secret config or create with empty secret. Flag for re-entry.
8. **Views:** Find by name → update or create. Restore provider attachments using provider name→ID map.
9. **Zones:** Find by name → update or create. Resolve view name→ID and git repo name→ID. Build zone name→ID map.
10. **Records:** For each zone_name group, resolve zone ID. Find existing by (name, type, zone_id) → update or create. Exclude provider_meta.
11. **Variables:** Find existing by (name, scope, zone_id) → update or create. Resolve zone name→ID for zone-scoped.

Track `RestoreResult` with create/update/skip counts and credential warnings.

**Step 2:** Build: `cmake --build build --parallel`

**Step 3:** Commit: `feat(backup): implement transactional restore with dependency-ordered entity processing`

---

### Task 8: Integration Tests

**Files:**
- Create: `tests/integration/test_backup_restore.cpp`

**Step 1:** Write integration tests (all skip without `DNS_DB_URL`):

- `BackupRestoreTest/ExportProducesValidJson` — export, validate format
- `BackupRestoreTest/ExportExcludesCredentials` — verify no password hashes or encrypted tokens in output
- `BackupRestoreTest/RestorePreviewCountsCorrect` — create some entities, export, modify DB, preview restore, check counts
- `BackupRestoreTest/RestoreApplyIsIdempotent` — export, restore, re-export, compare (should be equivalent)
- `BackupRestoreTest/RestoreCreatesWithEmptyCredentials` — restore to clean DB, verify providers/git repos have empty credentials
- `BackupRestoreTest/RestoreFlagsCredentialWarnings` — verify `vCredentialWarnings` lists credential-bearing entities
- `BackupRestoreTest/ZoneExportRoundTrip` — export zone, delete it, import back, verify records match

Test setup: create a few test entities (provider, view, zone, records, variables, role, group, user) via repository calls, then exercise export/restore.

**Step 2:** Run: `build/tests/dns-tests --gtest_filter=BackupRestore*`

**Step 3:** Commit: `test(backup): add integration tests for backup export/restore round-trip`

---

### Task 9: BackupRoutes Header

**Files:**
- Create: `include/api/routes/BackupRoutes.hpp`

**Step 1:** Create header following `SettingsRoutes.hpp` pattern:

```cpp
class BackupRoutes {
 public:
  BackupRoutes(dns::core::BackupService& bsService,
               dns::dal::SettingsRepository& stRepo,
               const dns::api::AuthMiddleware& amMiddleware,
               dns::gitops::GitRepoManager* pGitRepoManager = nullptr);
  ~BackupRoutes();
  void registerRoutes(crow::SimpleApp& app);
 private:
  dns::core::BackupService& _bsService;
  dns::dal::SettingsRepository& _stRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
  dns::gitops::GitRepoManager* _pGitRepoManager;
};
```

**Step 2:** Build: `cmake --build build --parallel`

**Step 3:** Commit: `feat(backup): add BackupRoutes header`

---

### Task 10: BackupRoutes Implementation

**Files:**
- Create: `src/api/routes/BackupRoutes.cpp`

**Step 1:** Implement 5 routes:

**GET /api/v1/backup/export** — `backup.create` permission
- Call `_bsService.exportSystem(rcCtx.sUsername)`
- If `?commit_to_git=true` and backup repo configured: write to git via `_pGitRepoManager`
- Return JSON with `Content-Disposition: attachment; filename="meridian-backup-{timestamp}.json"`

**POST /api/v1/backup/restore** — `backup.restore` permission
- Parse `req.body` as JSON (not multipart)
- If `?apply=true`: call `_bsService.applyRestore()`
- Else: call `_bsService.previewRestore()`
- Return `RestoreResult` as JSON:
  ```json
  {
    "applied": false,
    "summaries": [{"entity_type": "roles", "created": 2, "updated": 1, "skipped": 0}, ...],
    "credential_warnings": ["provider:cloudflare", "git_repo:main-repo"]
  }
  ```

**POST /api/v1/backup/restore-from-repo** — `backup.restore` permission
- Read `backup.git_repo_id` from settings
- Pull repo, read backup file from mirror's local path
- Same preview/apply logic as file restore

**GET /api/v1/zones/<int>/export** — `backup.create` permission
- Call `_bsService.exportZone(iZoneId)`
- Return with attachment header

**POST /api/v1/zones/<int>/import** — `backup.restore` permission
- Parse `req.body` as JSON
- Validate `type == "zone"`
- Apply zone records and variables (needs a dedicated method or reuse partial restore logic)

**Step 2:** Build: `cmake --build build --parallel`

**Step 3:** Commit: `feat(backup): implement backup/restore API endpoints`

---

### Task 11: GitRepoManager File Operations

**Files:**
- Modify: `include/gitops/GitRepoManager.hpp`
- Modify: `src/gitops/GitRepoManager.cpp`

**Step 1:** Add two methods:

```cpp
/// Read a file from a mirror's working directory.
std::string readFile(int64_t iRepoId, const std::string& sRelativePath);

/// Write a file and commit+push via the mirror.
void writeAndCommit(int64_t iRepoId, const std::string& sRelativePath,
                    const std::string& sContent, const std::string& sCommitMessage);
```

`readFile`: find mirror, construct full path from mirror's local path + relative path, read via `std::ifstream`, throw `NotFoundError` if missing.

`writeAndCommit`: find mirror, call `mirror->commitSnapshot(sRelativePath, sContent, sCommitMessage)`.

**Step 2:** Build: `cmake --build build --parallel`

**Step 3:** Commit: `feat(gitops): add readFile and writeAndCommit to GitRepoManager`

---

### Task 12: Wire Up in main.cpp

**Files:**
- Modify: `src/main.cpp`

**Step 1:** Add includes:
```cpp
#include "core/BackupService.hpp"
#include "api/routes/BackupRoutes.hpp"
```

**Step 2:** After all repositories are constructed (~line 294), create `BackupService`:
```cpp
auto backupService = std::make_unique<dns::core::BackupService>(
    *cpPool, *settingsRepo, *roleRepo, *grRepo, *urRepo, *idpRepo,
    *gitRepoRepo, *prRepo, *vrRepo, *zrRepo, *rrRepo, *varRepo);
```

**Step 3:** After route objects (~line 451), create `BackupRoutes`:
```cpp
auto backupRoutes = std::make_unique<dns::api::routes::BackupRoutes>(
    *backupService, *settingsRepo, *amMiddleware, upGitRepoManager.get());
```

**Step 4:** Register routes after `samlRoutes->registerRoutes(crowApp)` (~line 469):
```cpp
backupRoutes->registerRoutes(crowApp);
```

**Step 5:** Add optional scheduled backup maintenance task (after sync-check, ~line 409):
```cpp
{
  int iBackupInterval = settingsRepo->getInt("backup.auto_interval_seconds", 0);
  if (iBackupInterval > 0) {
    msScheduler->schedule("config-backup",
        std::chrono::seconds(iBackupInterval),
        [&bsService = *backupService, &stRepo = *settingsRepo,
         &grmgr = *upGitRepoManager]() {
          auto sRepoId = stRepo.getValue("backup.git_repo_id", "");
          if (sRepoId.empty()) return;
          auto jExport = bsService.exportSystem("system/scheduled");
          auto sPath = stRepo.getValue("backup.git_path",
                                       "_system/config-backup.json");
          grmgr.writeAndCommit(std::stoll(sRepoId), sPath,
                               jExport.dump(2), "Scheduled config backup");
        });
  }
}
```

**Step 6:** Build: `cmake --build build --parallel`

**Step 7:** Commit: `feat(backup): wire BackupService and BackupRoutes into startup`

---

### Task 13: API Client Module

**Files:**
- Create: `ui/src/api/backup.ts`

**Step 1:** Implement typed API functions following `ui/src/api/client.ts` patterns:

```typescript
import { get, post } from './client'

export interface RestoreSummary {
  entity_type: string
  created: number
  updated: number
  skipped: number
}

export interface RestoreResult {
  applied: boolean
  summaries: RestoreSummary[]
  credential_warnings: string[]
}

export async function downloadBackup(commitToGit = false): Promise<void> {
  const token = localStorage.getItem('jwt')
  const url = `/api/v1/backup/export${commitToGit ? '?commit_to_git=true' : ''}`
  const resp = await fetch(url, { headers: { Authorization: `Bearer ${token}` } })
  const blob = await resp.blob()
  const a = document.createElement('a')
  a.href = URL.createObjectURL(blob)
  a.download = `meridian-backup-${new Date().toISOString().slice(0, 10)}.json`
  a.click()
  URL.revokeObjectURL(a.href)
}

export function restoreFromFile(json: string, apply = false): Promise<RestoreResult> {
  return post<RestoreResult>(`/backup/restore${apply ? '?apply=true' : ''}`, JSON.parse(json))
}

export function restoreFromRepo(apply = false): Promise<RestoreResult> {
  return post<RestoreResult>(`/backup/restore-from-repo${apply ? '?apply=true' : ''}`)
}

export function downloadZoneExport(zoneId: number): Promise<void> {
  // Similar blob download pattern
}

export function importZone(zoneId: number, json: string): Promise<RestoreResult> {
  return post<RestoreResult>(`/zones/${zoneId}/import`, JSON.parse(json))
}
```

**Step 2:** Commit: `feat(ui): add backup API client module`

---

### Task 14: BackupRestoreView.vue

**Files:**
- Create: `ui/src/views/BackupRestoreView.vue`

**Step 1:** Create admin-only view with card-based sections (following `SettingsView.vue` pattern):

**Section 1 — Export:**
- "Export Configuration" Button (calls `downloadBackup()`)
- "Commit to GitOps" Checkbox (shown only if backup repo configured — check via settings API or hardcode visibility)
- Loading state during export

**Section 2 — Restore from File:**
- FileUpload area (PrimeVue `FileUpload` in basic mode, or custom `<input type="file">`)
- On file select: read via `FileReader.readAsText()`, call `restoreFromFile(content, false)` for preview
- Preview DataTable showing `RestoreSummary[]` — columns: Entity Type, Created, Updated, Skipped
- "Apply Restore" Button (calls `restoreFromFile(content, true)`)
- Post-restore: credential warnings list with router-links to provider/git-repo/IdP edit pages

**Section 3 — Restore from Git Repository:**
- "Restore from Repo" Button (calls `restoreFromRepo(false)` for preview)
- Same preview/apply pattern as Section 2

Use `PageHeader` component with title "Backup & Restore" and icon `pi pi-download`.

**Step 2:** Commit: `feat(ui): add Backup & Restore admin view`

---

### Task 15: Router + Sidebar Navigation

**Files:**
- Modify: `ui/src/router/index.ts` — add route under administration section (~line 108):
  ```typescript
  {
    path: 'admin/backup',
    name: 'admin-backup',
    component: () => import('../views/BackupRestoreView.vue'),
  },
  ```

- Modify: `ui/src/components/layout/AppSidebar.vue` — add to `adminNavItems` array (~line 23):
  ```typescript
  { label: 'Backup & Restore', icon: 'pi pi-download', to: '/admin/backup' },
  ```

**Step 1:** Make both edits.

**Step 2:** Verify: `cd ui && npm run build`

**Step 3:** Commit: `feat(ui): add backup/restore route and sidebar navigation`

---

### Task 16: Zone Export Button on ZoneDetailView

**Files:**
- Modify: `ui/src/views/ZoneDetailView.vue`

**Step 1:** Add an "Export Zone" button in the page header actions area (alongside existing Deploy button). On click, call `downloadZoneExport(zoneId)`.

**Step 2:** Verify: `cd ui && npm run build`

**Step 3:** Commit: `feat(ui): add zone export button to ZoneDetailView`

---

## Verification

### Backend
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
build/tests/dns-tests --gtest_filter=BackupFormat*
build/tests/dns-tests --gtest_filter=BackupRestore*  # requires DNS_DB_URL
```

### Frontend
```bash
cd ui && npm run build  # no TypeScript errors
cd ui && npm run dev    # manual test: navigate to /admin/backup
```

### Manual E2E
1. Start the app with a populated database
2. Navigate to Administration > Backup & Restore
3. Click "Export Configuration" — verify JSON file downloads with all entity types, no credentials
4. Delete or modify some entities
5. Upload the exported JSON — verify preview shows correct create/update/skip counts
6. Click "Apply Restore" — verify entities restored, credential warnings shown
7. Test zone export from ZoneDetailView — verify zone JSON downloads
8. If GitOps configured: test "Commit to GitOps" checkbox and "Restore from Repo"
