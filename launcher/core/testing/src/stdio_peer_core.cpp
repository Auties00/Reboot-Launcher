#include "stdio_peer_core.hpp"

#include <algorithm>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace rb::testing {
namespace {

// A tag for field 1 with a length but no length bytes: no message decodes it.
constexpr std::array<u8, 1> kGarbagePayload{0x0A};

}  // namespace

StdioPeerCore::StdioPeerCore(Executor& executor, const IClock& clock, ChildMisbehaviour misbehaviour)
    : executor_(executor), clock_(clock), misbehaviour_(std::move(misbehaviour)) {}

StdioPeerCore::~StdioPeerCore() = default;

void StdioPeerCore::start(StdioPeerOutputs outputs) {
    outputs_ = std::move(outputs);
    for (const std::string& line : misbehaviour_.stderr_lines) write_stderr(line);
    reached(ScriptStage::Start);
    after(misbehaviour_.hello_delay, [this] {
        if (misbehaviour_.send_hello) send_hello();
        if (misbehaviour_.garbage_frame) {
            sb::wire::Writer writer;
            writer.quic_varint(*misbehaviour_.garbage_frame);
            writer.quic_varint(kGarbagePayload.size());
            std::vector<u8> frame = writer.take();
            frame.insert(frame.end(), kGarbagePayload.begin(), kGarbagePayload.end());
            write(frame);
        }
    });
}

void StdioPeerCore::on_stdin(std::span<const u8> bytes) {
    if (finished_) return;
    std::vector<OwnedFrame> frames;
    const Framer::Status status = framer_.feed(bytes, [&](const RawFrame& frame) {
        frames.push_back({frame.type, {frame.payload.begin(), frame.payload.end()}});
        return true;
    });
    for (const OwnedFrame& frame : frames) {
        if (finished_) return;
        if (frame.type == contract_frame_type_v<contracts::common::Ping>) {
            if (hung_) continue;
            if (auto ping = decode_contract<contracts::common::Ping>(frame.payload)) send(contracts::common::Pong{ping->nonce});
            continue;
        }
        if (hung_) continue;
        if (std::ranges::contains(misbehaviour_.unsupported, frame.type)) {
            const u64 req_id = first_req_id(frame.payload);
            reply([this, req_id] { unsupported(req_id); });
            continue;
        }
        handle(frame);
    }
    // A child that cannot parse its control stream has nothing sane left to do.
    if (status != Framer::Status::ok) finish(kBadInvocationExitCode);
}

void StdioPeerCore::on_stdin_eof() {
    if (!misbehaviour_.ignore_stdin_eof) finish(0);
}

void StdioPeerCore::write(std::span<const u8> bytes) {
    if (finished_ || !outputs_.stdout_bytes) return;
    outputs_.stdout_bytes(bytes);
}

void StdioPeerCore::write_stderr(std::string_view line) {
    if (finished_ || !outputs_.stderr_line) return;
    outputs_.stderr_line(line);
}

void StdioPeerCore::after(std::chrono::milliseconds delay, UniqueFunction<void()> step) {
    auto guarded = [weak = weak_from_this(), step = std::move(step)]() mutable {
        if (const auto self = weak.lock(); self && !self->finished_) step();
    };
    if (delay <= std::chrono::milliseconds::zero()) {
        executor_.post(std::move(guarded));
    } else {
        executor_.post_at(clock_.steady_now() + delay, std::move(guarded));
    }
}

void StdioPeerCore::reply(UniqueFunction<void()> step) {
    if (misbehaviour_.reply_delay <= std::chrono::milliseconds::zero()) {
        if (!hung_ && !finished_) step();
        return;
    }
    after(misbehaviour_.reply_delay, [this, step = std::move(step)]() mutable {
        if (!hung_) step();
    });
}

void StdioPeerCore::ok(u64 req_id) { send(contracts::common::CommandResult{req_id, true, std::nullopt}); }

void StdioPeerCore::refuse(u64 req_id, const Diagnostic& error) {
    send(contracts::common::CommandResult{req_id, false, contracts::common::to_wire(error)});
}

void StdioPeerCore::unsupported(u64 req_id) { send(contracts::common::Unsupported{req_id}); }

void StdioPeerCore::reached(ScriptStage stage) {
    if (misbehaviour_.crash && misbehaviour_.crash->stage == stage)
        after(misbehaviour_.crash->after, [this] { finish(misbehaviour_.crash_exit_code); });
    if (misbehaviour_.hang && misbehaviour_.hang->stage == stage) after(misbehaviour_.hang->after, [this] { hung_ = true; });
}

void StdioPeerCore::finish(int exit_code) {
    if (finished_) return;
    finished_ = true;
    on_finished();
    if (outputs_.exit) outputs_.exit(exit_code);
}

u64 first_req_id(std::span<const u8> payload) {
    // Field 1, wire type varint.
    if (payload.empty() || payload[0] != 0x08) return 0;
    u64 value = 0;
    for (std::size_t i = 1, shift = 0; i < payload.size() && shift < 64; ++i, shift += 7) {
        value |= u64{payload[i] & 0x7Fu} << shift;
        if ((payload[i] & 0x80) == 0) return value;
    }
    return 0;
}

}  // namespace rb::testing
