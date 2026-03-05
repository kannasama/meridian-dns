# Project Rename Plan: DNS Orchestrator → Meridian DNS

**Date:** 2026-03-05
**Scope:** In-codebase rename only. GitLab/GitHub repo rename is a separate manual step.
**Exclusion:** `dns-tests` binary name remains unchanged per user request.

---

## Naming Mappings

| Old | New | Context |
|-----|-----|---------|
| `DNS Orchestrator` | `Meridian DNS` | Human-readable project name |
| `dns-orchestrator` | `meridian-dns` | Binary, CMake project, Docker, paths |
| `dns_orchestrator` | `meridian_dns` | PostgreSQL database name |
| `dns-orchestrator-ui` | `meridian-dns-ui` | Future Web UI repo reference |
| `dns-orchestrator-tui` | `meridian-dns-tui` | Future TUI repo reference |
| `dns-core` | `meridian-core` | CMake static library target |
| `dns@orchestrator.local` | `meridian@dns.local` | Git commit author in GitOpsMirror |
| `/var/dns-orchestrator/repo` | `/var/meridian-dns/repo` | Default GitOps repo path |
| `~/.config/dns-orchestrator/` | `~/.config/meridian-dns/` | TUI credential path |

---

## File-by-File Change List

### 1. Build System

#### `CMakeLists.txt` (root)
- Line 2: `project(dns-orchestrator` → `project(meridian-dns`
- Line 5: `DESCRIPTION "Multi-Provider DNS Orchestrator"` → `DESCRIPTION "Meridian DNS — Multi-Provider DNS Management"`

#### `src/CMakeLists.txt`
- Line 1: comment `dns-core` → `meridian-core`
- Line 13: `add_library(dns-core STATIC` → `add_library(meridian-core STATIC`
- Line 15: `target_include_directories(dns-core` → `target_include_directories(meridian-core`
- Line 19: `target_link_libraries(dns-core` → `target_link_libraries(meridian-core`
- Line 30: comment `dns-orchestrator` → `meridian-dns`
- Line 31: `add_executable(dns-orchestrator` → `add_executable(meridian-dns`
- Line 34: `dns-core` → `meridian-core`

#### `tests/CMakeLists.txt`
- Line 26: `dns-core` → `meridian-core` (link target for dns-tests)

---

### 2. Source Code

#### `src/main.cpp`
- Line 237: `"dns-orchestrator ready"` → `"meridian-dns ready"`
- Line 245: `"dns-orchestrator shutdown complete"` → `"meridian-dns shutdown complete"`

#### `src/gitops/GitOpsMirror.cpp`
- Line 194: `"dns-orchestrator", "dns@orchestrator.local"` → `"meridian-dns", "meridian@dns.local"`

#### `include/common/Config.hpp`
- Line 35: `sGitLocalPath = "/var/dns-orchestrator/repo"` → `sGitLocalPath = "/var/meridian-dns/repo"`

#### `tests/unit/test_crypto_service.cpp`
- Line 16: `"Hello, DNS Orchestrator!"` → `"Hello, Meridian DNS!"`

---

### 3. Docker / Deployment

#### `Dockerfile`
- Line 24: `useradd ... dns-orchestrator` → `useradd ... meridian-dns`
- Line 26: paths `dns-orchestrator` → `meridian-dns` (binary COPY)
- Line 27: `/opt/dns-orchestrator/db/` → `/opt/meridian-dns/db/`
- Line 31: `USER dns-orchestrator` → `USER meridian-dns`
- Line 35: `CMD ["dns-orchestrator"]` → `CMD ["meridian-dns"]`

#### `docker-compose.yml`
- Line 5: `POSTGRES_DB: dns_orchestrator` → `POSTGRES_DB: meridian_dns`
- Line 14: `pg_isready ... dns_orchestrator` → `pg_isready ... meridian_dns`
- Line 37: DB URL `dns_orchestrator` → `meridian_dns`
- Line 46: volume path `/var/dns-orchestrator/repo` → `/var/meridian-dns/repo`

#### `.env.example`
- Line 1: `# DNS Orchestrator` → `# Meridian DNS`

#### `scripts/docker/entrypoint.sh`
- Line 5: `dns-orchestrator --migrate` → `meridian-dns --migrate`

---

### 4. SQL Scripts

#### `scripts/db/001_initial_schema.sql`
- Line 2: comment `dns-orchestrator` → `meridian-dns`

#### `scripts/db/002_add_indexes.sql`
- Line 2: comment `dns-orchestrator` → `meridian-dns`

---

### 5. Documentation (active docs)

#### `CLAUDE.md`
- All references to "DNS Orchestrator" → "Meridian DNS"
- All references to `dns-orchestrator` → `meridian-dns`
- `dns-core` → `meridian-core`
- `dns-orchestrator-ui` → `meridian-dns-ui`
- `dns-orchestrator-tui` → `meridian-dns-tui`

#### `README.md`
- Line 1: `# DNS Orchestrator` → `# Meridian DNS`
- Line 71: directory tree header `dns-orchestrator/` → `meridian-dns/`

#### `docs/openapi.yaml`
- Line 3: `title: DNS Orchestrator API` → `title: Meridian DNS API`

#### `docs/ARCHITECTURE.md`
- Line 1 and all ~30 references to `dns-orchestrator` / `DNS Orchestrator` / `dns_orchestrator`
- Config paths, Docker snippets, Nginx/Caddy examples, env vars, directory trees
- Database names in SQL snippets (`dns_orchestrator` → `meridian_dns`)
- Git author (`dns-orchestrator` → `meridian-dns`)

#### `docs/DESIGN.md`
- Line 1: title reference

#### `docs/CODE_STANDARDS.md`
- Line 1: title reference

#### `docs/BUILD_ENVIRONMENT.md`
- All ~25 references: database name, binary paths, clone commands, env vars, quick-start

#### `docs/TUI_DESIGN.md`
- Line 1: title
- Line 11: server name reference
- Line 23: `~/.config/dns-orchestrator/` → `~/.config/meridian-dns/`

#### `docs/plans/SECURITY_PLAN.md`
- Title, container user, proxy references, database grants (~8 references)

---

### 6. Documentation (historical plans — bulk update)

These are completed phase plans. Update for consistency but treat as low priority:

- `docs/plans/2026-02-28-project-framing-design.md` (~15 references)
- `docs/plans/2026-02-28-phase-4-authentication.md` (~20 references)
- `docs/plans/2026-02-28-phase-3-5-crow-migration.md` (~3 references)
- `docs/plans/2026-03-04-phase-6-powerdns-provider-core-engines.md` (~10 references)
- `docs/plans/2026-03-05-phase-7-deployment-pipeline-gitops.md` (~10 references)
- `docs/plans/2026-03-05-phase-8-api-hardening-docker.md` (~20 references)
- `docs/plans/2026-02-28-documentation-restructure-design.md` (~5 references)

---

### 7. Other

#### `.roo/settings.local.json`
- Line 4: path reference (if still relevant)

#### Memory file (auto-memory)
- `/home/mjhill/.claude/projects/-home-mjhill-Projects-Git-dns-orchestrator/memory/MEMORY.md`
- Update project name references

---

## Execution Order

1. **Build system** — CMakeLists.txt files (3 files) — ensures `cmake --build` still works
2. **Source code** — main.cpp, GitOpsMirror.cpp, Config.hpp, test file (4 files)
3. **Docker/deployment** — Dockerfile, docker-compose.yml, .env.example, entrypoint.sh (4 files)
4. **SQL scripts** — comments only (2 files)
5. **Active docs** — CLAUDE.md, README.md, openapi.yaml, ARCHITECTURE.md, DESIGN.md, CODE_STANDARDS.md, BUILD_ENVIRONMENT.md, TUI_DESIGN.md, SECURITY_PLAN.md (9 files)
6. **Historical plans** — bulk sed-style replacement (7 files)
7. **Verify** — clean build + all tests pass
8. **Commit** — single commit: `refactor: rename project to Meridian DNS`

---

## Environment Variables

All existing `DNS_*` environment variable names are **unchanged** — they describe the domain
(DNS management), not the project name. No env var rename needed.

---

## NOT Changed

- `dns-tests` binary name (per user request)
- `DNS_*` environment variable prefixes (domain-specific, not project-specific)
- PostgreSQL role names (`dns`, `dns_app`, `dns_audit_admin` — these are DB users, not project names)
- Namespace `dns::` in C++ code (domain namespace, not project name)
