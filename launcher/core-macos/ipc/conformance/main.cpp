#include <catch2/catch_session.hpp>

#include <string_view>

#include "root_helper.hpp"

// The suite runs this executable again as root (root_helper.hpp), so main dispatches first.
int main(int argc, char* argv[]) {
    if (argc > 1 && std::string_view{argv[1]} == rb::os_macos::ipc::kRootHelperFlag)
        return rb::os_macos::ipc::run_root_helper(argc, argv);
    return Catch::Session().run(argc, argv);
}
