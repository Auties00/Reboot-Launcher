#include "backbone/factory.hpp"

#include <stdexcept>

#include "backbone/inproc.hpp"
#if SB_WITH_NATS
#include "backbone/nats.hpp"
#endif

namespace sb::backbone {

std::unique_ptr<Backbone> make_backbone(const edge::BackboneConfig& cfg, u64 edge_id) {
    if (cfg.kind == "inproc") return std::make_unique<InprocBackbone>();
#if SB_WITH_NATS
    if (cfg.kind == "nats") return std::make_unique<NatsBackbone>(cfg, edge_id);
#else
    (void)edge_id;
#endif
    throw std::runtime_error("backbone kind not available in this build: " + cfg.kind);
}

}  // namespace sb::backbone
