#include <cstddef>
#include <cstdint>
#include <span>

#include "frame_fuzz.hpp"
#include "reboot/contracts/common.hpp"
#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/framing.hpp"

namespace common = reboot::contracts::common;
namespace ipc = reboot::contracts::ipc;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    reboot::testing::fuzz_frames<common::Ping, common::Pong, ipc::Hello, ipc::Call, ipc::Start, ipc::Cancel,
                                 ipc::Attach, ipc::Release, ipc::Subscribe, ipc::Unsubscribe, ipc::Credit,
                                 ipc::SecretPut, ipc::SecretReveal, ipc::LogWrite, ipc::Goodbye, ipc::HelloAck,
                                 ipc::Reply, ipc::Started, ipc::OpResult, ipc::EventBatch, ipc::Resync,
                                 ipc::ForegroundHint>(std::span<const reboot::u8>(data, size), reboot::kIpcFrameCap);
    return 0;
}
