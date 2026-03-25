# Meridian DNS — Versioning Scheme & Release Roadmap

**Date:** 2026-03-24
**Status:** Approved

---

## Table of Contents

1. [Versioning Scheme](#versioning-scheme)
2. [Release Revisions (-N)](#release-revisions--n)
3. [Release Roadmap](#release-roadmap)
4. [Long-Term Deferred](#long-term-deferred)

---

## Versioning Scheme

**Format:** `MAJOR.MINOR.PATCH[-N]`

| Segment | Meaning | Trigger |
|---------|---------|---------|
| **MAJOR** | Breaking or paradigm-shifting release | Incompatible API/schema changes requiring manual intervention, or a fundamental architectural rewrite |
| **MINOR** | New conceptual capability | Changes how a user thinks about or interacts with the system — new workflows, new paradigms, new integrations that stand on their own |
| **PATCH** | Fix or incremental extension | Bug fixes, corrections, and extensions of existing capability that don't change the mental model (new config option, new filter, new export format, small self-contained feature) |
| **-N** | Release revision (errata) | Corrections to the release artifact itself; starts at `-1` for each new base release |

### The MINOR/PATCH Decision Test

> "Does this change how a user reasons about the system, or does it extend something that already exists?"
> - New paradigm, new workflow, new conceptual layer → **MINOR**
> - Extends existing capability, fixes something, adds a setting → **PATCH**

### Examples from Release History

| Release | Rationale |
|---------|-----------|
| `v1.0.0` | First public release |
| `v1.1.0` | MINOR — Templates & Snippets, Dynamic Variables, OIDC/SAML auth, multi-repo GitOps: all new conceptual capabilities |
| `v1.1.1` | PATCH — User preferences, zone categorization filter, drawer→dialog refactor: extensions of existing workflows |
| `v1.1.2` | PATCH — Deployment bug fixes, system logging: fixes and an extension of existing observability |
| `v1.1.2-1` | Errata — Permission seed fix: correction to the release artifact |
| `v1.1.2-2` | Errata — System log retention UI missing from release: feature committed to for the release but shipped incomplete |

---

## Release Revisions (-N)

The `-N` suffix identifies a **release revision** (errata) — a correction to the release itself
rather than new functionality. It is distinct from a PATCH release.

### Qualifies as -N (errata)

- A migration file was wrong or missing from the release
- A permission seed that was part of the release scope was incorrect or absent
- A UI setting or feature was explicitly committed to for the release but shipped incomplete or broken

### Does NOT qualify as -N

- New functionality not originally scoped to the release → new PATCH
- A bug discovered after release that wasn't part of the release scope → new PATCH

### Behavior

- `-N` starts at `-1` for each new base release (`v1.1.3-1`, not `v1.1.2-3`)
- Multiple errata on the same base are valid (`v1.1.2-1`, `v1.1.2-2`, …); if errata accumulate
  beyond two or three it is a signal to cut a new PATCH release instead
- Base release tags (`v1.1.2`) remain on the original merge commit and are never overwritten;
  `-N` tags point to the corrected commits that follow
- Docker image tags follow the same convention — `v1.1.2` is never re-pushed to the registry;
  only `v1.1.2-1`, `v1.1.2-2`, etc. receive new image pushes

---

## Release Roadmap

Specific version numbers are not guaranteed — these are logical release bands. A patch band
may ship as one or several releases; a minor band may shift position as priorities evolve.

---

### `v1.1.3` — UI Quality of Life

**Theme:** Discoverability, polish, and operational visibility

| Item | Type |
|------|------|
| Sidebar: logical navigation grouping (DNS Management / Operations / Configuration / Admin) | UI |
| Settings page: friendly display names, DB config key shown as secondary/monospace | UI |
| Role management: all permission keys visible when creating/editing roles | Fix |
| Selection dropdowns: sorting and filtering support | UI |
| Dashboard: configurable sorting and filtering | UI |
| Dashboard: System Health expansion — surface what each check is actually testing (provider connectivity, DB reachability, GitOps status, sync state) | Full-stack |
| Record notes/comments field (freeform annotation, visible in audit; extends existing record form and audit trail — no new workflow introduced) | Full-stack |
| Record count badge on zones in the zone list | UI |
| Deployment history: link from deployment record to related audit entries | UI |
| Zone list: last-deployed-at column | Full-stack |
| Audit page: direct link to zone from an audit entry | UI |

---

### `v1.1.4` — Operational Integration

**Theme:** Infrastructure hooks, connectivity, and hardening

| Item | Type |
|------|------|
| BIND export webhook/script trigger (post-push/export trigger for `rndc reload` etc.) | Full-stack |
| Deployment webhook notifications (general-purpose push success/failure signal) | Full-stack |
| Git repo configuration: auto-import known-hosts | Full-stack |
| Native TLS support (SEC-08) — `DNS_TLS_CERT_FILE` / `DNS_TLS_KEY_FILE` via Crow SSL | Backend |
| Document CLI password reset procedure — emergency recovery tool for loss of admin GUI access | Docs |
| Health endpoint enhancements: per-component status, latency, version, uptime in JSON response | Full-stack |
| `docker-compose.yml` HEALTHCHECK directive wired to `/api/v1/health` | Infra |
| `DNS_TRUSTED_PROXIES` — investigate Traefik/containerized proxy IP discoverability; implement if reliable approach exists, otherwise document constraint and recommended workaround | Investigate |
| BIND zone file pre-validation before export | Backend |
| **ACME support (investigate)** — feasibility of automated TLS certificate provisioning and renewal; DNS-01 challenge support is a compelling self-contained option given Meridian's direct DNS management capability | Investigate |

---

### `v1.2.0` — Access Control & User Governance

**Theme:** Delegated access, identity-aware security, and user lifecycle management

This release introduces a new conceptual layer over the existing global RBAC model: per-view
and per-zone permission scoping, user lifecycle controls, and auth hardening.

| Item | Type |
|------|------|
| Scoped permissions: per-view and per-zone permission overrides | Full-stack |
| Zone ownership: responsible operator field on zones, surfaces in list and audit | Full-stack |
| Delegation tokens: scoped API keys restricted to a specific view or zone | Full-stack |
| Disable local login when SSO is configured (org-level enforcement option) | Full-stack |
| Break-glass mechanism: env-var-gated emergency credential or CLI override for SSO recovery (env var by design — must be operable when DB is unreachable or SSO is misconfigured, making it a deploy-time infrastructure concern exempt from the UI-managed config policy) | Backend |
| Disable users: admin ability to disable any user account regardless of authentication source; disabling immediately invalidates all active sessions | Full-stack |
| Admin session management: admin-level view of all active sessions with revoke capability | Full-stack |
| TOTP/MFA support for local accounts | Full-stack |
| Audit enhancements: auth events (login, logout, MFA failure, break-glass activation, session revocation, user disabled) as first-class audit entries | Full-stack |
| Permission inheritance visualization: read-only UI showing effective permission set for a user across group memberships and scoped overrides | UI |

---

### `v1.3.0` — DNSSEC Management

**Theme:** DNS security extensions

This release introduces DNSSEC as a first-class capability. All three current providers
(PowerDNS, Cloudflare, DigitalOcean) expose DNSSEC management via their APIs.

| Item | Type |
|------|------|
| Enable/disable DNSSEC per zone | Full-stack |
| DS record display for registrar submission | Full-stack |
| Key status and rotation visibility | Full-stack |
| DNSSEC validation status indicator per zone (signatures valid and not expired) | Full-stack |
| Automated key rotation scheduling (configurable policy, via maintenance scheduler) | Backend |
| NSEC3 opt-in configuration per zone where supported | Full-stack |
| Provider conformance test extensions for DNSSEC operations | Backend |

---

### `v1.4.0` — Identity & Directory Integration

**Theme:** Enterprise identity backends

This release introduces LDAP/AD as an additional authentication backend and expands the
user identity model to support directory-sourced attributes with controlled local overrides.

Note: JIT (just-in-time) user provisioning from SSO is already implemented. Minimal SSO
claim mapping to profile fields is also already in place. This release extends that to a full
attribute model with additional fields, controlled local overrides, and LDAP/AD-sourced
attributes.

| Item | Type |
|------|------|
| Extended user fields: display name, email, department, title (AD/LDAP-style attributes) | Full-stack |
| Extend SSO claim mapping: expand beyond current minimal mapping to cover the full attribute set above | Backend |
| Local overrides of non-critical SSO-sourced fields — users may override preference-level fields (e.g., display name) while authoritative fields (e.g., email used for SSO matching) remain SSO-managed | Full-stack |
| LDAP/AD authentication as an additional auth backend | Backend |
| Group sync from LDAP/AD: auto-assign Meridian groups/roles based on directory group membership | Backend |
| Profile page: read-only view of SSO-sourced fields with "managed by SSO" labeling; editable fields clearly distinguished | UI |
| Admin UI: LDAP/AD connection testing and attribute mapping configuration | Full-stack |

---

## Long-Term Deferred

Items with no current release assignment. Revisit when use cases emerge or infrastructure
decisions are made.

| Item | Reason |
|------|--------|
| Approval workflow | No concrete use case yet; implement when actively requested |
| Community provider registry | Requires maintaining public infrastructure |
| Scheduled deployments | No demand identified |
| Multi-instance OIDC state (Redis) | Requires Redis infrastructure decision |
| RS256/ES256 JWT signing | Low urgency for single-instance deployments |
| TUI client (Phase 11) | Separate repository with independent versioning |

