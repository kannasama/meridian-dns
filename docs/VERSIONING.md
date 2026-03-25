# Versioning Policy

Meridian DNS uses a `MAJOR.MINOR.PATCH[-N]` versioning scheme with project-specific semantics
for each segment.

---

## Segments

| Segment | Meaning | Trigger |
|---------|---------|---------|
| **MAJOR** | Breaking or paradigm-shifting release | Incompatible API/schema changes requiring manual intervention, or a fundamental architectural rewrite |
| **MINOR** | New conceptual capability | Changes how a user thinks about or interacts with the system — new workflows, new paradigms, new integrations that stand on their own |
| **PATCH** | Fix or incremental extension | Bug fixes, corrections, and extensions of existing capability that don't change the mental model (new config option, new filter, new export format, small self-contained feature) |
| **-N** | Release revision (errata) | Corrections to the release artifact itself; starts at `-1` for each new base release |

---

## The MINOR/PATCH Decision Test

> "Does this change how a user reasons about the system, or does it extend something that already exists?"
> - New paradigm, new workflow, new conceptual layer → **MINOR**
> - Extends existing capability, fixes something, adds a setting → **PATCH**

---

## Release Revisions (-N)

The `-N` suffix identifies a **release revision** (errata) — a correction to the release itself
rather than new functionality. It is distinct from a new PATCH release.

### Qualifies as -N (errata)

- A migration file was wrong or missing from the release
- A permission seed that was part of the release scope was incorrect or absent
- A UI setting or feature was explicitly committed to for the release but shipped incomplete or broken

### Does NOT qualify as -N

- New functionality not originally scoped to the release → new PATCH release
- A bug discovered after release that wasn't part of the release scope → new PATCH release

### Behavior

- `-N` starts at `-1` for each new base release (e.g., `v1.1.3-1`, not `v1.1.2-3`)
- Multiple errata on the same base are valid (`v1.1.2-1`, `v1.1.2-2`, …); if errata accumulate
  beyond two or three it is a signal to cut a new PATCH release instead
- Base release tags (`v1.1.2`) remain on the original commit and are never overwritten; `-N`
  tags point to the corrected commits that follow
- Docker image tags follow the same convention — `v1.1.2` is never re-pushed; only
  `v1.1.2-1`, `v1.1.2-2`, etc. receive new image pushes

---

## Examples from Release History

| Release | Rationale |
|---------|-----------|
| `v1.0.0` | First public release |
| `v1.1.0` | MINOR — Templates & Snippets, Dynamic Variables, OIDC/SAML auth, multi-repo GitOps: all new conceptual capabilities |
| `v1.1.1` | PATCH — User preferences, zone categorization filter, drawer→dialog refactor: extensions of existing workflows |
| `v1.1.2` | PATCH — Deployment bug fixes, system logging: fixes and an extension of existing observability |
| `v1.1.2-1` | Errata — Permission seed fix: correction to the release artifact |
| `v1.1.2-2` | Errata — System log retention UI missing from release: feature committed to for the release but shipped incomplete |
