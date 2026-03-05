# ── Stage 1: Build ──────────────────────────────────────────────────────────
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
  cmake ninja-build g++ \
  libpqxx-dev libssl-dev libgit2-dev \
  nlohmann-json3-dev libspdlog-dev \
  git ca-certificates \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

RUN cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  && cmake --build build --parallel

# ── Stage 2: Runtime ─────────────────────────────────────────────────────────
FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
  libpq5 libssl3 libgit2-1.5 libspdlog1.10 \
  && rm -rf /var/lib/apt/lists/*

RUN useradd --system --no-create-home meridian-dns

COPY --from=builder /build/build/meridian-dns /usr/local/bin/meridian-dns
COPY scripts/db/ /opt/meridian-dns/db/
COPY scripts/docker/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

USER meridian-dns
EXPOSE 8080

ENTRYPOINT ["/entrypoint.sh"]
CMD ["meridian-dns"]
