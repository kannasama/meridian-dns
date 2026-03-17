# Contributing to Meridian DNS

Thank you for considering a contribution to Meridian DNS! This document explains the
process for contributing.

## Getting Started

1. Fork the repository
2. Create a feature branch from `main`
3. Set up the [build environment](docs/BUILD_ENVIRONMENT.md)
4. Make your changes following the [code standards](docs/CODE_STANDARDS.md)

## Code Standards

- **C++20** with `-Wall -Wextra -Wpedantic -Werror`
- Hungarian notation naming conventions (see `docs/CODE_STANDARDS.md`)
- 2-space indentation, 100-character line limit
- `#pragma once` for header guards
- SPDX license headers on all source files

## Commit Messages

Use [Conventional Commits](https://www.conventionalcommits.org/):

```
feat(scope): add new feature
fix(scope): correct a bug
docs: update documentation
test: add or update tests
refactor(scope): code change that neither fixes a bug nor adds a feature
build: changes to build system or dependencies
```

## Pull Request Process

1. Ensure the build succeeds with no warnings:
   - **Docker (recommended):** `docker buildx build .`
   - **Native:** `cmake --build build --parallel`
2. All tests pass: `build/tests/dns-tests`
3. SPDX headers present: `scripts/check-license-headers.sh`
4. Update relevant documentation if behavior changes
5. Open a PR against `main` with a clear description

## Reporting Issues

Use GitHub Issues for bug reports and feature requests. Include:
- Steps to reproduce (for bugs)
- Expected vs actual behavior
- Meridian DNS version (`meridian-dns --version`)
- Database version and OS

## Security Vulnerabilities

**Do not** open a public issue for security vulnerabilities. See [SECURITY.md](SECURITY.md)
for the responsible disclosure process.

## License

Meridian DNS is licensed under the [AGPL-3.0-or-later](LICENSE). By submitting a
contribution, you agree that your work will be licensed under the same terms.
