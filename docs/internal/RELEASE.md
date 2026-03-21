# Meridian DNS — Release Pipeline Design

> This document is git-ignored (operational, not for public distribution).

## Container Image Strategy

- **Docker Hub:** `kannasama/meridian-dns` — primary public distribution
- **GHCR:** `ghcr.io/kannasama/meridian-dns` — secondary, alongside GitHub mirror
- **Tags:** `latest`, semver (`1.0.0`), major (`1`), major.minor (`1.0`)
- **Architecture:** amd64-only for v1.0; arm64 planned for v1.1+

## Build Process

All builds use Docker — native builds are not supported due to library availability.

```bash
# Build the runtime image (cmake, UI, compiler flags handled by multi-stage Dockerfile)
docker buildx build -t meridian-dns:local .

# Build the builder stage (needed for running tests — the runtime image does not
# include the test binary)
docker buildx build --target builder -t meridian-dns:builder .
```

### Running Tests

The test binary is only available in the builder stage image
(`meridian-dns:builder`), not the final runtime image.

**Unit tests** (no database required):

```bash
docker run --rm meridian-dns:builder \
  /build/build/tests/dns-tests --gtest_filter=-*Integration*
```

**DB integration tests** require a running PostgreSQL instance. Two options:

Option A — Use docker-compose (recommended):

```bash
# Start only the database
docker compose up -d db

# Run integration tests against the compose database
docker run --rm --network host \
  -e DNS_DB_URL="postgresql://dns:dns_dev_password@localhost:5432/meridian_dns" \
  meridian-dns:builder \
  /build/build/tests/dns-tests --gtest_filter=*Integration*
```

Option B — Use an external dev/staging database:

```bash
docker run --rm \
  -e DNS_DB_URL="postgresql://user:pass@db-host:5432/meridian_dns_test" \
  meridian-dns:builder \
  /build/build/tests/dns-tests --gtest_filter=*Integration*
```

**Full test suite** (unit + integration combined):

```bash
docker compose up -d db

docker run --rm --network host \
  -e DNS_DB_URL="postgresql://dns:dns_dev_password@localhost:5432/meridian_dns" \
  meridian-dns:builder \
  /build/build/tests/dns-tests
```

## CI/CD Pipeline Design

### Release Workflow (triggered by `v*` tag push)

1. **Build stage:**
   - `docker buildx build --target builder -t meridian-dns:ci-builder .`
   - `docker buildx build -t meridian-dns:ci .`
   - Multi-stage Dockerfile (Fedora 43 builder → runtime image)
   - Handles cmake, UI build (`npm ci && npm run build`), and compiler
     flags (`-Wall -Wextra -Wpedantic -Werror`) internally
2. **Test stage:**
   - Unit tests: run inside builder image (`--gtest_filter=-*Integration*`)
   - DB integration tests: run builder image against a PostgreSQL service container
   - License header check: `scripts/check-license-headers.sh`
3. **Publish stage:**
   - Build and push to Docker Hub + GHCR
   - `docker buildx build --push --tag kannasama/meridian-dns:1.0.0 --tag ghcr.io/kannasama/meridian-dns:1.0.0 .`
   - Tag variants: `latest`, `1`, `1.0`, `1.0.0`
4. **Release stage:**
   - Create GitHub release entry

## Release Checklist

1. [ ] All tests pass (unit + DB integration): see [Build Process](#build-process)
2. [ ] Clean build with no warnings: `docker buildx build .`
3. [ ] Version bumped in `CMakeLists.txt` (`project(VERSION X.Y.Z)`)
4. [ ] `CHANGELOG.md` updated with release date
5. [ ] OpenAPI spec version matches (`docs/openapi.yaml` → `info.version`)
6. [ ] All SPDX headers present (`scripts/check-license-headers.sh`)
7. [ ] Screenshots captured and added to `docs/screenshots/`
8. [ ] Documentation reviewed for accuracy
9. [ ] Git tag `vX.Y.Z` created and pushed
10. [ ] Docker images built and pushed to Docker Hub + GHCR
11. [ ] Release entry created on GitHub
12. [ ] Verify image pullable: `docker pull kannasama/meridian-dns:X.Y.Z`
