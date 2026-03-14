# Build Environment Setup — EndeavourOS / Arch Linux

> **Target Platform:** Linux (EndeavourOS) using `paru` as the AUR/Pacman helper.
> This guide sets up a complete native build environment for the C++ Multi-Provider Meridian DNS.

---

## Table of Contents

1. [Prerequisites](#1-prerequisites)
2. [Install Build Tools](#2-install-build-tools)
3. [Install Library Dependencies](#3-install-library-dependencies)
4. [Install AUR Dependencies](#4-install-aur-dependencies)
5. [PostgreSQL Setup](#5-postgresql-setup)
6. [Clone the Repository](#6-clone-the-repository)
7. [Configure and Build](#7-configure-and-build)
8. [Verify the Build](#8-verify-the-build)
9. [Development Environment Variables](#9-development-environment-variables)
10. [Package Reference Table](#10-package-reference-table)

---

## 1. Prerequisites

Ensure `paru` is installed and your system is fully up to date before proceeding.

```bash
# Full system upgrade first — required to avoid partial-upgrade issues on Arch
paru -Syu
```

You will also need `base-devel` (provides `gcc`, `make`, `pkg-config`, etc.) and `git`:

```bash
paru -S --needed base-devel git
```

---

## 2. Install Build Tools

The project requires **CMake 3.20+**, **Ninja**, and **GCC 12+** (for C++20 support including
`std::jthread`, `std::format`, and `std::filesystem`).

```bash
paru -S --needed \
    cmake \
    ninja \
    gcc
```

> **Note:** EndeavourOS ships GCC 13+ in the standard repos. Verify with `gcc --version`.
> The minimum required is GCC 12 (`-std=c++20` with full `std::jthread` support).

---

## 3. Install Library Dependencies

All of the following are available in the official Arch/EndeavourOS repositories.

```bash
paru -S --needed \
    postgresql-libs \
    libpqxx \
    openssl \
    libgit2 \
    nlohmann-json \
    spdlog \
    jansson \
    curl \
    glib2 \
    libxml2 \
    xmlsec
```

> **Note:** `postgresql` (the server) is not listed here because this guide assumes an
> existing PostgreSQL 15+ instance is already running. Only the client library
> (`postgresql-libs`) is required to build and link against `libpq`.

### What each package provides

| Package | Provides | Used For |
|---------|----------|----------|
| `postgresql-libs` | `libpq.so`, `pg_config` | PostgreSQL C client library (runtime + headers) |
| `libpqxx` | `libpqxx.so`, `<pqxx/*.hxx>` | C++ PostgreSQL client (`libpqxx` DAL layer) |
| `openssl` | `libssl.so`, `libcrypto.so` | AES-256-GCM credential encryption, Argon2id hashing |
| `libgit2` | `libgit2.so`, `<git2.h>` | GitOps mirror subsystem (`libgit2` native bindings) |
| `nlohmann-json` | `<nlohmann/json.hpp>` | JSON serialization (header-only) |
| `spdlog` | `libspdlog.so`, `<spdlog/spdlog.h>` | Structured logging (JSON output, configurable levels) |
| `jansson` | `libjansson.so`, `<jansson.h>` | JSON parsing for liboauth2 (OIDC JWT handling) |
| `curl` | `libcurl.so`, `<curl/curl.h>` | HTTP client for liboauth2 (JWKS fetching) |
| `glib2` | `libglib-2.0.so`, `<glib.h>` | GObject runtime required by lasso |
| `libxml2` | `libxml2.so`, `<libxml/parser.h>` | XML parsing for SAML (lasso dependency) |
| `xmlsec` | `libxmlsec1.so` | XMLDSig signature verification (lasso dependency) |

### SAML support (lasso)

SAML 2.0 protocol operations use [lasso](https://lasso.entrouvert.org/) for proper XMLDSig
signature verification. On Arch Linux, install lasso from the AUR:

```bash
paru -S lasso
```

> **Note:** Lasso requires `glib2`, `libxml2`, and `xmlsec` as runtime deps.
> These are pulled automatically when installing the `lasso` package.
> The Dockerfile uses Fedora's `lasso-devel` package from the system repos.

### OIDC support (liboauth2 + cjose)

OIDC JWT/JWKS signature verification uses [liboauth2](https://github.com/OpenIDC/liboauth2)
(with [cjose](https://github.com/OpenIDC/cjose) for JOSE operations). Both libraries are
**built from source** automatically via CMake `ExternalProject_Add` — no manual installation
is required.

When you run `cmake -B build`, CMake clones and compiles:
- **cjose v0.6.2.3** (JOSE/JWK/JWS/JWE library)
- **liboauth2 v2.1.0** (OAuth 2.0 token verification)

The only system packages needed are their build-time dependencies: `jansson` (JSON),
`curl` (HTTP), and `openssl` (cryptography). The `autoconf`, `automake`, and `libtool`
tools are also required (provided by `base-devel`).

---

## 4. Install AUR Dependencies

The following libraries are not in the official repos and must be installed from the AUR.

### 4.1 Crow (HTTP Framework)

The API layer uses **Crow** (CrowCpp) for the HTTP server. Crow is acquired automatically via
CMake FetchContent at configure time — **no system package installation required**.

When you run `cmake -B build`, CMake downloads and configures Crow v1.3.1 from GitHub.
Nothing to install manually.

### 4.2 AUR Dependencies

There are no AUR dependencies. All third-party libraries are either in the official Arch
repos or acquired via CMake FetchContent at build time.

> **Note:** FTXUI is no longer required in this repository. The TUI client is maintained
> as a separate project — see [TUI Client Design](TUI_DESIGN.md).

### 4.4 Testing Framework (Google Test)

Google Test and Google Mock are pulled automatically via CMake `FetchContent` at configure
time. No system package is required.

```cmake
# Handled in tests/CMakeLists.txt — no manual install needed
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0)
FetchContent_MakeAvailable(googletest)
```

---

## 5. PostgreSQL Setup

The application requires **PostgreSQL 15+**. This section assumes you already have a
PostgreSQL instance running and accessible. If you need to install PostgreSQL from scratch,
see the [Arch Wiki — PostgreSQL](https://wiki.archlinux.org/title/PostgreSQL).

> **Version check:** Confirm your instance meets the minimum version requirement.
> ```bash
> psql --version
> # Expected: psql (PostgreSQL) 15.x or higher
> ```

### 5.1 Create the Development Database and User

Connect to your existing instance and provision the application database and role.

**If your instance uses peer authentication (local socket):**

```bash
sudo -u postgres psql <<'EOF'
CREATE USER dns WITH PASSWORD 'dns';
CREATE DATABASE meridian_dns OWNER dns;
GRANT ALL PRIVILEGES ON DATABASE meridian_dns TO dns;
EOF
```

**If your instance uses password authentication (TCP/IP):**

```bash
psql -h localhost -U postgres <<'EOF'
CREATE USER dns WITH PASSWORD 'dns';
CREATE DATABASE meridian_dns OWNER dns;
GRANT ALL PRIVILEGES ON DATABASE meridian_dns TO dns;
EOF
```

> **Existing user/database:** If the `dns` role or `meridian_dns` database already
> exist from a previous setup, skip the relevant `CREATE` statements and ensure the role
> has `ALL PRIVILEGES` on the database.

### 5.2 Verify Connectivity

Confirm the new role can connect before proceeding:

```bash
psql postgresql://dns:dns@localhost:5432/meridian_dns -c "SELECT version();"
```

A successful response confirms the connection string that goes into `DNS_DB_URL`.

### 5.3 Run Database Migrations

After building the project (see §7), apply the schema migrations in order:

```bash
psql postgresql://dns:dns@localhost:5432/meridian_dns \
    -f scripts/db/001_initial_schema.sql

psql postgresql://dns:dns@localhost:5432/meridian_dns \
    -f scripts/db/002_add_indexes.sql
```

> **Tip:** Migrations are numbered sequentially in `scripts/db/`. Run them in ascending
> numeric order. The application's `--migrate` flag (used in Docker) will automate this
> at runtime once implemented.

---

## 6. Clone the Repository

```bash
git clone <repository-url> meridian-dns
cd meridian-dns

# Initialize and update the workflow-orchestration skill submodule
git submodule update --init --recursive
```

> The `.gitmodules` file registers `.roo/skills/workflow-orchestration` as a submodule.
> The `--recursive` flag ensures it is checked out correctly.

---

## 7. Configure and Build

### 7.1 Configure with CMake

```bash
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_CXX_STANDARD=20
```

For a release build (optimized, no debug symbols):

```bash
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_CXX_STANDARD=20
```

### 7.2 Build

```bash
cmake --build build --parallel
```

The compiled binary will be located at `build/meridian-dns`.

### 7.3 Build with Verbose Output (Troubleshooting)

```bash
cmake --build build --parallel --verbose
```

---

## 8. Verify the Build

### 8.1 Check the Binary

```bash
./build/meridian-dns --version
```

### 8.2 Run Unit Tests

```bash
ctest --test-dir build --output-on-failure
```

### 8.3 Smoke Test Against Local PostgreSQL

Set the minimum required environment variables and start the server:

```bash
export DNS_DB_URL="postgresql://dns:dns@localhost:5432/meridian_dns"
export DNS_MASTER_KEY="$(openssl rand -hex 32)"
export DNS_JWT_SECRET="$(openssl rand -hex 32)"

./build/meridian-dns
```

The server should log `meridian-dns ready` and begin listening on port `8080`.

Verify the health endpoint:

```bash
curl -s http://localhost:8080/api/v1/health | python3 -m json.tool
# Expected: {"status":"ok"}
```

---

## 9. Development Environment Variables

Create a `.env` file in the project root for local development. **Do not commit this file.**

```bash
# .env — local development only
DNS_DB_URL=postgresql://dns:dns@localhost:5432/meridian_dns
DNS_DB_POOL_SIZE=5

# Generate once: openssl rand -hex 32
DNS_MASTER_KEY=<32-byte-hex-string>
DNS_JWT_SECRET=<32-byte-hex-string>

DNS_HTTP_PORT=8080
DNS_HTTP_THREADS=4
DNS_LOG_LEVEL=debug
DNS_AUDIT_STDOUT=true

# Optional: GitOps mirror (leave unset to disable)
# DNS_GIT_REMOTE_URL=git@github.com:yourorg/dns-mirror.git
# DNS_GIT_LOCAL_PATH=/tmp/meridian-dns-repo
# DNS_GIT_SSH_KEY_PATH=/home/youruser/.ssh/id_ed25519
```

Load the file in your shell session:

```bash
set -a && source .env && set +a
```

> **Security:** Add `.env` to `.gitignore` immediately. The `DNS_MASTER_KEY` encrypts all
> provider API tokens at rest — losing it makes stored credentials unrecoverable.

---

## 10. Package Reference Table

Complete mapping of Dockerfile/Fedora package names to their Arch/AUR equivalents.

| Fedora Package (Dockerfile) | Arch/AUR Package | Source | Notes |
|-----------------------------|------------------|--------|-------|
| `cmake` | `cmake` | Official | Build system |
| `ninja-build` | `ninja` | Official | Build backend |
| `gcc-c++` | `gcc` | Official | GCC 13+ ships by default; supports C++20 |
| `libpqxx-devel` | `libpqxx` | Official | C++ PostgreSQL client |
| `openssl-devel` | `openssl` | Official | OpenSSL 3.x headers + libs |
| `libgit2-devel` | `libgit2` | Official | libgit2 headers + libs |
| *(none)* | *(none)* | FetchContent | Crow HTTP framework (auto-fetched by CMake) |
| `nlohmann-json-devel` | `nlohmann-json` | Official | Header-only JSON library |
| `lasso-devel` | `lasso` | AUR | SAML 2.0 protocol (lasso + xmlsec1) |
| `libxml2-devel` | `libxml2` | Official | XML parsing (lasso dependency) |
| `xmlsec1-devel` | `xmlsec` | Official | XMLDSig verification (lasso dependency) |
| `glib2-devel` | `glib2` | Official | GObject runtime (lasso dependency) |
| `jansson-devel` | `jansson` | Official | JSON parsing (liboauth2 dependency) |
| `libcurl-devel` | `curl` | Official | HTTP client (liboauth2 dependency) |
| `cjose-devel` | *(built from source)* | ExternalProject | JOSE library — auto-built by CMake |
| *(none)* | *(built from source)* | ExternalProject | liboauth2 — auto-built by CMake |
| `postgresql` (runtime) | `postgresql` | Official | PostgreSQL 15+ server — **assumed pre-installed** |
| `libpq5` (runtime) | `postgresql-libs` | Official | PostgreSQL runtime client library |
| `openssl` (runtime) | `openssl` | Official | OpenSSL 3.x runtime |
| `libgit2` (runtime) | `libgit2` | Official | libgit2 runtime |

---

## Quick-Start Summary

```bash
# 1. System update
paru -Syu

# 2. All dependencies in one shot (official repos)
paru -S --needed \
    base-devel git cmake ninja gcc \
    postgresql-libs libpqxx \
    openssl libgit2 nlohmann-json \
    spdlog jansson curl \
    glib2 libxml2 xmlsec

# 3. SAML support (from AUR)
paru -S lasso

# 4. Provision database on existing PostgreSQL 15+ instance
# (adjust connection method if your instance uses TCP/IP auth instead of peer auth)
sudo -u postgres psql -c "CREATE USER dns WITH PASSWORD 'dns';"
sudo -u postgres psql -c "CREATE DATABASE meridian_dns OWNER dns;"
sudo -u postgres psql -c "GRANT ALL PRIVILEGES ON DATABASE meridian_dns TO dns;"
# Verify: psql postgresql://dns:dns@localhost:5432/meridian_dns -c "SELECT version();"

# 5. Clone and initialize submodules
git clone <repository-url> meridian-dns && cd meridian-dns
git submodule update --init --recursive

# 6. Build (liboauth2 + cjose are auto-built from source)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=20
cmake --build build --parallel

# 7. Run migrations
psql postgresql://dns:dns@localhost:5432/meridian_dns -f scripts/db/001_initial_schema.sql
psql postgresql://dns:dns@localhost:5432/meridian_dns -f scripts/db/002_add_indexes.sql

# 8. Start
export DNS_DB_URL="postgresql://dns:dns@localhost:5432/meridian_dns"
export DNS_MASTER_KEY="$(openssl rand -hex 32)"
export DNS_JWT_SECRET="$(openssl rand -hex 32)"
./build/meridian-dns
```
