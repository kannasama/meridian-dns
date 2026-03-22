# UI Improvements: Drawer→Dialog, Tags Fix, User Preferences — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace all Drawer components with Dialog modals, fix and relocate the tags feature, add tag creation in admin, implement DB-backed user preferences, and add zone categorization (forward/reverse/all).

**Architecture:** Frontend-first approach — mechanical Drawer→Dialog migration across 12 views, then tags fixes (free-form entry, relocation to zone edit dialog, admin creation with new backend endpoint). Backend adds a `user_preferences` table with repository + REST API. Frontend adds a Pinia preferences store loaded on auth, wired to zone filters and theme. Zone categorization is UI-only detection by name suffix.

**Tech Stack:** Vue 3 + TypeScript + PrimeVue (Dialog, AutoComplete, SelectButton), Pinia, C++20 backend (Crow, libpqxx, nlohmann/json), PostgreSQL.

**Design doc:** `docs/plans/2026-03-22-ui-drawer-tags-preferences-design.md`

---

## Task 1: Drawer → Dialog Migration (12 Views)

Mechanical replacement across all 12 views that use `Drawer`. Each follows the same pattern.

**Files to modify:**
- `ui/src/views/ZonesView.vue`
- `ui/src/views/ProvidersView.vue`
- `ui/src/views/ViewsView.vue`
- `ui/src/views/VariablesView.vue`
- `ui/src/views/ProviderDefinitionsView.vue`
- `ui/src/views/SnippetsView.vue`
- `ui/src/views/SoaPresetsView.vue`
- `ui/src/views/TemplatesView.vue`
- `ui/src/views/GroupsView.vue`
- `ui/src/views/AdminAuthView.vue`
- `ui/src/views/GitReposView.vue`
- `ui/src/views/UsersView.vue`

### Pattern per file

Each view follows the exact same transformation. Use this as a checklist:

**Step 1: Update import**

Replace:
```typescript
import Drawer from 'primevue/drawer'
```
With:
```typescript
import Dialog from 'primevue/dialog'
```

Note: Some views (ZonesView, ZoneDetailView) already import Dialog for other purposes. In those cases, just remove the Drawer import line — Dialog is already available.

**Step 2: Rename state variable**

Replace all occurrences in the `<script>` section:
```typescript
drawerVisible → dialogVisible
```

This affects: the `ref()` declaration, `openCreate()`, `openEdit()`, `handleSubmit()`, and any other function that sets it.

**Step 3: Replace template component**

Replace the Drawer template block:
```vue
<Drawer
  v-model:visible="drawerVisible"
  :header="editingId ? 'Edit X' : 'Add X'"
  position="right"
  class="w-25rem"
>
  <!-- form contents unchanged -->
</Drawer>
```

With Dialog:
```vue
<Dialog
  v-model:visible="dialogVisible"
  :header="editingId ? 'Edit X' : 'Add X'"
  modal
  class="w-30rem"
>
  <!-- form contents unchanged -->
</Dialog>
```

**Step 4: Update CSS**

- Remove `.w-25rem { width: 25rem; }` from `<style scoped>` (Dialog uses PrimeVue utility classes)
- Rename `.drawer-form` → `.dialog-form` in both the template `class=` and the `<style>` block

### Dialog width per view

| View | Width Class | Rationale |
|------|------------|-----------|
| SoaPresetsView | `w-30rem` | Numeric fields |
| SnippetsView | `w-40rem` | Nested record forms |
| TemplatesView | `w-40rem` | Snippet picker dual-panel |
| ZonesView | `w-30rem` | Zone settings + tags (added in Task 3) |
| VariablesView | `w-30rem` | Simple key-value |
| ProvidersView | `w-30rem` | Provider config |
| ViewsView | `w-30rem` | View + provider attach |
| GroupsView | `w-30rem` | Group CRUD |
| GitReposView | `w-30rem` | Git repo config |
| ProviderDefinitionsView | `w-30rem` | Provider definitions |
| AdminAuthView | `w-30rem` | User/group/role management |
| UsersView | `w-30rem` | User CRUD |

**Step 5: Verify in browser**

Run: `cd ui && npm run dev`
Open each view, click Add/Edit, confirm the dialog opens centered as a modal.

**Step 6: Commit**

```bash
git add ui/src/views/*.vue
git commit -m "refactor(ui): replace Drawer with Dialog across all 12 views"
```

---

## Task 2: Fix AutoComplete Free-Form Tag Entry

**Problem:** PrimeVue `AutoComplete` with `multiple` mode doesn't commit free-text entries when no suggestions match. Typing a new tag and pressing Enter does nothing.

**Files to modify:**
- `ui/src/views/ZoneDetailView.vue` (temporary — will be moved in Task 3)

### Step 1: Add keydown handler to AutoComplete

In `ZoneDetailView.vue`, find the `onTagSearch` function (around line 449) and add a new handler after it:

```typescript
function onTagKeydown(event: KeyboardEvent) {
  if (event.key !== 'Enter') return
  const input = (event.target as HTMLInputElement).value?.trim()
  if (!input) return
  if (!zoneTags.value.includes(input)) {
    zoneTags.value.push(input)
  }
  // Clear the input
  ;(event.target as HTMLInputElement).value = ''
  event.preventDefault()
}
```

### Step 2: Wire handler to template

Find the AutoComplete template (around line 691):

```vue
<AutoComplete
  v-model="zoneTags"
  :suggestions="tagSuggestions"
  multiple
  @complete="onTagSearch"
  class="flex-1"
/>
```

Add the keydown handler:

```vue
<AutoComplete
  v-model="zoneTags"
  :suggestions="tagSuggestions"
  multiple
  @complete="onTagSearch"
  @keydown.enter="onTagKeydown"
  class="flex-1"
/>
```

### Step 3: Test manually

1. Navigate to a zone detail page
2. Type a tag name that doesn't exist in suggestions
3. Press Enter — tag should be added as a chip
4. Type a duplicate — it should not be added again

### Step 4: Commit

```bash
git add ui/src/views/ZoneDetailView.vue
git commit -m "fix(ui): allow free-form tag entry in AutoComplete"
```

---

## Task 3: Move Tag Editing to Zone Edit Dialog

**Problem:** Tag editing lives in `ZoneDetailView.vue` (zone records page) — wrong context for metadata editing. Should be in the zone edit Dialog in `ZonesView.vue`.

**Files to modify:**
- `ui/src/views/ZoneDetailView.vue` — remove tags section
- `ui/src/views/ZonesView.vue` — add tags to zone edit Dialog

### Step 1: Remove tags section from ZoneDetailView

In `ui/src/views/ZoneDetailView.vue`:

1. Remove the import: `import { listTags } from '../api/tags'`
2. Remove state variables (around line 66-68):
   ```typescript
   const zoneTags = ref<string[]>([])
   const tagSuggestions = ref<string[]>([])
   let allTagNames: string[] = []
   ```
3. Remove the `listTags()` call from `onMounted` / `fetchData` (around line 162)
4. Remove the `onTagSearch` function (around line 449-453)
5. Remove the `onTagKeydown` function (added in Task 2)
6. Remove the `saveTags` function (around line 455-464)
7. Remove the template block (around line 685-699):
   ```vue
   <div class="tags-section mb-4">
     ...
   </div>
   ```

### Step 2: Add tags field to ZonesView form

In `ui/src/views/ZonesView.vue`:

1. Add AutoComplete import (it's not currently imported):
   ```typescript
   import AutoComplete from 'primevue/autocomplete'
   ```

2. Add tags state to the form object (around line 69-78). Add `tags` field:
   ```typescript
   const form = ref({
     name: '',
     view_id: null as number | null,
     deployment_retention: null as number | null,
     manage_soa: false,
     manage_ns: false,
     soa_preset_id: null as number | null,
     git_repo_id: null as number | null,
     git_branch: '' as string,
     tags: [] as string[],
   })
   ```

3. Add tag autocomplete state (after `selectedTagFilters`):
   ```typescript
   const tagSuggestions = ref<string[]>([])
   ```

4. Add tag search handler:
   ```typescript
   function onTagSearch(event: { query: string }) {
     tagSuggestions.value = allTags.value.filter(t =>
       t.toLowerCase().includes(event.query.toLowerCase())
     )
   }

   function onTagKeydown(event: KeyboardEvent) {
     if (event.key !== 'Enter') return
     const input = (event.target as HTMLInputElement).value?.trim()
     if (!input) return
     if (!form.value.tags.includes(input)) {
       form.value.tags.push(input)
     }
     ;(event.target as HTMLInputElement).value = ''
     event.preventDefault()
   }
   ```

5. Update `openCreate()` to reset tags:
   ```typescript
   function openCreate() {
     editingId.value = null
     form.value = { name: '', view_id: null, deployment_retention: null, manage_soa: false, manage_ns: false, soa_preset_id: null, git_repo_id: null, git_branch: '', tags: [] }
     dialogVisible.value = true
   }
   ```

6. Update `openEdit()` to load zone tags:
   ```typescript
   function openEdit(zone: Zone) {
     editingId.value = zone.id
     form.value = {
       name: zone.name,
       view_id: zone.view_id,
       deployment_retention: zone.deployment_retention,
       manage_soa: zone.manage_soa,
       manage_ns: zone.manage_ns,
       soa_preset_id: zone.soa_preset_id ?? null,
       git_repo_id: zone.git_repo_id,
       git_branch: zone.git_branch || '',
       tags: [...(zone.tags ?? [])],
     }
     dialogVisible.value = true
   }
   ```

7. Update `handleSubmit()` to save tags after zone update:
   ```typescript
   async function handleSubmit() {
     let ok: boolean
     const gitBranch = form.value.git_branch.trim() || null
     if (editingId.value !== null) {
       ok = await update(editingId.value, {
         name: form.value.name,
         view_id: form.value.view_id,
         deployment_retention: form.value.deployment_retention,
         manage_soa: form.value.manage_soa,
         manage_ns: form.value.manage_ns,
         soa_preset_id: form.value.soa_preset_id,
         git_repo_id: form.value.git_repo_id,
         git_branch: gitBranch,
       })
       if (ok) {
         await zoneApi.updateZoneTags(editingId.value, form.value.tags)
       }
     } else {
       ok = await create({
         name: form.value.name,
         view_id: form.value.view_id!,
         deployment_retention: form.value.deployment_retention,
         manage_soa: form.value.manage_soa,
         manage_ns: form.value.manage_ns,
         soa_preset_id: form.value.soa_preset_id,
         git_repo_id: form.value.git_repo_id,
         git_branch: gitBranch,
       })
       // For new zones, update tags after creation if any were specified
       if (ok && form.value.tags.length > 0) {
         const createdZone = zones.value.find(z => z.name === form.value.name)
         if (createdZone) {
           await zoneApi.updateZoneTags(createdZone.id, form.value.tags)
           await fetchZones()
         }
       }
     }
     if (ok) dialogVisible.value = false
   }
   ```

### Step 3: Add tags field to Dialog template

In the Dialog form, add a tags field after the GitOps section and before the submit button:

```vue
<div class="field">
  <label>Tags</label>
  <AutoComplete
    v-model="form.tags"
    :suggestions="tagSuggestions"
    multiple
    @complete="onTagSearch"
    @keydown.enter="onTagKeydown"
    class="w-full"
    placeholder="Add tags..."
  />
</div>
```

### Step 4: Verify in browser

1. Open Zones page, click Edit on a zone with tags — tags should appear as chips
2. Add a new tag by typing and pressing Enter
3. Remove a tag by clicking its X
4. Submit — zone and tags should save
5. Create a new zone with tags — tags should be saved after creation

### Step 5: Commit

```bash
git add ui/src/views/ZoneDetailView.vue ui/src/views/ZonesView.vue
git commit -m "refactor(ui): move tag editing from zone detail to zone edit dialog"
```

---

## Task 4: Add Tag Creation in Admin

**Problem:** `TagsView.vue` only supports rename and delete — no way to create tags directly.

**Files to modify:**
- Backend:
  - `include/dal/TagRepository.hpp` — add `create()` method declaration
  - `src/dal/TagRepository.cpp` — implement `create()`
  - `include/api/routes/TagRoutes.hpp` — no changes needed (already has TagRepository ref)
  - `src/api/routes/TagRoutes.cpp` — add `POST /api/v1/tags` endpoint
- Frontend:
  - `ui/src/api/tags.ts` — add `createTag()` function
  - `ui/src/views/TagsView.vue` — add "New Tag" button + create dialog

### Step 1: Add create method to TagRepository header

In `include/dal/TagRepository.hpp`, add after `findById`:

```cpp
/// Create a single tag in the vocabulary. Returns the new row.
/// Throws ConflictError if name already exists.
TagRow create(const std::string& sName);
```

### Step 2: Implement create in TagRepository

In `src/dal/TagRepository.cpp`, add before the closing namespace brace:

```cpp
TagRow TagRepository::create(const std::string& sName) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  try {
    auto result = txn.exec(
        "INSERT INTO tags (name, created_at) VALUES ($1, NOW()) "
        "RETURNING id, name, EXTRACT(EPOCH FROM created_at)::bigint",
        pqxx::params{sName});
    txn.commit();

    TagRow tr;
    tr.iId         = result[0][0].as<int64_t>();
    tr.sName       = result[0][1].as<std::string>();
    tr.tpCreatedAt = std::chrono::system_clock::time_point(
        std::chrono::seconds(result[0][2].as<int64_t>()));
    tr.iZoneCount  = 0;
    return tr;
  } catch (const pqxx::unique_violation&) {
    throw dns::common::ConflictError("TAG_NAME_EXISTS", "Tag name already exists");
  }
}
```

### Step 3: Add POST /api/v1/tags endpoint

In `src/api/routes/TagRoutes.cpp`, in the `registerRoutes` method, add before the PUT route:

```cpp
// POST /api/v1/tags
CROW_ROUTE(app, "/api/v1/tags").methods("POST"_method)(
    [this](const crow::request& req) -> crow::response {
      try {
        auto rcCtx = authenticate(_amMiddleware, req);
        requirePermission(rcCtx, Permissions::kUsersEdit);
        enforceBodyLimit(req);
        auto jBody = nlohmann::json::parse(req.body);

        std::string sName = jBody.value("name", "");
        if (sName.empty() || sName.size() > 64) {
          throw common::ValidationError("INVALID_TAG",
              "Tag name must be 1-64 characters");
        }

        auto trRow = _trRepo.create(sName);
        return jsonResponse(201, tagRowToJson(trRow));
      } catch (const common::AppError& e) {
        return errorResponse(e);
      } catch (const nlohmann::json::exception&) {
        return invalidJsonResponse();
      }
    });
```

### Step 4: Add createTag to frontend API client

In `ui/src/api/tags.ts`, add the `post` import and new function:

```typescript
import { get, post, put, del } from './client'
import type { Tag } from '../types'

export function listTags(): Promise<Tag[]> {
  return get('/tags')
}

export function createTag(name: string): Promise<Tag> {
  return post('/tags', { name })
}

export function renameTag(id: number, name: string): Promise<Tag> {
  return put(`/tags/${id}`, { name })
}

export function deleteTag(id: number): Promise<void> {
  return del(`/tags/${id}`)
}
```

### Step 5: Add "New Tag" button and create dialog to TagsView

Replace `ui/src/views/TagsView.vue` script and template sections. Key changes:

1. Add missing PrimeVue imports:
   ```typescript
   import DataTable from 'primevue/datatable'
   import Column from 'primevue/column'
   import Button from 'primevue/button'
   import Dialog from 'primevue/dialog'
   import InputText from 'primevue/inputtext'
   import PageHeader from '../components/shared/PageHeader.vue'
   ```

2. Import `createTag`:
   ```typescript
   import { listTags, createTag, renameTag, deleteTag } from '../api/tags'
   ```

3. Add create state:
   ```typescript
   const showCreateDialog = ref(false)
   const newTagName = ref('')
   ```

4. Add create function:
   ```typescript
   async function submitCreate() {
     const name = newTagName.value.trim()
     if (!name) return
     try {
       await createTag(name)
       notify.success('Tag created')
       showCreateDialog.value = false
       newTagName.value = ''
       await loadTags()
     } catch {
       notify.error('Failed to create tag')
     }
   }
   ```

5. Replace the header with PageHeader + "New Tag" button:
   ```vue
   <PageHeader title="Tags" subtitle="Tag vocabulary for zone categorization">
     <Button label="New Tag" icon="pi pi-plus" @click="showCreateDialog = true" />
   </PageHeader>
   ```

6. Add create dialog after the rename dialog:
   ```vue
   <Dialog v-model:visible="showCreateDialog" header="New Tag" modal>
     <div class="dialog-body">
       <InputText v-model="newTagName" placeholder="Tag name" class="w-full" @keydown.enter="submitCreate" />
       <div class="flex justify-end gap-2">
         <Button label="Cancel" severity="secondary" @click="showCreateDialog = false" />
         <Button label="Create" @click="submitCreate" />
       </div>
     </div>
   </Dialog>
   ```

### Step 6: Build and verify

Build the C++ backend:
```bash
docker buildx build --load -t meridian-dns .
```

Verify the UI:
```bash
cd ui && npm run dev
```
Navigate to Admin > Tags, click "New Tag", enter a name, submit. Tag should appear in the list.

### Step 7: Commit

```bash
git add include/dal/TagRepository.hpp src/dal/TagRepository.cpp \
  src/api/routes/TagRoutes.cpp ui/src/api/tags.ts ui/src/views/TagsView.vue
git commit -m "feat: add tag creation endpoint and admin UI"
```

---

## Task 5: Database Migration — user_preferences Table

**Files to create:**
- `scripts/db/v019/001_user_preferences.sql`

### Step 1: Create the migration file

```bash
mkdir -p scripts/db/v019
```

### Step 2: Write the migration SQL

Create `scripts/db/v019/001_user_preferences.sql`:

```sql
-- User preferences: per-user key-value settings stored as JSONB
CREATE TABLE user_preferences (
  user_id   BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  key       TEXT NOT NULL,
  value     JSONB NOT NULL DEFAULT '""',
  PRIMARY KEY (user_id, key)
);
```

### Step 3: Verify migration applies

```bash
docker buildx build --load -t meridian-dns .
```

The MigrationRunner auto-discovers `v019/` and applies it on startup.

### Step 4: Commit

```bash
git add scripts/db/v019/001_user_preferences.sql
git commit -m "feat: add user_preferences table (v019 migration)"
```

---

## Task 6: User Preferences Backend — Repository + API

**Files to create:**
- `include/dal/UserPreferenceRepository.hpp`
- `src/dal/UserPreferenceRepository.cpp`
- `include/api/routes/PreferenceRoutes.hpp`
- `src/api/routes/PreferenceRoutes.cpp`

**Files to modify:**
- `src/api/ApiServer.hpp` — add PreferenceRoutes member
- `src/api/ApiServer.cpp` — construct and register PreferenceRoutes
- `src/main.cpp` — construct UserPreferenceRepository, pass to ApiServer
- `src/CMakeLists.txt` — add new source files

### Step 1: Create UserPreferenceRepository header

Create `include/dal/UserPreferenceRepository.hpp`:

```cpp
#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace dns::dal {

class ConnectionPool;

/// Manages the user_preferences table.
/// Class abbreviation: upr
class UserPreferenceRepository {
 public:
  explicit UserPreferenceRepository(ConnectionPool& cpPool);
  ~UserPreferenceRepository();

  /// Get all preferences for a user as key→value map.
  std::map<std::string, nlohmann::json> getAll(int64_t iUserId);

  /// Get a single preference value. Returns nullopt if not set.
  std::optional<nlohmann::json> get(int64_t iUserId, const std::string& sKey);

  /// Upsert a single preference.
  void set(int64_t iUserId, const std::string& sKey, const nlohmann::json& jValue);

  /// Batch upsert multiple preferences.
  void setAll(int64_t iUserId, const std::map<std::string, nlohmann::json>& mPrefs);

  /// Delete a single preference key.
  void deleteKey(int64_t iUserId, const std::string& sKey);

 private:
  ConnectionPool& _cpPool;
};

}  // namespace dns::dal
```

### Step 2: Implement UserPreferenceRepository

Create `src/dal/UserPreferenceRepository.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "dal/UserPreferenceRepository.hpp"

#include "dal/ConnectionPool.hpp"

#include <pqxx/pqxx>

namespace dns::dal {

UserPreferenceRepository::UserPreferenceRepository(ConnectionPool& cpPool)
    : _cpPool(cpPool) {}
UserPreferenceRepository::~UserPreferenceRepository() = default;

std::map<std::string, nlohmann::json> UserPreferenceRepository::getAll(int64_t iUserId) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT key, value FROM user_preferences WHERE user_id = $1",
      pqxx::params{iUserId});
  txn.commit();

  std::map<std::string, nlohmann::json> mPrefs;
  for (const auto& row : result) {
    mPrefs[row[0].as<std::string>()] = nlohmann::json::parse(row[1].as<std::string>());
  }
  return mPrefs;
}

std::optional<nlohmann::json> UserPreferenceRepository::get(
    int64_t iUserId, const std::string& sKey) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  auto result = txn.exec(
      "SELECT value FROM user_preferences WHERE user_id = $1 AND key = $2",
      pqxx::params{iUserId, sKey});
  txn.commit();

  if (result.empty()) return std::nullopt;
  return nlohmann::json::parse(result[0][0].as<std::string>());
}

void UserPreferenceRepository::set(
    int64_t iUserId, const std::string& sKey, const nlohmann::json& jValue) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  txn.exec(
      "INSERT INTO user_preferences (user_id, key, value) VALUES ($1, $2, $3::jsonb) "
      "ON CONFLICT (user_id, key) DO UPDATE SET value = EXCLUDED.value",
      pqxx::params{iUserId, sKey, jValue.dump()});
  txn.commit();
}

void UserPreferenceRepository::setAll(
    int64_t iUserId, const std::map<std::string, nlohmann::json>& mPrefs) {
  if (mPrefs.empty()) return;
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  for (const auto& [sKey, jValue] : mPrefs) {
    txn.exec(
        "INSERT INTO user_preferences (user_id, key, value) VALUES ($1, $2, $3::jsonb) "
        "ON CONFLICT (user_id, key) DO UPDATE SET value = EXCLUDED.value",
        pqxx::params{iUserId, sKey, jValue.dump()});
  }
  txn.commit();
}

void UserPreferenceRepository::deleteKey(int64_t iUserId, const std::string& sKey) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);
  txn.exec(
      "DELETE FROM user_preferences WHERE user_id = $1 AND key = $2",
      pqxx::params{iUserId, sKey});
  txn.commit();
}

}  // namespace dns::dal
```

### Step 3: Create PreferenceRoutes header

Create `include/api/routes/PreferenceRoutes.hpp`:

```cpp
#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include <crow.h>

namespace dns::dal {
class UserPreferenceRepository;
}

namespace dns::api {
class AuthMiddleware;
}

namespace dns::api::routes {

/// Handlers for /api/v1/preferences
/// Class abbreviation: prefr
class PreferenceRoutes {
 public:
  PreferenceRoutes(dns::dal::UserPreferenceRepository& uprRepo,
                   const dns::api::AuthMiddleware& amMiddleware);
  ~PreferenceRoutes();

  void registerRoutes(crow::SimpleApp& app);

 private:
  dns::dal::UserPreferenceRepository& _uprRepo;
  const dns::api::AuthMiddleware& _amMiddleware;
};

}  // namespace dns::api::routes
```

### Step 4: Implement PreferenceRoutes

Create `src/api/routes/PreferenceRoutes.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

#include "api/routes/PreferenceRoutes.hpp"

#include "api/AuthMiddleware.hpp"
#include "api/RouteHelpers.hpp"
#include "common/Errors.hpp"
#include "dal/UserPreferenceRepository.hpp"

#include <nlohmann/json.hpp>

namespace dns::api::routes {
using namespace dns::common;

PreferenceRoutes::PreferenceRoutes(
    dns::dal::UserPreferenceRepository& uprRepo,
    const dns::api::AuthMiddleware& amMiddleware)
    : _uprRepo(uprRepo), _amMiddleware(amMiddleware) {}

PreferenceRoutes::~PreferenceRoutes() = default;

void PreferenceRoutes::registerRoutes(crow::SimpleApp& app) {
  // GET /api/v1/preferences
  CROW_ROUTE(app, "/api/v1/preferences").methods("GET"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);

          auto mPrefs = _uprRepo.getAll(rcCtx.iUserId);
          nlohmann::json jResult = nlohmann::json::object();
          for (const auto& [sKey, jValue] : mPrefs) {
            jResult[sKey] = jValue;
          }
          return jsonResponse(200, jResult);
        } catch (const common::AppError& e) {
          return errorResponse(e);
        }
      });

  // PUT /api/v1/preferences
  CROW_ROUTE(app, "/api/v1/preferences").methods("PUT"_method)(
      [this](const crow::request& req) -> crow::response {
        try {
          auto rcCtx = authenticate(_amMiddleware, req);
          enforceBodyLimit(req);
          auto jBody = nlohmann::json::parse(req.body);

          if (!jBody.is_object()) {
            throw common::ValidationError("INVALID_BODY",
                "Request body must be a JSON object");
          }

          // Validate key count and key lengths
          if (jBody.size() > 50) {
            throw common::ValidationError("TOO_MANY_KEYS",
                "Maximum 50 preference keys allowed");
          }

          std::map<std::string, nlohmann::json> mPrefs;
          for (auto& [sKey, jValue] : jBody.items()) {
            if (sKey.empty() || sKey.size() > 64) {
              throw common::ValidationError("INVALID_KEY",
                  "Preference key must be 1-64 characters");
            }
            mPrefs[sKey] = jValue;
          }

          _uprRepo.setAll(rcCtx.iUserId, mPrefs);
          return jsonResponse(200, {{"message", "Preferences updated"}});
        } catch (const common::AppError& e) {
          return errorResponse(e);
        } catch (const nlohmann::json::exception&) {
          return invalidJsonResponse();
        }
      });
}

}  // namespace dns::api::routes
```

### Step 5: Wire into ApiServer and main.cpp

This step requires modifying the existing ApiServer constructor and main.cpp to:
1. Add `UserPreferenceRepository` construction in main.cpp (alongside other repositories)
2. Add `PreferenceRoutes` construction in main.cpp
3. Pass `PreferenceRoutes&` to ApiServer constructor
4. Call `_prefrRoutes.registerRoutes(_app)` in `ApiServer::registerRoutes()`
5. Add new `.cpp` files to `src/CMakeLists.txt`

Follow the exact pattern used for TagRoutes:
- In `src/api/ApiServer.cpp`: add `#include "api/routes/PreferenceRoutes.hpp"`, add constructor parameter, store as `_prefrRoutes` member, register in `registerRoutes()`
- In `include/api/ApiServer.hpp`: forward-declare, add member reference
- In `src/main.cpp`: construct `UserPreferenceRepository uprRepo(cpPool)`, construct `PreferenceRoutes prefrRoutes(uprRepo, amMiddleware)`, pass to ApiServer

### Step 6: Build and verify

```bash
docker buildx build --load -t meridian-dns .
```

### Step 7: Commit

```bash
git add include/dal/UserPreferenceRepository.hpp src/dal/UserPreferenceRepository.cpp \
  include/api/routes/PreferenceRoutes.hpp src/api/routes/PreferenceRoutes.cpp \
  include/api/ApiServer.hpp src/api/ApiServer.cpp src/main.cpp src/CMakeLists.txt
git commit -m "feat: add user preferences backend (repository + REST API)"
```

---

## Task 7: User Preferences Frontend — Pinia Store + API

**Files to create:**
- `ui/src/stores/preferences.ts`
- `ui/src/api/preferences.ts`

**Files to modify:**
- `ui/src/App.vue` (or wherever auth hydration happens) — fetch preferences after auth

### Step 1: Create preferences API client

Create `ui/src/api/preferences.ts`:

```typescript
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import { get, put } from './client'

export function getPreferences(): Promise<Record<string, unknown>> {
  return get('/preferences')
}

export function savePreferences(prefs: Record<string, unknown>): Promise<{ message: string }> {
  return put('/preferences', prefs)
}
```

### Step 2: Create preferences Pinia store

Create `ui/src/stores/preferences.ts`:

```typescript
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Meridian DNS Contributors
// This file is part of Meridian DNS. See LICENSE for details.

import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { getPreferences, savePreferences } from '../api/preferences'

export const usePreferencesStore = defineStore('preferences', () => {
  const data = ref<Record<string, unknown>>({})
  const loaded = ref(false)

  async function fetch() {
    try {
      data.value = await getPreferences()
      loaded.value = true
    } catch {
      // Preferences are optional — fail silently
      loaded.value = true
    }
  }

  async function save(key: string, value: unknown) {
    data.value[key] = value
    await savePreferences({ [key]: value })
  }

  async function saveMany(prefs: Record<string, unknown>) {
    Object.assign(data.value, prefs)
    await savePreferences(prefs)
  }

  const zoneHiddenTags = computed<string[]>(() =>
    (data.value.zone_hidden_tags as string[]) ?? []
  )

  const zoneDefaultView = computed<string>(() =>
    (data.value.zone_default_view as string) ?? 'all'
  )

  const theme = computed<string>(() =>
    (data.value.theme as string) ?? 'system'
  )

  return { data, loaded, fetch, save, saveMany, zoneHiddenTags, zoneDefaultView, theme }
})
```

### Step 3: Fetch preferences after auth

Find where the auth store's `hydrate()` is called (typically in `App.vue` or `router/index.ts`). After successful hydration (user is logged in), call:

```typescript
import { usePreferencesStore } from './stores/preferences'

const preferences = usePreferencesStore()

// After auth hydration confirms user is logged in:
if (auth.isAuthenticated) {
  await preferences.fetch()
}
```

Also fetch after login succeeds in the auth store or login flow.

### Step 4: Verify in browser

Open browser devtools Network tab. After login, confirm `GET /api/v1/preferences` is called.

### Step 5: Commit

```bash
git add ui/src/api/preferences.ts ui/src/stores/preferences.ts ui/src/App.vue
git commit -m "feat(ui): add preferences Pinia store with API integration"
```

---

## Task 8: Zone Categorization UI — Forward / Reverse / All

**Files to modify:**
- `ui/src/views/ZonesView.vue`

### Step 1: Add SelectButton import

```typescript
import SelectButton from 'primevue/selectbutton'
```

### Step 2: Add zone category state

After the tag filter state, add:

```typescript
import { usePreferencesStore } from '../stores/preferences'

const preferences = usePreferencesStore()

const zoneCategoryOptions = [
  { label: 'Forward', value: 'forward' },
  { label: 'Reverse', value: 'reverse' },
  { label: 'All', value: 'all' },
]
const zoneCategory = ref(preferences.zoneDefaultView)

const isReverseZone = (name: string) =>
  name.endsWith('.in-addr.arpa') || name.endsWith('.ip6.arpa')
```

### Step 3: Update the filteredZones computed

Replace the existing `filteredZones` computed with a two-stage filter:

```typescript
const categoryFilteredZones = computed(() => {
  if (zoneCategory.value === 'forward')
    return zones.value.filter(z => !isReverseZone(z.name))
  if (zoneCategory.value === 'reverse')
    return zones.value.filter(z => isReverseZone(z.name))
  return zones.value
})

const filteredZones = computed(() => {
  if (selectedTagFilters.value.length === 0) return categoryFilteredZones.value
  return categoryFilteredZones.value.filter(z =>
    selectedTagFilters.value.every(tag => (z.tags ?? []).includes(tag))
  )
})
```

### Step 4: Add SelectButton to the filter bar

In the template, add the segmented control to the filter bar (before the MultiSelect):

```vue
<div class="filter-bar">
  <SelectButton
    v-model="zoneCategory"
    :options="zoneCategoryOptions"
    optionLabel="label"
    optionValue="value"
    :allowEmpty="false"
  />
  <MultiSelect
    v-model="selectedTagFilters"
    :options="allTags"
    placeholder="Filter by tag"
    class="tag-filter"
    :showClear="true"
  />
</div>
```

### Step 5: Verify in browser

1. Navigate to Zones page
2. SelectButton should show Forward / Reverse / All
3. Click "Reverse" — only `.in-addr.arpa` and `.ip6.arpa` zones should show
4. Click "Forward" — reverse zones hidden
5. Tag filter should work within the selected category

### Step 6: Commit

```bash
git add ui/src/views/ZonesView.vue
git commit -m "feat(ui): add forward/reverse/all zone categorization filter"
```

---

## Task 9: Persistent Tag Filters — Wire Preferences to Zone List

**Files to modify:**
- `ui/src/views/ZonesView.vue`

### Step 1: Initialize filters from preferences on mount

In the `onMounted` callback, after data is loaded, apply saved preferences:

```typescript
onMounted(async () => {
  await Promise.all([
    fetchZones(),
    viewApi.listViews().then((v) => (allViews.value = v)),
    gitRepoApi.listGitRepos().then((r) => (allGitRepos.value = r)),
    soaPresetApi.listSoaPresets().then((r) => { soaPresets.value = r }).catch(() => {}),
    listTags().then((t) => { allTags.value = t.map(tag => tag.name) }).catch(() => {}),
  ])

  // Apply saved preferences
  if (preferences.loaded) {
    zoneCategory.value = preferences.zoneDefaultView
    if (preferences.zoneHiddenTags.length > 0) {
      // Pre-select tags to exclude — the filter shows zones that MATCH selected tags,
      // so hidden tags work differently. We need to filter OUT zones with hidden tags.
      // Implementation: add a computed that excludes hidden-tagged zones.
    }
  }
})
```

### Step 2: Add "Save as default" button

Add a small "Save as default" link/button next to the filter bar that saves the current category and tag filter state to preferences:

```vue
<Button
  v-if="zoneCategory !== preferences.zoneDefaultView"
  label="Save as default"
  text
  size="small"
  @click="saveFilterDefaults"
/>
```

```typescript
async function saveFilterDefaults() {
  try {
    await preferences.saveMany({
      zone_default_view: zoneCategory.value,
      zone_hidden_tags: selectedTagFilters.value,
    })
    notify.success('Filter defaults saved')
  } catch {
    notify.error('Failed to save defaults')
  }
}
```

### Step 3: Update filteredZones to exclude hidden tags

Add hidden tag exclusion to the computed:

```typescript
const filteredZones = computed(() => {
  let result = categoryFilteredZones.value

  // Exclude zones with hidden tags (from preferences)
  const hiddenTags = preferences.zoneHiddenTags
  if (hiddenTags.length > 0 && selectedTagFilters.value.length === 0) {
    result = result.filter(z =>
      !hiddenTags.some(tag => (z.tags ?? []).includes(tag))
    )
  }

  // Apply explicit tag filter (if user has selected tags)
  if (selectedTagFilters.value.length > 0) {
    result = result.filter(z =>
      selectedTagFilters.value.every(tag => (z.tags ?? []).includes(tag))
    )
  }

  return result
})
```

### Step 4: Verify in browser

1. Set category to "Forward", click "Save as default"
2. Refresh page — should default to "Forward"
3. Check network tab — `PUT /api/v1/preferences` sent with correct payload

### Step 5: Commit

```bash
git add ui/src/views/ZonesView.vue
git commit -m "feat(ui): persist zone filter defaults via user preferences"
```

---

## Task 10: Theme Migration — localStorage to Preferences

**Files to modify:**
- `ui/src/stores/theme.ts`
- `ui/src/stores/preferences.ts` (minor — add theme-related computed if needed)

### Step 1: Migrate theme on first load

In the theme store initialization, after the preferences store is loaded, check if the user has a `theme` preference in the DB. If not but localStorage has theme data, migrate it:

In `ui/src/stores/theme.ts`, add a `migrateToPreferences()` action:

```typescript
import { usePreferencesStore } from './preferences'

async function migrateToPreferences() {
  const preferences = usePreferencesStore()
  if (!preferences.loaded) return

  // Only migrate if DB has no theme preference yet
  if (preferences.data.theme !== undefined) return

  // Read current localStorage values and save to DB
  const themePrefs: Record<string, unknown> = {}
  if (darkMode.value !== undefined) themePrefs.theme_dark_mode = darkMode.value
  if (darkTheme.value) themePrefs.theme_dark_preset = darkTheme.value
  if (lightTheme.value) themePrefs.theme_light_preset = lightTheme.value
  if (accent.value) themePrefs.theme_accent = accent.value

  if (Object.keys(themePrefs).length > 0) {
    await preferences.saveMany(themePrefs)
  }
}
```

### Step 2: Load theme from preferences

When the preferences store loads, if theme preferences exist in the DB, apply them to the theme store instead of reading from localStorage:

```typescript
function loadFromPreferences() {
  const preferences = usePreferencesStore()
  if (!preferences.loaded) return

  const d = preferences.data
  if (d.theme_dark_mode !== undefined) darkMode.value = d.theme_dark_mode as boolean
  if (d.theme_dark_preset) darkTheme.value = d.theme_dark_preset as string
  if (d.theme_light_preset) lightTheme.value = d.theme_light_preset as string
  if (d.theme_accent) accent.value = d.theme_accent as string
}
```

### Step 3: Save theme changes to both localStorage and preferences

Update `toggleDarkMode()`, `setAccent()`, `setDarkTheme()`, `setLightTheme()` to also call `preferences.save()` for the corresponding key. Keep localStorage as a fallback for offline/unauthenticated state.

### Step 4: Verify

1. Log in with existing localStorage theme settings
2. Check that preferences are migrated (network tab shows PUT)
3. Clear localStorage, refresh — theme should load from DB preferences
4. Change theme — both localStorage and DB should update

### Step 5: Commit

```bash
git add ui/src/stores/theme.ts ui/src/stores/preferences.ts
git commit -m "feat(ui): migrate theme storage from localStorage to user preferences"
```

---

## Final Verification

After all 10 tasks are complete:

### Step 1: Full UI build

```bash
cd ui && npm run build
```

Ensure no TypeScript errors.

### Step 2: Docker build

```bash
docker buildx build --load -t meridian-dns .
```

Ensure C++ compiles and tests pass.

### Step 3: Manual smoke test

1. Log in → preferences fetched
2. Open any CRUD view → Dialog opens (not Drawer)
3. Edit a zone → tags field present, free-form entry works
4. Admin > Tags → "New Tag" button works
5. Zone list → Forward/Reverse/All segmented control works
6. Save filter defaults → persisted across page refresh
7. Change theme → persisted to DB

### Step 4: Final commit (if any cleanup needed)

```bash
git commit -m "chore: final cleanup for drawer-to-dialog and preferences"
```
