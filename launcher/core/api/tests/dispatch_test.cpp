// Drives the generated dispatch through a handler set that records which member each id reached.
#include <catch2/catch_test_macros.hpp>

#include <any>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "messages.hpp"
#include "reboot/api/codec.hpp"
#include "reboot/api/decode_error.hpp"
#include "reboot/api/v1/backend.hpp"
#include "reboot/api/v1/browser.hpp"
#include "reboot/api/v1/catalog.hpp"
#include "reboot/api/v1/components.hpp"
#include "reboot/api/v1/dispatch.hpp"
#include "reboot/api/v1/engine.hpp"
#include "reboot/api/v1/guidance.hpp"
#include "reboot/api/v1/host.hpp"
#include "reboot/api/v1/identity.hpp"
#include "reboot/api/v1/install.hpp"
#include "reboot/api/v1/integration.hpp"
#include "reboot/api/v1/join.hpp"
#include "reboot/api/v1/library.hpp"
#include "reboot/api/v1/logs.hpp"
#include "reboot/api/v1/method_table.hpp"
#include "reboot/api/v1/play.hpp"
#include "reboot/api/v1/requests.hpp"
#include "reboot/api/v1/secrets.hpp"
#include "reboot/api/v1/sessions.hpp"
#include "reboot/api/v1/settings.hpp"
#include "reboot/api/v1/support.hpp"
#include "reboot/api/v1/updates.hpp"
#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/diag.hpp"

namespace api = reboot::api;
namespace msg = reboot::api::msg;
using reboot::DisconnectPolicy;
using reboot::OpHandle;
using reboot::Result;
using reboot::u32;
using reboot::u64;

// Every method of the schema: X_CALL(id, member, request, response) or X_OP(id, member, request).
#define REBOOT_API_METHODS(X_CALL, X_OP) \
    X_CALL(kEngineStatus, status, EngineStatusRequest, EngineStatusResponse) \
    X_CALL(kEngineInfo, info, EngineInfoRequest, EngineInfoResponse) \
    X_CALL(kEngineDrain, drain, EngineDrainRequest, EngineDrainResponse) \
    X_CALL(kEngineShutdown, shutdown, EngineShutdownRequest, EngineShutdownResponse) \
    X_CALL(kEngineRestartWhenIdle, restart_when_idle, EngineRestartWhenIdleRequest, EngineRestartWhenIdleResponse) \
    X_CALL(kEngineOperations, operations, EngineOperationsRequest, EngineOperationsResponse) \
    X_CALL(kSettingsSnapshot, snapshot, SettingsSnapshotRequest, SettingsSnapshotResponse) \
    X_CALL(kSettingsPatch, patch, SettingsPatchRequest, SettingsPatchResponse) \
    X_CALL(kSettingsReset, reset, SettingsResetRequest, SettingsResetResponse) \
    X_CALL(kSettingsSearch, search, SettingsSearchRequest, SettingsSearchResponse) \
    X_CALL(kSettingsFrontendStateGet, frontend_state_get, SettingsFrontendStateGetRequest, SettingsFrontendStateGetResponse) \
    X_CALL(kSettingsFrontendStatePut, frontend_state_put, SettingsFrontendStatePutRequest, SettingsFrontendStatePutResponse) \
    X_CALL(kSettingsLanguages, languages, SettingsLanguagesRequest, SettingsLanguagesResponse) \
    X_CALL(kSettingsDescriptors, descriptors, SettingsDescriptorsRequest, SettingsDescriptorsResponse) \
    X_CALL(kSettingsConsoleKeys, console_keys, SettingsConsoleKeysRequest, SettingsConsoleKeysResponse) \
    X_CALL(kSettingsConsoleKeyFromNative, console_key_from_native, SettingsConsoleKeyFromNativeRequest, SettingsConsoleKeyFromNativeResponse) \
    X_CALL(kLibraryList, list, LibraryListRequest, LibraryListResponse) \
    X_CALL(kLibraryGet, get, LibraryGetRequest, LibraryGetResponse) \
    X_CALL(kLibrarySelect, select, LibrarySelectRequest, LibrarySelectResponse) \
    X_CALL(kLibraryUpdate, update, LibraryUpdateRequest, LibraryUpdateResponse) \
    X_OP(kLibraryRelocate, start_relocate, LibraryRelocateRequest) \
    X_OP(kLibraryImport, start_import, LibraryImportRequest) \
    X_OP(kLibraryRemove, start_remove, LibraryRemoveRequest) \
    X_CALL(kCatalogList, list, CatalogListRequest, CatalogListResponse) \
    X_OP(kCatalogRefresh, start_refresh, CatalogRefreshRequest) \
    X_OP(kInstallSuggestDestination, start_suggest_destination, InstallSuggestDestinationRequest) \
    X_OP(kInstallInstall, start_install, InstallInstallRequest) \
    X_OP(kInstallDiscardStaging, start_discard_staging, InstallDiscardStagingRequest) \
    X_OP(kInstallDeleteUnregistered, start_delete_unregistered, InstallDeleteUnregisteredRequest) \
    X_CALL(kSupportQuery, query, SupportQueryRequest, SupportQueryResponse) \
    X_CALL(kComponentsList, list, ComponentsListRequest, ComponentsListResponse) \
    X_OP(kComponentsEnsure, start_ensure, ComponentsEnsureRequest) \
    X_OP(kComponentsRemove, start_remove, ComponentsRemoveRequest) \
    X_OP(kComponentsRuntimeSetup, start_runtime_setup, ComponentsRuntimeSetupRequest) \
    X_CALL(kIdentityGet, get, IdentityGetRequest, IdentityGetResponse) \
    X_CALL(kIdentitySetDisplayName, set_display_name, IdentitySetDisplayNameRequest, IdentitySetDisplayNameResponse) \
    X_CALL(kIdentityReset, reset, IdentityResetRequest, IdentityResetResponse) \
    X_CALL(kSecretsState, state, SecretsStateRequest, SecretsStateResponse) \
    X_CALL(kSecretsClear, clear, SecretsClearRequest, SecretsClearResponse) \
    X_CALL(kPlayPlan, plan, PlayPlanRequest, PlayPlanResponse) \
    X_OP(kPlayStart, start, PlayStartRequest) \
    X_CALL(kHostProfilesList, profiles_list, HostProfilesListRequest, HostProfilesListResponse) \
    X_CALL(kHostProfilesCreate, profiles_create, HostProfilesCreateRequest, HostProfilesCreateResponse) \
    X_CALL(kHostProfilesUpdate, profiles_update, HostProfilesUpdateRequest, HostProfilesUpdateResponse) \
    X_CALL(kHostProfilesDelete, profiles_delete, HostProfilesDeleteRequest, HostProfilesDeleteResponse) \
    X_CALL(kHostShareLink, share_link, HostShareLinkRequest, HostShareLinkResponse) \
    X_CALL(kHostCommand, command, HostCommandRequest, HostCommandResponse) \
    X_OP(kHostStart, start, HostStartRequest) \
    X_CALL(kHostStatus, status, HostStatusRequest, HostStatusResponse) \
    X_CALL(kHostCancelMatchEnd, cancel_match_end, HostCancelMatchEndRequest, HostCancelMatchEndResponse) \
    X_OP(kHostIdentityExport, start_identity_export, HostIdentityExportRequest) \
    X_OP(kHostIdentityImport, start_identity_import, HostIdentityImportRequest) \
    X_CALL(kSessionsList, list, SessionsListRequest, SessionsListResponse) \
    X_CALL(kSessionsGet, get, SessionsGetRequest, SessionsGetResponse) \
    X_CALL(kSessionsSetLease, set_lease, SessionsSetLeaseRequest, SessionsSetLeaseResponse) \
    X_OP(kSessionsStop, start_stop, SessionsStopRequest) \
    X_CALL(kBackendStatus, status, BackendStatusRequest, BackendStatusResponse) \
    X_CALL(kBackendDataDir, data_dir, BackendDataDirRequest, BackendDataDirResponse) \
    X_CALL(kBackendSetTarget, set_target, BackendSetTargetRequest, BackendSetTargetResponse) \
    X_CALL(kBackendAccountsList, accounts_list, BackendAccountsListRequest, BackendAccountsListResponse) \
    X_OP(kBackendStart, start, BackendStartRequest) \
    X_OP(kBackendStop, start_stop, BackendStopRequest) \
    X_OP(kBackendAccountsReset, start_accounts_reset, BackendAccountsResetRequest) \
    X_OP(kBackendAccountsDelete, start_accounts_delete, BackendAccountsDeleteRequest) \
    X_OP(kBackendAccountsPrune, start_accounts_prune, BackendAccountsPruneRequest) \
    X_OP(kBackendAccountsRename, start_accounts_rename, BackendAccountsRenameRequest) \
    X_CALL(kBrowserOpenView, open_view, BrowserOpenViewRequest, BrowserOpenViewResponse) \
    X_CALL(kBrowserUpdateView, update_view, BrowserUpdateViewRequest, BrowserUpdateViewResponse) \
    X_CALL(kBrowserCloseView, close_view, BrowserCloseViewRequest, BrowserCloseViewResponse) \
    X_CALL(kBrowserState, state, BrowserStateRequest, BrowserStateResponse) \
    X_OP(kBrowserResolve, start_resolve, BrowserResolveRequest) \
    X_CALL(kJoinParseAddress, parse_address, JoinParseAddressRequest, JoinParseAddressResponse) \
    X_CALL(kJoinTarget, target, JoinTargetRequest, JoinTargetResponse) \
    X_CALL(kJoinSetCustomTarget, set_custom_target, JoinSetCustomTargetRequest, JoinSetCustomTargetResponse) \
    X_CALL(kJoinClearTarget, clear_target, JoinClearTargetRequest, JoinClearTargetResponse) \
    X_OP(kJoinResolveLink, start_resolve_link, JoinResolveLinkRequest) \
    X_OP(kJoinGrant, start_grant, JoinGrantRequest) \
    X_CALL(kUpdatesStatus, status, UpdatesStatusRequest, UpdatesStatusResponse) \
    X_OP(kUpdatesCheck, start_check, UpdatesCheckRequest) \
    X_OP(kUpdatesApply, start_apply, UpdatesApplyRequest) \
    X_CALL(kIntegrationStatus, status, IntegrationStatusRequest, IntegrationStatusResponse) \
    X_CALL(kIntegrationPrerequisites, prerequisites, IntegrationPrerequisitesRequest, IntegrationPrerequisitesResponse) \
    X_CALL(kIntegrationShellOpenUrl, shell_open_url, IntegrationShellOpenUrlRequest, IntegrationShellOpenUrlResponse) \
    X_CALL(kIntegrationShellOpenPath, shell_open_path, IntegrationShellOpenPathRequest, IntegrationShellOpenPathResponse) \
    X_CALL(kIntegrationShellReveal, shell_reveal, IntegrationShellRevealRequest, IntegrationShellRevealResponse) \
    X_OP(kIntegrationApply, start_apply, IntegrationApplyRequest) \
    X_OP(kIntegrationRemove, start_remove, IntegrationRemoveRequest) \
    X_OP(kIntegrationRemediate, start_remediate, IntegrationRemediateRequest) \
    X_OP(kIntegrationPurge, start_purge, IntegrationPurgeRequest) \
    X_CALL(kGuidanceNoticesList, notices_list, GuidanceNoticesListRequest, GuidanceNoticesListResponse) \
    X_CALL(kGuidanceNoticesDismiss, notices_dismiss, GuidanceNoticesDismissRequest, GuidanceNoticesDismissResponse) \
    X_CALL(kGuidanceOnboardingState, onboarding_state, GuidanceOnboardingStateRequest, GuidanceOnboardingStateResponse) \
    X_CALL(kGuidanceOnboardingAdvance, onboarding_advance, GuidanceOnboardingAdvanceRequest, GuidanceOnboardingAdvanceResponse) \
    X_CALL(kGuidanceOnboardingSkip, onboarding_skip, GuidanceOnboardingSkipRequest, GuidanceOnboardingSkipResponse) \
    X_CALL(kGuidanceLinks, links, GuidanceLinksRequest, GuidanceLinksResponse) \
    X_CALL(kLogsRead, read, LogsReadRequest, LogsReadResponse) \
    X_OP(kLogsExport, start_export, LogsExportRequest) \
    X_CALL(kRequestsPending, pending, RequestsPendingRequest, RequestsPendingResponse) \
    X_CALL(kRequestsRespond, respond, RequestsRespondRequest, RequestsRespondResponse)

namespace {

struct Invocation {
    u32 method_id = 0;
    reboot::ConnectionId connection;
    std::optional<DisconnectPolicy> disconnect;
    std::any request;
};

class RecordingHandlers final : public api::IBackendHandler,
                                public api::IBrowserHandler,
                                public api::ICatalogHandler,
                                public api::IComponentsHandler,
                                public api::IEngineHandler,
                                public api::IGuidanceHandler,
                                public api::IHostHandler,
                                public api::IIdentityHandler,
                                public api::IInstallHandler,
                                public api::IIntegrationHandler,
                                public api::IJoinHandler,
                                public api::ILibraryHandler,
                                public api::ILogsHandler,
                                public api::IPlayHandler,
                                public api::IRequestsHandler,
                                public api::ISecretsHandler,
                                public api::ISessionsHandler,
                                public api::ISettingsHandler,
                                public api::ISupportHandler,
                                public api::IUpdatesHandler {
public:
    std::vector<Invocation> invocations;
    std::optional<reboot::Diagnostic> fail_with;
    // Returned by a call whose response type matches; otherwise the response is empty.
    std::any response;

    [[nodiscard]] api::Handlers handlers() {
        return {*this, *this, *this, *this, *this, *this, *this, *this, *this, *this,
                *this, *this, *this, *this, *this, *this, *this, *this, *this, *this};
    }

#define RECORD_CALL(id, member, Request, Response)                                                  \
    Result<api::Response> member(const api::CallContext& context, const api::Request& request) override { \
        invocations.push_back({api::id, context.connection, std::nullopt, request});                 \
        if (fail_with) return std::unexpected(*fail_with);                                           \
        if (const auto* canned = std::any_cast<api::Response>(&response)) return *canned;           \
        return api::Response{};                                                                      \
    }
#define RECORD_OP(id, member, Request)                                                               \
    Result<OpHandle> member(const api::CallContext& context, const api::Request& request,           \
                            DisconnectPolicy disconnect) override {                                  \
        invocations.push_back({api::id, context.connection, disconnect, request});                   \
        if (fail_with) return std::unexpected(*fail_with);                                           \
        return OpHandle(reboot::OpId{static_cast<u64>(invocations.size())});                       \
    }
    REBOOT_API_METHODS(RECORD_CALL, RECORD_OP)
#undef RECORD_CALL
#undef RECORD_OP
};

// Sets one alternative of each choice a request must carry; other requests stay empty.
template <class T>
void choose_cases(T&) {}

void choose_cases(api::HostProfile& profile) {
    profile.port.pinned = 7777;
    profile.start.manual = true;
}
void choose_cases(api::HostProfilesCreateRequest& request) { choose_cases(request.profile); }
void choose_cases(api::HostProfilesUpdateRequest& request) { choose_cases(request.profile); }
void choose_cases(api::HostCommandRequest& request) { request.command.reset = api::ResetMatch{}; }
void choose_cases(api::RequestsRespondRequest& request) { request.answer.secret_provided = true; }
void choose_cases(api::SecretsStateRequest& request) { request.target.join_request = 1; }
void choose_cases(api::SecretsClearRequest& request) { request.target.join_request = 1; }
void choose_cases(api::SupportQueryRequest& request) { request.build = api::BuildId{}; }

template <class T>
api::Bytes minimal_request() {
    T request{};
    choose_cases(request);
    return api::encode(request);
}

struct Sample {
    u32 method_id;
    api::Bytes request;
    // What a call answering with a default response sends back; empty for operations.
    api::Bytes empty_response;
};

#define SAMPLE(id, Request, response) Sample{api::id, minimal_request<api::Request>(), response},
#define SAMPLE_CALL(id, member, Request, Response) SAMPLE(id, Request, api::encode(api::Response{}))
#define SAMPLE_OP(id, member, Request) SAMPLE(id, Request, api::Bytes{})

std::vector<Sample> minimal_requests() {
    return {REBOOT_API_METHODS(SAMPLE_CALL, SAMPLE_OP)};
}

#undef SAMPLE
#undef SAMPLE_CALL
#undef SAMPLE_OP

api::Bytes golden(const std::string& name) {
    std::ifstream stream(std::string(REBOOT_API_TEST_DATA) + "/" + name + ".bin", std::ios::binary);
    REQUIRE(stream);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

struct Fixture {
    reboot::contracts::ipc::CallerContext caller{"console", false, {}};
    api::CallContext context{reboot::ConnectionId{17}, reboot::contracts::ipc::ClientKind::Test, caller};
    RecordingHandlers recorder;
    api::Handlers handlers = recorder.handlers();
};

void check_api_error(const reboot::Diagnostic& diag, reboot::MessageId message, u32 method_id) {
    CHECK(diag.is(message));
    CHECK(diag.domain == reboot::ErrorDomain::Api);
    CHECK(diag.kind == reboot::ErrorKind::InvalidInput);
    const reboot::Arg* method = diag.find_arg("method");
    REQUIRE(method);
    const auto* value = std::get_if<u64>(method);
    REQUIRE(value);
    CHECK(*value == method_id);
}

}  // namespace

TEST_CASE("every method of the table has a route", "[dispatch]") {
    const std::vector<Sample> samples = minimal_requests();
    REQUIRE(samples.size() == api::MethodTable::all().size());
    for (std::size_t i = 0; i < samples.size(); ++i) CHECK(samples[i].method_id == api::MethodTable::all()[i].id);
}

TEST_CASE("an empty request missing a required choice is an unknown case", "[dispatch]") {
    Fixture fixture;
    const auto call = api::dispatch_call(fixture.handlers, fixture.context, api::kSupportQuery,
                                         api::encode(api::SupportQueryRequest{}));
    REQUIRE_FALSE(call);
    check_api_error(call.error(), msg::kUnknownCase, api::kSupportQuery);

    const auto respond = api::dispatch_call(fixture.handlers, fixture.context, api::kRequestsRespond,
                                            api::encode(api::RequestsRespondRequest{}));
    REQUIRE_FALSE(respond);
    check_api_error(respond.error(), msg::kUnknownCase, api::kRequestsRespond);
    CHECK(fixture.recorder.invocations.empty());
}

TEST_CASE("a call reaches its own handler member with the caller's connection", "[dispatch]") {
    for (const Sample& sample : minimal_requests()) {
        const api::MethodSpec* spec = api::MethodTable::find(sample.method_id);
        REQUIRE(spec);
        if (spec->kind != api::MethodKind::Call) continue;
        Fixture fixture;
        const auto response = api::dispatch_call(fixture.handlers, fixture.context, sample.method_id, sample.request);
        INFO(spec->name);
        REQUIRE(response);
        CHECK(*response == sample.empty_response);
        REQUIRE(fixture.recorder.invocations.size() == 1);
        const Invocation& invocation = fixture.recorder.invocations.front();
        CHECK(invocation.method_id == sample.method_id);
        CHECK(invocation.connection == fixture.context.connection);
        CHECK_FALSE(invocation.disconnect);
    }
}

TEST_CASE("an operation starts on its own handler member with its default disconnect policy", "[dispatch]") {
    for (const Sample& sample : minimal_requests()) {
        const api::MethodSpec* spec = api::MethodTable::find(sample.method_id);
        REQUIRE(spec);
        if (spec->kind != api::MethodKind::Operation) continue;
        Fixture fixture;
        const auto handle =
            api::dispatch_start(fixture.handlers, fixture.context, sample.method_id, sample.request, std::nullopt);
        INFO(spec->name);
        REQUIRE(handle);
        CHECK(handle->id() == reboot::OpId{1});
        REQUIRE(fixture.recorder.invocations.size() == 1);
        const Invocation& invocation = fixture.recorder.invocations.front();
        CHECK(invocation.method_id == sample.method_id);
        CHECK(invocation.connection == fixture.context.connection);
        CHECK(invocation.disconnect == spec->default_disconnect);
    }
}

TEST_CASE("an explicit disconnect policy overrides the method's default", "[dispatch]") {
    Fixture fixture;
    const api::Bytes request = api::encode(api::InstallInstallRequest{});
    REQUIRE(api::dispatch_start(fixture.handlers, fixture.context, api::kInstallInstall, request,
                                DisconnectPolicy::BoundToConnection));
    REQUIRE(api::dispatch_start(fixture.handlers, fixture.context, api::kLogsExport,
                                api::encode(api::LogsExportRequest{}), DisconnectPolicy::Detached));
    REQUIRE(fixture.recorder.invocations.size() == 2);
    CHECK(fixture.recorder.invocations[0].disconnect == DisconnectPolicy::BoundToConnection);
    CHECK(fixture.recorder.invocations[1].disconnect == DisconnectPolicy::Detached);
}

TEST_CASE("a method used as the wrong kind is refused before decoding", "[dispatch]") {
    Fixture fixture;
    const api::Bytes garbage{0xFF, 0xFF, 0xFF};
    const auto call = api::dispatch_call(fixture.handlers, fixture.context, api::kPlayStart, garbage);
    REQUIRE_FALSE(call);
    check_api_error(call.error(), msg::kWrongMethodKind, api::kPlayStart);

    const auto start = api::dispatch_start(fixture.handlers, fixture.context, api::kPlayPlan, garbage, std::nullopt);
    REQUIRE_FALSE(start);
    check_api_error(start.error(), msg::kWrongMethodKind, api::kPlayPlan);
    CHECK(fixture.recorder.invocations.empty());
}

TEST_CASE("an id outside the table is an unknown method", "[dispatch]") {
    Fixture fixture;
    constexpr u32 unknown = 0x00110001;
    REQUIRE_FALSE(api::MethodTable::find(unknown));
    const auto call = api::dispatch_call(fixture.handlers, fixture.context, unknown, {});
    REQUIRE_FALSE(call);
    check_api_error(call.error(), msg::kUnknownMethod, unknown);

    const auto start = api::dispatch_start(fixture.handlers, fixture.context, unknown, {}, DisconnectPolicy::Detached);
    REQUIRE_FALSE(start);
    check_api_error(start.error(), msg::kUnknownMethod, unknown);
    CHECK(fixture.recorder.invocations.empty());
}

TEST_CASE("an undecodable request never reaches the handler", "[dispatch]") {
    Fixture fixture;
    const api::Bytes truncated{0x0A, 0x05, 0x01};
    const auto call = api::dispatch_call(fixture.handlers, fixture.context, api::kSessionsGet, truncated);
    REQUIRE_FALSE(call);
    check_api_error(call.error(), msg::kMalformedRequest, api::kSessionsGet);

    const auto start = api::dispatch_start(fixture.handlers, fixture.context, api::kSessionsStop, truncated, std::nullopt);
    REQUIRE_FALSE(start);
    check_api_error(start.error(), msg::kMalformedRequest, api::kSessionsStop);
    CHECK(fixture.recorder.invocations.empty());
}

TEST_CASE("a request setting two alternatives of a choice is refused", "[dispatch]") {
    Fixture fixture;
    api::PlayStartRequest start;
    start.request.target = api::PlayTarget{.server = api::ServerId{}, .address = "127.0.0.1:7777"};
    const auto handle =
        api::dispatch_start(fixture.handlers, fixture.context, api::kPlayStart, api::encode(start), std::nullopt);
    REQUIRE_FALSE(handle);
    check_api_error(handle.error(), msg::kConflictingCases, api::kPlayStart);
    CHECK(fixture.recorder.invocations.empty());
}

TEST_CASE("a repeated element setting two alternatives of a choice is refused", "[dispatch]") {
    Fixture fixture;
    api::SettingsPatchRequest patch;
    patch.changes.push_back({"host.listing", {.text = "listed"}, false});
    patch.changes.push_back({"host.max_players", {.flag = true, .integer = 3}, false});
    const auto call = api::dispatch_call(fixture.handlers, fixture.context, api::kSettingsPatch, api::encode(patch));
    REQUIRE_FALSE(call);
    check_api_error(call.error(), msg::kConflictingCases, api::kSettingsPatch);
    CHECK(fixture.recorder.invocations.empty());
}

TEST_CASE("the handler receives the request exactly as encoded", "[dispatch]") {
    Fixture fixture;
    const api::Bytes bytes = golden("play_start_request");
    const auto handle = api::dispatch_start(fixture.handlers, fixture.context, api::kPlayStart, bytes, std::nullopt);
    REQUIRE(handle);
    REQUIRE(fixture.recorder.invocations.size() == 1);
    const auto* request = std::any_cast<api::PlayStartRequest>(&fixture.recorder.invocations.front().request);
    REQUIRE(request);
    CHECK(api::encode(*request) == bytes);
    CHECK(request->request.custom_args == "-log \"-name=a b\"");

    const api::Bytes patch = golden("settings_patch_request");
    REQUIRE(api::dispatch_call(fixture.handlers, fixture.context, api::kSettingsPatch, patch));
    REQUIRE(fixture.recorder.invocations.size() == 2);
    const auto* decoded = std::any_cast<api::SettingsPatchRequest>(&fixture.recorder.invocations.back().request);
    REQUIRE(decoded);
    CHECK(decoded->expected_revision == 12);
    CHECK(api::encode(*decoded) == patch);
}

TEST_CASE("a call's response is encoded as its message", "[dispatch]") {
    Fixture fixture;
    api::SecretsStateResponse response;
    response.present = true;
    fixture.recorder.response = response;
    const auto bytes = api::dispatch_call(fixture.handlers, fixture.context, api::kSecretsState,
                                          golden("secrets_state_request"));
    REQUIRE(bytes);
    CHECK(*bytes == api::encode(response));
    const auto decoded = api::decode<api::SecretsStateResponse>(*bytes);
    REQUIRE(decoded);
    CHECK(*decoded == response);
}

TEST_CASE("a handler's diagnostic passes through unchanged", "[dispatch]") {
    Fixture fixture;
    const reboot::Diagnostic busy = reboot::make_diag(reboot::ErrorDomain::Api, msg::kUnknownCase)
                                        .arg("method", 1u)
                                        .kind(reboot::ErrorKind::Conflict)
                                        .retryable(true)
                                        .detail("busy");
    fixture.recorder.fail_with = busy;

    const auto call = api::dispatch_call(fixture.handlers, fixture.context, api::kSessionsList,
                                         api::encode(api::SessionsListRequest{}));
    REQUIRE_FALSE(call);
    CHECK(call.error().kind == reboot::ErrorKind::Conflict);
    CHECK(call.error().retryable);
    CHECK(call.error().detail == "busy");

    const auto start = api::dispatch_start(fixture.handlers, fixture.context, api::kHostStart,
                                           api::encode(api::HostStartRequest{}),
                                           std::nullopt);
    REQUIRE_FALSE(start);
    CHECK(start.error().kind == reboot::ErrorKind::Conflict);
    CHECK(start.error().detail == "busy");
    CHECK(fixture.recorder.invocations.size() == 2);
}

TEST_CASE("an Operation<void> completes a method whose response has no fields", "[dispatch]") {
    const auto stopped = api::encode_op_result(api::kSessionsStop, std::any{});
    REQUIRE(stopped);
    CHECK(stopped->empty());
    CHECK_FALSE(api::encode_op_result(api::kPlayStart, std::any{}));
}

TEST_CASE("an operation's response encodes as its message", "[dispatch]") {
    api::PlayStartResponse response;
    response.session.uuid.bytes[0] = 7;
    const auto bytes = api::encode_op_result(api::kPlayStart, std::any{response});
    REQUIRE(bytes);
    CHECK(*bytes == api::encode(response));
}

TEST_CASE("an operation completing with another method's response is a bug", "[dispatch]") {
    const auto mismatched = api::encode_op_result(api::kPlayStart, std::any{api::HostStartResponse{}});
    REQUIRE_FALSE(mismatched);
    CHECK(mismatched.error().domain == reboot::ErrorDomain::Internal);
}

TEST_CASE("an op result for a call or an unknown id is the engine's bug", "[dispatch]") {
    const auto call = api::encode_op_result(api::kPlayPlan, std::any{api::PlayPlanResponse{}});
    REQUIRE_FALSE(call);
    CHECK(call.error().domain == reboot::ErrorDomain::Internal);

    const auto unknown = api::encode_op_result(0, std::any{});
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().domain == reboot::ErrorDomain::Internal);
}

TEST_CASE("each decode error maps to its own message", "[dispatch]") {
    check_api_error(api::to_diagnostic(api::DecodeError::Malformed, 9), msg::kMalformedRequest, 9);
    check_api_error(api::to_diagnostic(api::DecodeError::ConflictingCases, 9), msg::kConflictingCases, 9);
    check_api_error(api::to_diagnostic(api::DecodeError::UnknownCase, 9), msg::kUnknownCase, 9);
}
