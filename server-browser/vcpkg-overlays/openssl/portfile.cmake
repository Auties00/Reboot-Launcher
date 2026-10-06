# Intentionally empty: consumers' find_package(OpenSSL) resolves to the system installation
# (libssl-dev >= 3.5), which MsQuic is also built against. Two OpenSSL copies in one process
# would split TLS state between MsQuic and the service.
set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
