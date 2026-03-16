# Build Environment Setup

## Overview

Meridian DNS is built on **Fedora 43** and packaged as a multi-stage Docker image.
The recommended build method uses Docker — native builds are possible on Fedora 43
or Arch Linux with the appropriate dependencies installed.

---

## Option 1: Docker Build (Recommended)

The Docker build is self-contained — all dependencies are installed inside the
container. No local toolchain required beyond Docker.

### Prerequisites

- Docker 24+ with BuildKit
- `docker buildx` available (included in Docker Desktop and modern Docker CE)

### Build

```bash
docker buildx build -t meridian-dns:dev .
```

This runs a three-stage build:
1. **UI stage** — Node 22 builds the Vue 3 frontend (`npm ci && npm run build`)
2. **C++ stage** — Fedora 43 builds the server binary (`cmake + ninja`)
3. **Runtime stage** — Minimal Fedora 43 image with only runtime libraries

### Verify

```bash
docker run --rm meridian-dns:dev meridian-dns --version
# Expected: meridian-dns 1.0.0
```

### Run with Docker Compose

See [DEPLOYMENT.md](DEPLOYMENT.md) for the full Docker Compose setup with PostgreSQL.

---

## Option 2: Native Build — Fedora 43

This matches the Docker build environment exactly.

### Install Build Tools

```bash
sudo dnf install -y \
  cmake ninja-build gcc-c++ \
  git ca-certificates \
  autoconf automake libtool \
  pkgconf-pkg-config
```

### Install Library Dependencies

```bash
sudo dnf install -y \
  libpqxx-devel openssl-devel libgit2-devel \
  json-devel spdlog-devel \
  asio-devel \
  lasso-devel libxml2-devel xmlsec1-devel xmlsec1-openssl-devel glib2-devel \
  jansson-devel libcurl-devel cjose-devel \
  httpd-devel pcre2-devel
```

### Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

### Verify

```bash
./build/src/meridian-dns --version
# Expected: meridian-dns 1.0.0
```

---

## Option 3: Native Build — Arch / EndeavourOS

### Install Dependencies

```bash
paru -S --needed \
    base-devel git cmake ninja gcc \
    postgresql-libs libpqxx \
    openssl libgit2 nlohmann-json \
    spdlog jansson curl \
    glib2 libxml2 xmlsec

# SAML support (AUR)
paru -S lasso
```

### Build

Same as Fedora:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

> **Note:** cjose and liboauth2 are built from source automatically via CMake
> `ExternalProject_Add`. Crow and Google Test are fetched via `FetchContent`.

---

## PostgreSQL Setup

The application requires **PostgreSQL 15+**.

### Using Docker Compose (easiest)

```bash
cp .env.example .env
# Fill in DNS_MASTER_KEY and DNS_JWT_SECRET (see DEPLOYMENT.md)
docker compose up -d db
```

### Using a Local Instance

```bash
sudo -u postgres psql <<'EOF'
CREATE USER dns WITH PASSWORD 'dns';
CREATE DATABASE meridian_dns OWNER dns;
GRANT ALL PRIVILEGES ON DATABASE meridian_dns TO dns;
EOF
```

Verify connectivity:

```bash
psql postgresql://dns:dns@localhost:5432/meridian_dns -c "SELECT version();"
```

---

## Running Migrations

Migrations run automatically on startup in Docker (via the entrypoint script).

For native builds, use the `--migrate` flag:

```bash
export DNS_DB_URL="postgresql://dns:dns@localhost:5432/meridian_dns"
./build/src/meridian-dns --migrate
```

---

## Running Tests

### Unit Tests (no database required)

```bash
./build/tests/dns-tests --gtest_filter=-*Integration*
```

### Integration Tests (requires PostgreSQL)

```bash
export DNS_DB_URL="postgresql://dns:dns@localhost:5432/meridian_dns"
./build/tests/dns-tests
```

### SPDX License Header Check

```bash
scripts/check-license-headers.sh
```

---

## UI Development

The Vue 3 frontend is in the `ui/` directory.

```bash
cd ui
npm install
npm run dev     # Vite dev server on :5173, proxies /api/v1 to :8080
npm run build   # Production build to ui/dist/
```

---

## Development Environment Variables

Create a `.env` file in the project root (**do not commit**):

```bash
DNS_DB_URL=postgresql://dns:dns@localhost:5432/meridian_dns
DNS_MASTER_KEY=$(openssl rand -hex 32)
DNS_JWT_SECRET=$(openssl rand -hex 32)
DNS_HTTP_PORT=8080
DNS_LOG_LEVEL=debug
DNS_AUDIT_STDOUT=true
```

Load in your shell:

```bash
set -a && source .env && set +a
./build/src/meridian-dns
```

> **Security:** The `DNS_MASTER_KEY` encrypts all provider API tokens at rest.
> Losing it makes stored credentials unrecoverable.

---

## Package Reference

| Component | Fedora 43 Package | Arch Package | Notes |
|-----------|-------------------|-------------|-------|
| C++ compiler | `gcc-c++` | `gcc` | GCC 13+, C++20 |
| Build system | `cmake`, `ninja-build` | `cmake`, `ninja` | CMake 3.20+ |
| PostgreSQL client | `libpqxx-devel` | `libpqxx` | C++ PostgreSQL |
| OpenSSL | `openssl-devel` | `openssl` | AES-256-GCM, Argon2id |
| Git library | `libgit2-devel` | `libgit2` | GitOps subsystem |
| JSON | `json-devel` | `nlohmann-json` | Header-only |
| Logging | `spdlog-devel` | `spdlog` | Structured logging |
| SAML | `lasso-devel` | `lasso` (AUR) | SAML 2.0 protocol |
| OIDC | *(built from source)* | *(built from source)* | liboauth2 + cjose via CMake |
| HTTP framework | *(FetchContent)* | *(FetchContent)* | Crow v1.3.1 |
| Testing | *(FetchContent)* | *(FetchContent)* | Google Test v1.14.0 |
