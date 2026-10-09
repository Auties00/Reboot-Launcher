#include "reboot/game_channel/legacy_output_adapter.hpp"

#include <array>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "reboot/foundation/log.hpp"
#include "reboot/process/line_reader.hpp"

namespace reboot::game_channel {

namespace {

constexpr std::size_t kSourceCount = 3;
constexpr std::size_t kMarkerCount = 5;

[[nodiscard]] GameLifecycleEvent event_for(LegacyMarker marker) {
    switch (marker) {
        case LegacyMarker::LoginCompleted: return LoggedIn{};
        case LegacyMarker::Shutdown: return ExitRequested{contracts::game_client::ExitKind::RequestExit, 0, {}};
        case LegacyMarker::CorruptBuild: return SessionFatal{FatalCause::CorruptBuild, {}};
        case LegacyMarker::AuthFailure: return SessionFatal{FatalCause::AuthFailure, {}};
        case LegacyMarker::CannotConnect: return SessionFatal{FatalCause::CannotConnect, {}};
    }
    return SessionFatal{FatalCause::CorruptBuild, {}};
}

[[nodiscard]] constexpr bool ends_session(LegacyMarker marker) noexcept { return marker != LegacyMarker::LoginCompleted; }

}  // namespace

struct LegacyOutputAdapter::Impl {
    Impl(SessionId session_id, const LifecycleMarkers& marker_set, UniqueFunction<void(GameLifecycleEvent)> handler)
        : session(session_id),
          markers(marker_set),
          on_event(std::move(handler)),
          readers{reader_for(OutputSource::Stdout), reader_for(OutputSource::Stderr), reader_for(OutputSource::UeLog)} {}

    process::LineReader reader_for(OutputSource source) {
        return process::LineReader([this, source](std::string_view line, bool continued) { on_line(source, line, continued); });
    }

    void on_line(OutputSource source, std::string_view line, bool continued) {
        // A line cut at the reader's cap is matched only by its first part.
        const bool fresh = !cut[static_cast<std::size_t>(source)];
        cut[static_cast<std::size_t>(source)] = continued;
        if (source != OutputSource::UeLog)
            REBOOT_LOG_AT(LogLevel::Info, GameOutput, session, "{}: {}", source == OutputSource::Stdout ? "stdout" : "stderr",
                          line);
        if (ended || !fresh) return;
        const std::optional<LegacyMarker> marker = markers.match(line);
        if (!marker) return;
        const auto index = static_cast<std::size_t>(*marker);
        // stdout and the UE log carry the same lines, so each marker counts once.
        if (index >= kMarkerCount || emitted[index]) return;
        emitted[index] = true;
        ended = ends_session(*marker);
        pending.push_back(event_for(*marker));
    }

    // Outside the LineReader, since a handler may end the session and destroy this adapter.
    void flush() {
        std::vector<GameLifecycleEvent> events = std::exchange(pending, {});
        const std::weak_ptr<void> guard = alive;
        for (GameLifecycleEvent& event : events) {
            UniqueFunction<void(GameLifecycleEvent)> handler = std::move(on_event);
            handler(std::move(event));
            if (guard.expired()) return;
            if (!on_event) on_event = std::move(handler);
        }
    }

    SessionId session;
    LifecycleMarkers markers;
    UniqueFunction<void(GameLifecycleEvent)> on_event;
    std::array<process::LineReader, kSourceCount> readers;
    std::array<bool, kSourceCount> cut{};
    std::array<bool, kMarkerCount> emitted{};
    bool ended = false;
    std::vector<GameLifecycleEvent> pending;
    std::shared_ptr<int> alive = std::make_shared<int>(0);
};

LegacyOutputAdapter::LegacyOutputAdapter(SessionId session, const LifecycleMarkers& markers,
                                         UniqueFunction<void(GameLifecycleEvent)> on_event)
    : impl_(std::make_unique<Impl>(session, markers, std::move(on_event))) {}

LegacyOutputAdapter::~LegacyOutputAdapter() = default;

void LegacyOutputAdapter::feed(OutputSource source, std::span<const u8> bytes) {
    impl_->readers[static_cast<std::size_t>(source)].feed(bytes);
    impl_->flush();
}

void LegacyOutputAdapter::finish(OutputSource source) {
    impl_->readers[static_cast<std::size_t>(source)].finish();
    impl_->flush();
}

}  // namespace reboot::game_channel
