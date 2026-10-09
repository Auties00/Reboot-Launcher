#include "agent_service.hpp"

#include "unistd.hpp"

#import <Foundation/Foundation.h>
#import <ServiceManagement/ServiceManagement.h>

#include <optional>

namespace reboot::os_macos::ipc {
namespace {

[[nodiscard]] NSString* to_ns_string(std::string_view text) {
    return [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
}

[[nodiscard]] SMAppService* agent(std::string_view plist_name) {
    return [SMAppService agentServiceWithPlistName:to_ns_string(plist_name)];
}

}  // namespace

bool main_bundle_has_agent(std::string_view plist_name) {
    @autoreleasepool {
        NSURL* const agents = [[[NSBundle mainBundle] bundleURL] URLByAppendingPathComponent:@"Contents/Library/LaunchAgents"
                                                                                  isDirectory:YES];
        NSURL* const plist = [agents URLByAppendingPathComponent:to_ns_string(plist_name) isDirectory:NO];
        return [plist checkResourceIsReachableAndReturnError:nil] == YES;
    }
}

AgentStatus agent_status(std::string_view plist_name) {
    @autoreleasepool {
        switch (agent(plist_name).status) {
            case SMAppServiceStatusNotRegistered: return AgentStatus::NotRegistered;
            case SMAppServiceStatusEnabled: return AgentStatus::Enabled;
            case SMAppServiceStatusRequiresApproval: return AgentStatus::RequiresApproval;
            case SMAppServiceStatusNotFound: return AgentStatus::NotFound;
        }
        return AgentStatus::NotFound;
    }
}

Result<void> agent_register(std::string_view plist_name, std::string_view label) {
    @autoreleasepool {
        NSError* error = nil;
        if ([agent(plist_name) registerAndReturnError:&error] == YES) return {};
        const std::optional<i64> code = error != nil ? std::optional<i64>{static_cast<i64>(error.code)} : std::nullopt;
        return std::unexpected(agent_register_failed(label, code));
    }
}

}  // namespace reboot::os_macos::ipc
