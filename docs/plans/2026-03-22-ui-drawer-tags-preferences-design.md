# UI Improvements: Drawer→Dialog, Tags Fix, User Preferences

**Date:** 2026-03-22  
**Status:** Approved

---

## Summary

Three related UI/UX improvements:

1. **Replace all Drawer (sidebar) components with Dialog (modal) components** across all 12 views
2. **Fix tags feature** — fix AutoComplete free-form entry, move tag editing to zone edit Dialog, add tag creation in admin
3. **Zone categorization & user preferences** — auto-detect reverse/forward zones, DB-backed user preferences for persistent tag filters and theme

---

## 1. Drawer → Dialog Migration

### Problem

Sidebar drawers are used for create/edit forms across 12 views. They don't provide meaningful space benefits given their constrained width, and fields often clip.

### Solution

Replace every `Drawer` component with PrimeVue `Dialog` (modal). Changes per view:

- Replace `import Drawer` → `import Dialog`
- Replace `<Drawer v-model:visible position="right">` → `<Dialog v-model:visible modal>`
- Remove Drawer-specific CSS, add Dialog width classes
- Rename `drawerVisible` → `dialogVisible` for consistency

### Affected Views

| View | Dialog Width | Notes |
|------|-------------|-------|
| `SoaPresetsView.vue` | `w-30rem` | Numeric fields, straightforward |
| `SnippetsView.vue` | `w-40rem` | Nested record forms |
| `TemplatesView.vue` | `w-40rem` | Snippet picker (dual-panel) |
| `ZonesView.vue` | `w-30rem` | Zone settings + tags (new) |
| `VariablesView.vue` | `w-30rem` | Simple key-value |
| `ProvidersView.vue` | `w-30rem` | Provider config |
| `ViewsView.vue` | `w-30rem` | View + provider attach |
| `GroupsView.vue` | `w-30rem` | Group CRUD |
| `GitReposView.vue` | `w-30rem` | Git repo config |
| `ProviderDefinitionsView.vue` | `w-30rem` | Provider definitions |
| `AdminAuthView.vue` | `w-30rem` | User/group/role management |
| `UsersView.vue` | `w-30rem` | User CRUD |

---

## 2. Tags Feature Fix & Enhancement

### 2a. Fix AutoComplete Free-Form Entry

**Problem:** PrimeVue `AutoComplete` with `multiple` mode doesn't commit free-text entries when there are no matching suggestions. Typing a new tag and pressing Enter does nothing.

**Solution:** Add a `@keydown.enter` handler on the AutoComplete that:
1. Reads the current input text
2. If non-empty and not already in the tags array, adds it
3. Clears the input

### 2b. Move Tag Editing to Zone Edit Dialog

**Problem:** Tag editing is in `ZoneDetailView.vue` (zone records page), which is the wrong context for metadata editing.

**Solution:**
- Remove tags section (AutoComplete + Save Tags button) from `ZoneDetailView.vue`
- Add tags AutoComplete field to the zone edit Dialog in `ZonesView.vue`
- On form submit, call both `updateZone()` and `updateZoneTags()` sequentially
- Load existing tags into form when opening for edit
- Load all existing tag names for autocomplete suggestions on mount

### 2c. Add Tag Creation in Admin

**Problem:** `TagsView.vue` only supports rename and delete — no way to create tags.

**Solution:**
- Add "New Tag" button + Dialog to `TagsView.vue`
- New backend endpoint: `POST /api/v1/tags` — creates a tag in vocabulary
- New API client function: `createTag(name: string)` in `ui/src/api/tags.ts`
- Missing imports in `TagsView.vue`: add `DataTable`, `Column`, `Button`, `Dialog`, `InputText`

---

## 3. Zone Categorization — Reverse vs Forward

### Problem

Reverse lookup zones (`.in-addr.arpa`, `.ip6.arpa`) clutter the zone list alongside forward zones. Users want to hide them by default.

### Solution

**UI-only categorization** (no backend changes):
- Detect reverse zones by name suffix: `.in-addr.arpa` or `.ip6.arpa`
- Add a segmented control to the zones page: **Forward** | **Reverse** | **All**
- Default view is determined by user preference (`zone_default_view`), falling back to "All"
- The existing tag-based MultiSelect filter continues to work within the selected category

### Implementation

```typescript
const isReverseZone = (name: string) =>
  name.endsWith('.in-addr.arpa') || name.endsWith('.ip6.arpa')

const categoryFilteredZones = computed(() => {
  if (zoneCategory.value === 'forward')
    return zones.value.filter(z => !isReverseZone(z.name))
  if (zoneCategory.value === 'reverse')
    return zones.value.filter(z => isReverseZone(z.name))
  return zones.value
})
```

---

## 4. User Preferences (DB-Backed)

### Database

New `user_preferences` table:

```sql
CREATE TABLE user_preferences (
  user_id   BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  key       TEXT NOT NULL,
  value     JSONB NOT NULL DEFAULT '""',
  PRIMARY KEY (user_id, key)
);
```

### Backend

**Repository:** `UserPreferenceRepository` with:
- `getAll(userId)` → `map<string, json>`
- `get(userId, key)` → `optional<json>`
- `set(userId, key, value)` → upsert
- `setAll(userId, map<string, json>)` → batch upsert
- `deleteKey(userId, key)`

**API Endpoints:**
- `GET /api/v1/users/me/preferences` — returns all preferences as flat JSON object
- `PUT /api/v1/users/me/preferences` — upsert key-value pairs from request body

**Migration:** Add table in `MigrationRunner.cpp`.

### Initial Preference Keys

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `zone_hidden_tags` | `string[]` | `[]` | Tags to exclude from zone list by default |
| `zone_default_view` | `string` | `"all"` | Default zone category: `"forward"`, `"reverse"`, `"all"` |
| `theme` | `string` | `"system"` | User's theme preference (migrates from localStorage) |

### Frontend Integration

**Pinia store:** New `usePreferencesStore()`:
- Fetched once on app load (after auth)
- Provides reactive getters for each preference key
- `save(key, value)` action calls the PUT endpoint

**Zone list integration:**
- On mount, read `zone_hidden_tags` to pre-populate tag exclusion filter
- Read `zone_default_view` to set the initial Forward/Reverse/All tab
- Changes to these filters offer a "Save as default" option

**Theme migration:**
- On first load, if DB has no `theme` preference but localStorage does, migrate it
- Remove localStorage theme storage, use only the preferences store

---

## Implementation Order

1. **Drawer → Dialog migration** (12 views, mechanical, no backend)
2. **Tags fix** (AutoComplete free-form entry)
3. **Tags relocation** (move to zone edit Dialog)
4. **Tag creation** (admin view + backend POST endpoint)
5. **DB migration** (user_preferences table)
6. **User preferences backend** (repository + API endpoints)
7. **User preferences frontend** (Pinia store + fetch on load)
8. **Zone categorization UI** (Forward/Reverse/All segmented control)
9. **Persistent tag filters** (wire preferences to zone list filters)
10. **Theme migration** (move from localStorage to preferences)

---

## Out of Scope

- Zone-type-specific constraints (e.g., different record validation for reverse zones)
- Sharing preference presets between users
- Role-based default preferences
