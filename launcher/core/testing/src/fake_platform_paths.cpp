#include "reboot/testing/fake_platform_paths.hpp"

#include "reboot/foundation/native_path.hpp"

namespace reboot::testing {

NativePath default_fake_root() {
#ifdef _WIN32
    return NativePath(L"C:/reboot-test");
#else
    return NativePath("/reboot-test");
#endif
}

}  // namespace reboot::testing
