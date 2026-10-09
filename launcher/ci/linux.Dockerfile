# syntax=docker/dockerfile:1.7
# The launcher's Linux CI build on the GCC 15 floor. Context: the repository root, because the
# launcher builds ../server-browser's sb_contract.
#   docker build -f launcher/ci/linux.Dockerfile --target test .

ARG GCC_IMAGE=gcc:15-trixie
ARG MSQUIC_VERSION=v2.6.2
ARG VCPKG_COMMIT=19780d9cdf84d0944cf9a318666703b89ab6629c
ARG PRESET=linux-gcc15-debug

# ---- toolchain: GCC 15, CMake, system OpenSSL 3.5, MsQuic 2.6 -----------------------------------
FROM ${GCC_IMAGE} AS toolchain
ARG MSQUIC_VERSION
ARG VCPKG_COMMIT
RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake ninja-build git curl zip unzip tar pkg-config perl python3 binutils libssl-dev libnuma-dev \
    && rm -rf /var/lib/apt/lists/* \
    && ln -sf /usr/local/bin/gcc /usr/local/bin/gcc-15 && ln -sf /usr/local/bin/g++ /usr/local/bin/g++-15
# As in server-browser: MsQuic built against the system OpenSSL that the launcher also links.
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

# ---- dependencies (cached separately from sources) -----------------------------------------------
FROM toolchain AS deps
WORKDIR /src/launcher
COPY launcher/vcpkg.json launcher/vcpkg-configuration.json ./
COPY launcher/vcpkg-overlays ./vcpkg-overlays
COPY server-browser/vcpkg-overlays /src/server-browser/vcpkg-overlays
RUN --mount=type=cache,target=/root/.cache/vcpkg \
    vcpkg install --triplet x64-linux --overlay-ports=/src/server-browser/vcpkg-overlays \
        --x-install-root=/opt/vcpkg_installed

# ---- build: every target with the floor probe, then the DAG check --------------------------------
# A failed build or DAG check is recorded rather than fatal, so the test stage still runs what built.
FROM deps AS build
ARG PRESET
COPY server-browser /src/server-browser
COPY launcher /src/launcher
RUN --mount=type=cache,target=/root/.cache/vcpkg \
    cmake --preset ${PRESET} -DVCPKG_INSTALLED_DIR=/opt/vcpkg_installed -DREBOOT_FLOOR_CHECK=ON
RUN cmake --build --preset ${PRESET} -- -k 0; echo $? > /tmp/build.rc
RUN cmake --build --preset ${PRESET} --target dag_check; echo $? > /tmp/dag.rc

# ---- test: unit, contract and conformance suites, then the CI gates, as a regular user -----------
# The gates here are message catalog completeness and the secret grep; Linux has no Velopack package.
FROM build AS test
ARG PRESET
RUN useradd --create-home ci && chown -R ci /src/launcher/build
USER ci
RUN ctest --preset ${PRESET} --no-tests=error -LE gate; test=$?; \
    ctest --preset ${PRESET} --no-tests=error -L gate; gates=$?; \
    echo "build: $(cat /tmp/build.rc), dag_check: $(cat /tmp/dag.rc), ctest: $test, gates: $gates"; \
    [ "$(cat /tmp/build.rc)" = 0 ] && [ "$(cat /tmp/dag.rc)" = 0 ] && [ "$test" = 0 ] && [ "$gates" = 0 ]
