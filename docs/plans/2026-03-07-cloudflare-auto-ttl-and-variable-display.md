# Cloudflare Auto-TTL + Variable Values Display — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add Cloudflare Auto-TTL support (TTL=1 normalization) and show expanded variable values in the zone records view.

**Architecture:** Two independent features. (1) Auto-TTL adds a `auto_ttl` boolean to `provider_meta` JSONB — no schema migration needed. The DiffEngine normalizes TTL to 1 for Cloudflare providers during preview. CloudflareProvider sends TTL=1 when auto_ttl is set. The TTL field stays editable for non-Cloudflare providers in multi-provider zones. (2) Variable display expands `{{var}}` templates client-side using variables already fetched by `useVariableAutocomplete`.

**Tech Stack:** C++20 (backend), Vue 3 + TypeScript + PrimeVue (frontend)

**Prerequisite fix:** The RecordRepository `create()`/`update()` methods and RecordRoutes `recordRowToJson()` don't pass through `provider_meta`. This must be fixed first as both features depend on it.

---

## Task 1: Fix provider_meta Round-Trip (Backend Prerequisite)

The `provider_meta` JSONB column exists in the database and is read by `listByZoneId()`/`findById()`, but:
- `RecordRoutes::recordRowToJson()` omits it from the API response
- `RecordRoutes` POST/PUT handlers don't extract it from request body
- `RecordRepository::create()` hardcodes `nullptr` for provider_meta
- `RecordRepository::update()` doesn't touch provider_meta at all

**Files:**
- Modify: `include/dal/RecordRepository.hpp`
- Modify: `src/dal/RecordRepository.cpp`
- Modify: `src/api/routes/RecordRoutes.cpp`
- Test: `tests/integration/test_zone_repository.cpp` (existing file with record tests)

### Step 1: Update RecordRepository to accept provider_meta

Add `jProviderMeta` parameter to `create()` and `update()` in the header:

```cpp
// include/dal/RecordRepository.hpp — update signatures

int64_t create(int64_t iZoneId, const std::string& sName, const std::string& sType,
               int iTtl, const std::string& sValueTemplate, int iPriority,
               const nlohmann::json& jProviderMeta = nullptr);

void update(int64_t iId, const std::string& sName, const std::string& sType,
            int iTtl, const std::string& sValueTemplate, int iPriority,
            const nlohmann::json& jProviderMeta = nullptr);
```

Update the implementations in `src/dal/RecordRepository.cpp`:

**`create()`** — change the `nullptr` to use the parameter:

```cpp
// src/dal/RecordRepository.cpp — create() body
// The INSERT already includes provider_meta; just pass the param instead of nullptr
int64_t RecordRepository::create(int64_t iZoneId, const std::string& sName,
                                 const std::string& sType, int iTtl,
                                 const std::string& sValueTemplate, int iPriority,
                                 const nlohmann::json& jProviderMeta) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  try {
    std::optional<std::string> sMetaJson;
    if (!jProviderMeta.is_null()) {
      sMetaJson = jProviderMeta.dump();
    }
    auto result = txn.exec(
        "INSERT INTO records (zone_id, name, type, ttl, value_template, priority, provider_meta) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7::jsonb) RETURNING id",
        pqxx::params{iZoneId, sName, sType, iTtl, sValueTemplate, iPriority, sMetaJson});
    txn.commit();
    return result.one_row()[0].as<int64_t>();
  } catch (const pqxx::foreign_key_violation&) {
    throw common::ValidationError("INVALID_ZONE_ID",
                                  "Zone with id " + std::to_string(iZoneId) + " not found");
  }
}
```

**`update()`** — add provider_meta to the UPDATE SET:

```cpp
void RecordRepository::update(int64_t iId, const std::string& sName,
                              const std::string& sType, int iTtl,
                              const std::string& sValueTemplate, int iPriority,
                              const nlohmann::json& jProviderMeta) {
  auto cg = _cpPool.checkout();
  pqxx::work txn(*cg);

  std::optional<std::string> sMetaJson;
  if (!jProviderMeta.is_null()) {
    sMetaJson = jProviderMeta.dump();
  }
  auto result = txn.exec(
      "UPDATE records SET name = $2, type = $3, ttl = $4, value_template = $5, "
      "priority = $6, provider_meta = $7::jsonb, updated_at = NOW() WHERE id = $1",
      pqxx::params{iId, sName, sType, iTtl, sValueTemplate, iPriority, sMetaJson});
  txn.commit();

  if (result.affected_rows() == 0) {
    throw common::NotFoundError("RECORD_NOT_FOUND",
                                "Record with id " + std::to_string(iId) + " not found");
  }
}
```

### Step 2: Update RecordRoutes to pass through provider_meta

In `src/api/routes/RecordRoutes.cpp`:

**`recordRowToJson()`** — add provider_meta to the JSON output (after the `last_audit_id` block):

```cpp
// Add after the last_audit_id block in recordRowToJson()
if (!row.jProviderMeta.is_null()) {
  j["provider_meta"] = row.jProviderMeta;
} else {
  j["provider_meta"] = nullptr;
}
```

**POST handler** — extract provider_meta and pass to create():

```cpp
// After iPriority extraction in POST handler
nlohmann::json jProviderMeta;
if (jBody.contains("provider_meta") && jBody["provider_meta"].is_object()) {
  jProviderMeta = jBody["provider_meta"];
}

int64_t iId = _rrRepo.create(iZoneId, sName, sType, iTtl,
                             sValueTemplate, iPriority, jProviderMeta);
```

**PUT handler** — extract provider_meta and pass to update():

```cpp
// After iPriority extraction in PUT handler
nlohmann::json jProviderMeta;
if (jBody.contains("provider_meta") && jBody["provider_meta"].is_object()) {
  jProviderMeta = jBody["provider_meta"];
}

_rrRepo.update(iRecordId, sName, sType, iTtl, sValueTemplate, iPriority, jProviderMeta);
```

### Step 3: Build and verify

Run:
```bash
cmake --build build --parallel
```
Expected: Clean compile, no errors.

### Step 4: Run existing tests

Run:
```bash
build/tests/dns-tests --gtest_filter="DiffEngine*"
```
Expected: All existing tests pass (the new default parameter means callers don't change).

### Step 5: Commit

```bash
git add include/dal/RecordRepository.hpp src/dal/RecordRepository.cpp src/api/routes/RecordRoutes.cpp
git commit -m "fix: wire provider_meta through RecordRepository create/update and API response"
```

---

## Task 2: Cloudflare Auto-TTL — DiffEngine Unit Tests

Write failing tests for the TTL normalization behavior before implementing it. The `computeDiff()` static method doesn't know about providers — that's `preview()`'s job. So we test the building block: that when desired records have TTL=1 (normalized before being passed to `computeDiff()`), they match Cloudflare's TTL=1 live records.

**Files:**
- Modify: `tests/unit/test_diff_engine.cpp`

### Step 1: Add test — Auto-TTL records match Cloudflare TTL=1

```cpp
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
```

### Step 2: Add test — Auto-TTL propagates in diff output

```cpp
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
```

### Step 3: Run tests to verify they pass

These tests exercise `computeDiff()` which already works correctly — they verify the contract
that normalized TTL=1 produces no false diffs, and that auto_ttl metadata propagates.

```bash
cmake --build build --parallel && build/tests/dns-tests --gtest_filter="DiffEngine*"
```
Expected: All tests pass (including the two new ones — they test existing correct behavior with new metadata).

### Step 4: Commit

```bash
git add tests/unit/test_diff_engine.cpp
git commit -m "test: add DiffEngine tests for Cloudflare auto_ttl normalization"
```

---

## Task 3: Cloudflare Auto-TTL — CloudflareProvider Changes

Update the provider to tag `auto_ttl` in parsed records and respect it when building request bodies.

**Files:**
- Modify: `src/providers/CloudflareProvider.cpp`

### Step 1: Update parseRecordsResponse to tag auto_ttl

In `src/providers/CloudflareProvider.cpp`, update the metadata extraction in `parseRecordsResponse()` (around line 138-140):

Replace:
```cpp
    // Capture provider-specific metadata
    bool bProxied = jRec.value("proxied", false);
    dr.jProviderMeta = {{"proxied", bProxied}};
```

With:
```cpp
    // Capture provider-specific metadata
    bool bProxied = jRec.value("proxied", false);
    bool bAutoTtl = (dr.uTtl == 1);
    dr.jProviderMeta = {{"proxied", bProxied}, {"auto_ttl", bAutoTtl}};
```

### Step 2: Update buildRecordBody to emit TTL=1 for auto_ttl

In `src/providers/CloudflareProvider.cpp`, update `buildRecordBody()` (around line 189-212):

Replace the TTL line in the json body construction:
```cpp
  json jBody = {
      {"type", drRecord.sType},
      {"name", drRecord.sName},
      {"content", drRecord.sValue},
      {"ttl", drRecord.uTtl},
  };
```

With:
```cpp
  // Auto-TTL: send TTL=1 (Cloudflare "Auto") when auto_ttl is set
  uint32_t uEffectiveTtl = drRecord.uTtl;
  if (!drRecord.jProviderMeta.is_null() && drRecord.jProviderMeta.value("auto_ttl", false)) {
    uEffectiveTtl = 1;
  }

  json jBody = {
      {"type", drRecord.sType},
      {"name", drRecord.sName},
      {"content", drRecord.sValue},
      {"ttl", uEffectiveTtl},
  };
```

### Step 3: Build and run tests

```bash
cmake --build build --parallel && build/tests/dns-tests --gtest_filter="DiffEngine*:Cloudflare*"
```
Expected: All tests pass.

### Step 4: Commit

```bash
git add src/providers/CloudflareProvider.cpp
git commit -m "feat: Cloudflare auto_ttl — tag TTL=1 records and emit TTL=1 on push"
```

---

## Task 4: Cloudflare Auto-TTL — DiffEngine Preview Normalization

When computing per-provider diffs for a Cloudflare provider, normalize desired record TTLs
to 1 for records that have `auto_ttl: true` in their provider_meta. This prevents false TTL
diffs. Non-Cloudflare providers in the same zone still get the original user-specified TTL.

**Files:**
- Modify: `src/core/DiffEngine.cpp`

### Step 1: Add TTL normalization in preview()

In `src/core/DiffEngine.cpp`, inside the `preview()` method's per-provider loop (around line 291), add normalization before calling `computeDiff()`:

Replace:
```cpp
  for (auto& [iProviderId, vLive] : mLive) {
    vLive = filterRecordTypes(vLive, oZone->bManageSoa, oZone->bManageNs);
    auto vDiffs = computeDiff(vDesired, vLive);

    auto oProvider = _prRepo.findById(iProviderId);
```

With:
```cpp
  for (auto& [iProviderId, vLive] : mLive) {
    vLive = filterRecordTypes(vLive, oZone->bManageSoa, oZone->bManageNs);

    auto oProvider = _prRepo.findById(iProviderId);

    // For Cloudflare providers, normalize desired TTL to 1 for auto_ttl records
    // so the diff doesn't flag a false TTL mismatch against Cloudflare's TTL=1
    auto vProviderDesired = vDesired;
    if (oProvider && oProvider->sType == "cloudflare") {
      for (auto& dr : vProviderDesired) {
        if (!dr.jProviderMeta.is_null() && dr.jProviderMeta.value("auto_ttl", false)) {
          dr.uTtl = 1;
        }
      }
    }

    auto vDiffs = computeDiff(vProviderDesired, vLive);
```

Note: `oProvider` lookup moved before `computeDiff()` to access `sType`. It was already looked up after — just reorder, don't duplicate. Remove the second `auto oProvider = _prRepo.findById(iProviderId);` line that follows.

### Step 2: Build and run tests

```bash
cmake --build build --parallel && build/tests/dns-tests --gtest_filter="DiffEngine*"
```
Expected: All tests pass.

### Step 3: Commit

```bash
git add src/core/DiffEngine.cpp
git commit -m "feat: DiffEngine normalizes TTL for Cloudflare auto_ttl records in preview"
```

---

## Task 5: Cloudflare Auto-TTL — Frontend UI

Add the Auto-TTL toggle to the record form and display "Auto" in the TTL column.

**Files:**
- Modify: `ui/src/views/ZoneDetailView.vue`

### Step 1: Add autoTtl ref and computed

In the `<script setup>` section, after the `proxied` ref (line 40):

```typescript
const autoTtl = ref(true)
```

Add a computed for showing the toggle (after `showProxyToggle` around line 74):

```typescript
const showAutoTtlToggle = computed(() => hasCloudflareProvider.value)
```

### Step 2: Wire autoTtl into form lifecycle

**`openCreateRecord()`** — add after `proxied.value = false`:
```typescript
autoTtl.value = hasCloudflareProvider.value  // default ON for Cloudflare zones
```

**`openEditRecord()`** — add after the `proxied.value = ...` line:
```typescript
autoTtl.value = (rec.provider_meta as Record<string, unknown>)?.auto_ttl === true
```

**`handleSubmitRecord()`** — update the provider_meta assembly block. Replace:
```typescript
    if (hasCloudflareProvider.value && ['A', 'AAAA', 'CNAME'].includes(payload.type)) {
      payload.provider_meta = { proxied: proxied.value }
    }
```
With:
```typescript
    if (hasCloudflareProvider.value) {
      const meta: Record<string, unknown> = { auto_ttl: autoTtl.value }
      if (['A', 'AAAA', 'CNAME'].includes(payload.type)) {
        meta.proxied = proxied.value
      }
      payload.provider_meta = meta
    }
```

### Step 3: Auto-lock autoTtl when proxied

In the `watch` on `form.value.type` (around line 166), add after the proxied reset:

```typescript
// Also in the existing watch block, add a new watcher for proxied → autoTtl
```

Actually, add a separate watcher after the existing one:

```typescript
watch(proxied, (bProxied) => {
  if (bProxied) {
    autoTtl.value = true  // Cloudflare enforces TTL=1 on proxied records
  }
})
```

### Step 4: Add Auto-TTL toggle UI

In the template, after the proxy toggle `<div>` (after line 380), add:

```vue
<div v-if="showAutoTtlToggle" class="field">
  <div class="proxy-row">
    <label for="rec-auto-ttl">Cloudflare Auto TTL</label>
    <ToggleSwitch id="rec-auto-ttl" v-model="autoTtl" :disabled="proxied" />
  </div>
  <small class="text-muted">
    {{ proxied ? 'Auto TTL is required for proxied records' : 'Cloudflare will use Auto TTL (other providers use the TTL value above)' }}
  </small>
</div>
```

### Step 5: Add "Auto" indicator in TTL column

Update the TTL column in the DataTable (around line 253-256). Replace:

```vue
<Column field="ttl" header="TTL" sortable style="width: 5rem">
  <template #body="{ data }">
    <span class="font-mono">{{ data.ttl }}</span>
  </template>
</Column>
```

With:

```vue
<Column field="ttl" header="TTL" sortable style="width: 7rem">
  <template #body="{ data }">
    <span class="font-mono">{{ data.ttl }}</span>
    <Tag
      v-if="hasCloudflareProvider && data.provider_meta?.auto_ttl"
      value="Auto"
      severity="secondary"
      class="ml-auto-ttl"
    />
  </template>
</Column>
```

Add the styling in the `<style scoped>` section:

```css
.ml-auto-ttl {
  margin-left: 0.375rem;
  font-size: 0.7rem;
}
```

### Step 6: Verify frontend builds

```bash
cd ui && npm run build
```
Expected: Clean build, no TypeScript errors.

### Step 7: Commit

```bash
git add ui/src/views/ZoneDetailView.vue
git commit -m "feat: Cloudflare Auto TTL toggle in record form with Auto indicator in TTL column"
```

---

## Task 6: Variable Values Display — Frontend

Show expanded variable values in the zone records DataTable. Uses the variables already
fetched by `useVariableAutocomplete`.

**Files:**
- Modify: `ui/src/views/ZoneDetailView.vue`

### Step 1: Load variables on mount and create expand helper

The `useVariableAutocomplete` composable already exposes `variables` and `loadVariables()`.
Currently `loadVariables()` is only called when the variable panel is opened. We need to call
it on mount so variables are available for the table display.

In `ZoneDetailView.vue`, the composable is already destructured at line 42. Add `loadVariables`
and `variables` to the destructured return if not already there (check the composable — both
are already exported).

Update the destructuring (line 42) to include `variables` and `loadVariables`:

```typescript
const { variables, varFilter, varPanelRef, filteredVars, loadVariables, togglePanel, hidePanel, onValueInput } =
  useVariableAutocomplete(zoneId)
```

Add `loadVariables()` call in the `fetchData()` function, after the main data fetch (e.g. after
line 99, inside the `try` block):

```typescript
loadVariables()
```

Add the expand helper function after `fetchData()`:

```typescript
function expandTemplate(sTemplate: string): string | null {
  if (!sTemplate.includes('{{')) return null
  return sTemplate.replace(/\{\{([A-Za-z0-9_]+)\}\}/g, (_match, varName) => {
    // Zone-scoped vars shadow global vars (matching backend VariableEngine behavior)
    const zoneVar = variables.value.find(v => v.name === varName && v.scope === 'zone')
    const globalVar = variables.value.find(v => v.name === varName && v.scope === 'global')
    const resolved = zoneVar || globalVar
    return resolved ? resolved.value : `{{${varName}}}`
  })
}

function hasUnresolvedVars(sTemplate: string): boolean {
  const expanded = expandTemplate(sTemplate)
  return expanded !== null && /\{\{[A-Za-z0-9_]+\}\}/.test(expanded)
}
```

### Step 2: Update the Value column template

Replace the existing Value column (around line 248-252):

```vue
<Column field="value_template" header="Value">
  <template #body="{ data }">
    <span class="font-mono">{{ data.value_template }}</span>
  </template>
</Column>
```

With:

```vue
<Column field="value_template" header="Value">
  <template #body="{ data }">
    <div v-if="expandTemplate(data.value_template)" class="value-expanded">
      <span class="font-mono" :class="{ 'text-warn': hasUnresolvedVars(data.value_template) }">
        {{ expandTemplate(data.value_template) }}
      </span>
      <span class="font-mono value-template-raw">{{ data.value_template }}</span>
    </div>
    <span v-else class="font-mono">{{ data.value_template }}</span>
  </template>
</Column>
```

### Step 3: Add styles

Add to the `<style scoped>` section:

```css
.value-expanded {
  display: flex;
  flex-direction: column;
  gap: 0.125rem;
}

.value-template-raw {
  font-size: 0.75rem;
  color: var(--p-text-muted-color);
}

.text-warn {
  color: var(--p-orange-400);
}
```

### Step 4: Verify frontend builds

```bash
cd ui && npm run build
```
Expected: Clean build, no TypeScript errors.

### Step 5: Commit

```bash
git add ui/src/views/ZoneDetailView.vue
git commit -m "feat: show expanded variable values in zone records table"
```

---

## Task 7: Build and Verify All

Full build and test run to confirm everything works together.

### Step 1: Build backend

```bash
cmake --build build --parallel
```
Expected: Clean compile.

### Step 2: Run all tests

```bash
build/tests/dns-tests
```
Expected: All tests pass (162 pass, 123 skip — same as baseline plus the 2 new DiffEngine tests).

### Step 3: Build frontend

```bash
cd ui && npm run build
```
Expected: Clean build.

### Step 4: Commit (if any adjustments were needed)

If adjustments were made, commit them with an appropriate message.

---

## Summary of Changes

| File | Change |
|------|--------|
| `include/dal/RecordRepository.hpp` | Add `jProviderMeta` param to `create()` and `update()` |
| `src/dal/RecordRepository.cpp` | Pass `jProviderMeta` through to SQL in `create()` and `update()` |
| `src/api/routes/RecordRoutes.cpp` | Extract/emit `provider_meta` in POST/PUT/GET handlers |
| `src/providers/CloudflareProvider.cpp` | Tag `auto_ttl` in `parseRecordsResponse()`, emit TTL=1 in `buildRecordBody()` |
| `src/core/DiffEngine.cpp` | Normalize TTL for Cloudflare auto_ttl records in `preview()` |
| `tests/unit/test_diff_engine.cpp` | 2 new tests for auto_ttl normalization behavior |
| `ui/src/views/ZoneDetailView.vue` | Auto-TTL toggle, "Auto" tag in TTL column, expanded variable values |
