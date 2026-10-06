# syntax=docker/dockerfile:1.7
# Build:  docker build -f deploy/docker/Dockerfile -t sb-edge .        (context: server-browser/)
# Tests:  docker build -f deploy/docker/Dockerfile --target test .

ARG GCC_IMAGE=gcc:16
ARG MSQUIC_VERSION=v2.6.2
ARG VCPKG_COMMIT=19780d9cdf84d0944cf9a318666703b89ab6629c
ARG NATS_VERSION=v2.15.0

# ---- toolchain: GCC 16, CMake, system OpenSSL 3.5, MsQuic 2.6 (app-owned execution) ----------
FROM ${GCC_IMAGE} AS toolchain
ARG MSQUIC_VERSION
ARG VCPKG_COMMIT
ARG NATS_VERSION
RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake ninja-build git curl zip unzip tar pkg-config perl python3 libssl-dev libnuma-dev \
    && rm -rf /var/lib/apt/lists/*
# JetStream per-message TTLs need nats-server >= 2.11; distribution packages lag behind.
RUN curl -fsSL "https://github.com/nats-io/nats-server/releases/download/${NATS_VERSION}/nats-server-${NATS_VERSION}-linux-amd64.tar.gz" \
        | tar xz -C /tmp && mv /tmp/nats-server-*/nats-server /usr/local/bin/ && rm -rf /tmp/nats-server-*
# vcpkg's msquic port predates app-owned execution (2.5+), so MsQuic is built here against the
# system OpenSSL 3.5, the same library the service links.
RUN git clone --depth 1 --branch ${MSQUIC_VERSION} https://github.com/microsoft/msquic /src/msquic \
    && git -C /src/msquic submodule update --init --depth 1 submodules/clog \
    && cmake -S /src/msquic -B /src/msquic/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DQUIC_TLS_LIB=openssl -DQUIC_USE_EXTERNAL_OPENSSL=ON -DQUIC_BUILD_SHARED=ON \
        -DQUIC_BUILD_TOOLS=OFF -DQUIC_BUILD_TEST=OFF -DQUIC_BUILD_PERF=OFF -DQUIC_ENABLE_LOGGING=OFF \
        -DQUIC_EMBED_GIT_HASH=OFF -DCMAKE_INSTALL_PREFIX=/opt/msquic \
    && cmake --build /src/msquic/build && cmake --install /src/msquic/build && rm -rf /src/msquic
RUN git clone https://github.com/microsoft/vcpkg /opt/vcpkg && git -C /opt/vcpkg checkout ${VCPKG_COMMIT} \
    && /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics
ENV VCPKG_ROOT=/opt/vcpkg PATH=/opt/vcpkg:$PATH LD_LIBRARY_PATH=/opt/msquic/lib

# ---- dependencies (cached separately from sources) --------------------------------------------
FROM toolchain AS deps
WORKDIR /src/server-browser
COPY vcpkg.json vcpkg-configuration.json ./
COPY vcpkg-overlays ./vcpkg-overlays
RUN --mount=type=cache,target=/root/.cache/vcpkg \
    vcpkg install --triplet x64-linux --x-install-root=/opt/vcpkg_installed

# ---- build -----------------------------------------------------------------------------------
FROM deps AS build
COPY . .
RUN --mount=type=cache,target=/root/.cache/vcpkg \
    cmake --preset gcc-release -DVCPKG_INSTALLED_DIR=/opt/vcpkg_installed \
        -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
    && cmake --build --preset gcc-release -- -k 0

# ---- test: unit + integration (inproc and a local NATS JetStream) ------------------------------
FROM build AS test
RUN (nats-server -js -sd /tmp/js -p 4222 >/tmp/nats.log 2>&1 &) \
    && sleep 1 && SB_TEST_LOGS=1 SB_NATS_URL=nats://127.0.0.1:4222 ctest --preset gcc-release --output-on-failure

# ---- sanitizers and fuzzing (Clang: Boost.PFR codec path, libFuzzer) -------------------------
FROM deps AS sanitize
ARG LLVM_VERSION=21
RUN apt-get update && apt-get install -y --no-install-recommends lsb-release wget gnupg \
    && wget -qO- https://apt.llvm.org/llvm.sh | bash -s -- ${LLVM_VERSION} \
    && ln -sf /usr/bin/clang-${LLVM_VERSION} /usr/local/bin/clang \
    && ln -sf /usr/bin/clang++-${LLVM_VERSION} /usr/local/bin/clang++ \
    && rm -rf /var/lib/apt/lists/*
COPY . .
RUN --mount=type=cache,target=/root/.cache/vcpkg \
    cmake --preset clang-asan -DVCPKG_INSTALLED_DIR=/opt/vcpkg_installed \
    && cmake --build --preset clang-asan -- -k 0 \
    && (nats-server -js -sd /tmp/js -p 4222 >/tmp/nats.log 2>&1 &) && sleep 1 \
    && SB_TEST_LOGS=1 SB_NATS_URL=nats://127.0.0.1:4222 ctest --preset clang-asan --output-on-failure \
    && for f in fuzz_messages fuzz_framer fuzz_mirror; do \
           ./build/clang-asan/fuzz/$f -max_total_time=60 -rss_limit_mb=4096 || exit 1; \
       done

# ---- runtime ---------------------------------------------------------------------------------
FROM gcr.io/distroless/cc-debian13 AS runtime
COPY --from=toolchain /opt/msquic/lib/libmsquic.so* /usr/lib/
COPY --from=build /src/server-browser/build/gcc-release/apps/sb-edge /usr/local/bin/sb-edge
COPY --from=build /src/server-browser/build/gcc-release/apps/sb-cli /usr/local/bin/sb-cli
COPY --from=build /src/server-browser/build/gcc-release/apps/sb-loadgen /usr/local/bin/sb-loadgen
USER nonroot
EXPOSE 443/udp 9100/tcp
ENTRYPOINT ["/usr/local/bin/sb-edge"]
CMD ["--config", "/etc/sb/edge.toml"]
