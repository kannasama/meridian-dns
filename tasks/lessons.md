# Lessons Learned

## 2026-02-26 — Indentation changes must include code blocks

**Pattern:** When asked to update indentation standards in documentation, the scope includes
indentation *inside fenced code blocks* (` ``` `), not just prose bullet lists and inline references.

**Rule:** Before marking an indentation task complete, always scan for 4-space-indented lines
inside code fences (` ```cpp `, ` ```sql `, ` ```bash `, ` ```json `, ` ``` `, etc.) across
all affected files.

**Verification step to add:** After prose changes, run a regex search for lines matching
`^    ` (4 leading spaces) inside code blocks to catch missed indentation.

## 2026-03-04 — Follow existing file naming conventions for plans

**Pattern:** When creating new plan documents, check the existing `docs/plans/` directory for
the naming convention before choosing a filename. The project uses date-prefixed names like
`2026-02-28-phase-4-authentication.md`, not flat names like `PHASE5_PLAN.md`.

**Rule:** Before creating any new document in an existing directory, run `ls` on that directory
first and match the established naming pattern (date prefix, kebab-case, descriptive slug).

**Verification step to add:** After creating a plan file, confirm its name matches the pattern
of sibling files in the same directory.

## 2026-03-04 — Communication

**Mistake**: After writing a plan the user explicitly said they would not execute, presented
execution options ("Subagent-Driven or Parallel Session?") anyway — copying the writing-plans
skill's "Execution Handoff" section verbatim.

**Pattern**: Blindly following a skill template without adapting to user-stated intent.

**Rule**: When the user says they won't execute now or plan to execute with another tool, skip
the execution handoff entirely. Just confirm the plan is saved and committed. The skill template
is a default, not a mandate — always defer to the user's explicit instructions.

**Applied**: Any skill with a handoff/next-steps section (writing-plans, brainstorming, etc.).
Read the user's request fully before appending boilerplate.
