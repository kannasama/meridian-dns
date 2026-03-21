# v1.2 Internal Backlog

Items deferred from the v1.1 code review for the next minor release.

---

## CR2-QUALITY-02 -- Extract shared diff-to-JSON serialization

**Source:** `docs/internal/CODE_REVIEW_2_1.1.md` Section 4

Diff-to-JSON conversion is duplicated in three locations:

1. `src/api/routes/RecordRoutes.cpp:337-347` (preview endpoint -- inline ternary)
2. `src/api/routes/RecordRoutes.cpp:354-374` (per-provider breakdown -- inline ternary)
3. `src/api/routes/ZoneTemplateRoutes.cpp:66-83` (`diffEntryToJson()` helper)

Location 3 already has a clean helper function. Refactor locations 1 and 2 to call
`diffEntryToJson()` (move it to `RouteHelpers.hpp` or a shared serialization header).

**Priority:** Low -- cosmetic duplication, no correctness risk.
