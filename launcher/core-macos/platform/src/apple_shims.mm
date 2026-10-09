#include "darwin.hpp"

#include "apple_shims.hpp"

#import <AppKit/AppKit.h>
#import <CoreServices/CoreServices.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <ServiceManagement/ServiceManagement.h>

#include <sys/stat.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

#include "messages.hpp"
#include "reboot/posix/posix_error.hpp"

// Cocoa's macros and bridged casts are C-style casts.
#pragma clang diagnostic ignored "-Wold-style-cast"

namespace reboot::os_macos::platform::shims {

namespace {

// How long the asynchronous LaunchServices call that sets the scheme handler is awaited.
constexpr int64_t kHandlerTimeoutSeconds = 10;

[[nodiscard]] std::string utf8(NSString* text) {
    const char* bytes = text == nil ? nullptr : text.UTF8String;
    return bytes == nullptr ? std::string{} : std::string(bytes);
}

// The NSError's code travels as the OS error and its domain as the detail.
[[nodiscard]] Diagnostic with_error(DiagBuilder diag, NSError* error) {
    if (error != nil) {
        std::move(diag).os(SystemError{SystemError::Origin::Host, static_cast<i64>(error.code)});
        std::move(diag).detail(utf8(error.domain));
    }
    return std::move(diag).build();
}

[[nodiscard]] Diagnostic failed(std::string_view call, NSError* error) {
    return with_error(make_diag(ErrorDomain::Platform, kCallFailed).arg("call", call), error);
}

[[nodiscard]] Diagnostic failed_on(std::string_view call, const NativePath& path, NSError* error) {
    return with_error(make_diag(ErrorDomain::Platform, kCallFailedOnPath).arg("call", call).arg("path", path), error);
}

[[nodiscard]] NSString* ns_string(std::string_view text) {
    return [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
}

[[nodiscard]] NSURL* file_url(const NativePath& path) {
    struct stat info {};
    const bool directory = ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
    return [NSURL fileURLWithFileSystemRepresentation:path.c_str() isDirectory:directory relativeToURL:nil];
}

[[nodiscard]] NativePath path_of(NSURL* url) { return NativePath{std::string(url.fileSystemRepresentation)}; }

// Opening or revealing a missing path must say so, not fail somewhere inside LaunchServices.
[[nodiscard]] Result<void> require_exists(const NativePath& path) {
    struct stat info {};
    if (::lstat(path.c_str(), &info) != 0) return std::unexpected(posix::call_failed("lstat", errno, path));
    return {};
}

[[nodiscard]] std::optional<std::string> info_plist_string(const NativePath& app_bundle, NSString* key) {
    @autoreleasepool {
        NSURL* plist = file_url(app_bundle / "Contents" / "Info.plist");
        NSError* error = nil;
        NSDictionary* info = [NSDictionary dictionaryWithContentsOfURL:plist error:&error];
        if (info == nil) return std::nullopt;
        id value = info[key];
        if (![value isKindOfClass:[NSString class]]) return std::nullopt;
        return utf8((NSString*)value);
    }
}

[[nodiscard]] SMAppService* agent(std::string_view plist_name) {
    return [SMAppService agentServiceWithPlistName:ns_string(plist_name)];
}

}  // namespace

Result<void> workspace_open(std::string_view url) {
    @autoreleasepool {
        NSString* text = ns_string(url);
        NSURL* target = text == nil ? nil : [NSURL URLWithString:text];
        if (target == nil) return std::unexpected(failed("NSURL URLWithString", nil));
        if (![[NSWorkspace sharedWorkspace] openURL:target]) return std::unexpected(failed("NSWorkspace openURL", nil));
        return {};
    }
}

Result<void> workspace_open_file(const NativePath& path) {
    if (auto exists = require_exists(path); !exists) return exists;
    @autoreleasepool {
        if (![[NSWorkspace sharedWorkspace] openURL:file_url(path)])
            return std::unexpected(failed_on("NSWorkspace openURL", path, nil));
        return {};
    }
}

Result<void> workspace_reveal(const NativePath& path) {
    if (auto exists = require_exists(path); !exists) return exists;
    @autoreleasepool {
        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[ file_url(path) ]];
        return {};
    }
}

Result<void> file_manager_trash(const NativePath& path) {
    if (auto exists = require_exists(path); !exists) return exists;
    @autoreleasepool {
        NSError* error = nil;
        if ([[NSFileManager defaultManager] trashItemAtURL:file_url(path) resultingItemURL:nil error:&error]) return {};
        // A volume without a trash: the item would have to be deleted for good, which trash never does.
        if ([error.domain isEqualToString:NSCocoaErrorDomain] && error.code == NSFeatureUnsupportedError)
            return make_diag(ErrorDomain::Platform, kTrashUnavailable).arg("path", path).kind(ErrorKind::Unsupported).fail();
        return std::unexpected(failed_on("NSFileManager trashItemAtURL", path, error));
    }
}

std::optional<NativePath> default_handler_bundle(std::string_view scheme) {
    @autoreleasepool {
        NSString* text = ns_string(std::string(scheme) + "://");
        NSURL* probe = text == nil ? nil : [NSURL URLWithString:text];
        if (probe == nil) return std::nullopt;
        NSURL* handler = [[NSWorkspace sharedWorkspace] URLForApplicationToOpenURL:probe];
        if (handler == nil || !handler.isFileURL) return std::nullopt;
        return path_of(handler);
    }
}

Result<void> register_bundle(const NativePath& app_bundle) {
    @autoreleasepool {
#pragma clang diagnostic push
// LaunchServices has no other call that registers a bundle on disk.
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        const OSStatus status = LSRegisterURL((__bridge CFURLRef)file_url(app_bundle), true);
#pragma clang diagnostic pop
        if (status != noErr)
            return make_diag(ErrorDomain::Platform, kCallFailedOnPath)
                .arg("call", "LSRegisterURL")
                .arg("path", app_bundle)
                .os(SystemError{SystemError::Origin::Host, status})
                .fail();
        return {};
    }
}

Result<void> set_default_handler(const NativePath& app_bundle, std::string_view scheme) {
    @autoreleasepool {
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        __block NSError* outcome = nil;
        [[NSWorkspace sharedWorkspace] setDefaultApplicationAtURL:file_url(app_bundle)
                                            toOpenURLsWithScheme:ns_string(scheme)
                                               completionHandler:^(NSError* error) {
                                                 outcome = error;
                                                 dispatch_semaphore_signal(done);
                                               }];
        const dispatch_time_t limit = dispatch_time(DISPATCH_TIME_NOW, kHandlerTimeoutSeconds * static_cast<int64_t>(NSEC_PER_SEC));
        if (dispatch_semaphore_wait(done, limit) != 0)
            return make_diag(ErrorDomain::Platform, kCallFailedOnPath)
                .arg("call", "NSWorkspace setDefaultApplicationAtURL")
                .arg("path", app_bundle)
                .os(posix::errno_error(ETIMEDOUT))
                .retryable()
                .fail();
        if (outcome != nil) return std::unexpected(failed_on("NSWorkspace setDefaultApplicationAtURL", app_bundle, outcome));
        return {};
    }
}

std::optional<std::string> bundle_identifier(const NativePath& app_bundle) {
    return info_plist_string(app_bundle, @"CFBundleIdentifier");
}

std::optional<std::string> bundle_version(const NativePath& app_bundle) {
    if (auto shown = info_plist_string(app_bundle, @"CFBundleShortVersionString")) return shown;
    return info_plist_string(app_bundle, @"CFBundleVersion");
}

AgentStatus agent_status(std::string_view plist_name) {
    @autoreleasepool {
        switch (agent(plist_name).status) {
            case SMAppServiceStatusNotRegistered:
                return AgentStatus::NotRegistered;
            case SMAppServiceStatusEnabled:
                return AgentStatus::Enabled;
            case SMAppServiceStatusRequiresApproval:
                return AgentStatus::RequiresApproval;
            case SMAppServiceStatusNotFound:
                return AgentStatus::NotFound;
        }
        return AgentStatus::NotFound;
    }
}

Result<void> agent_register(std::string_view plist_name) {
    @autoreleasepool {
        NSError* error = nil;
        if (![agent(plist_name) registerAndReturnError:&error])
            return std::unexpected(failed("SMAppService register", error));
        return {};
    }
}

Result<void> agent_unregister(std::string_view plist_name) {
    @autoreleasepool {
        NSError* error = nil;
        if (![agent(plist_name) unregisterAndReturnError:&error])
            return std::unexpected(failed("SMAppService unregister", error));
        return {};
    }
}

std::optional<VolumeKeys> volume_keys(const NativePath& mount) {
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithFileSystemRepresentation:mount.c_str() isDirectory:YES relativeToURL:nil];
        NSError* error = nil;
        NSDictionary<NSURLResourceKey, id>* values =
            [url resourceValuesForKeys:@[
                NSURLVolumeLocalizedNameKey, NSURLVolumeAvailableCapacityForImportantUsageKey, NSURLVolumeIsRemovableKey,
                NSURLVolumeIsEjectableKey, NSURLVolumeIsLocalKey
            ]
                                 error:&error];
        if (values == nil) return std::nullopt;
        VolumeKeys keys;
        if (NSString* name = values[NSURLVolumeLocalizedNameKey]) keys.localized_name = utf8(name);
        if (NSNumber* important = values[NSURLVolumeAvailableCapacityForImportantUsageKey]; important != nil && important.longLongValue >= 0)
            keys.important_free_bytes = static_cast<u64>(important.longLongValue);
        keys.removable = [values[NSURLVolumeIsRemovableKey] boolValue] || [values[NSURLVolumeIsEjectableKey] boolValue];
        NSNumber* local = values[NSURLVolumeIsLocalKey];
        keys.local = local == nil || local.boolValue;
        return keys;
    }
}

bool metal3_supported() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        return device != nil && [device supportsFamily:MTLGPUFamilyMetal3];
    }
}

}  // namespace reboot::os_macos::platform::shims
