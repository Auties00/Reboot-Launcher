#pragma once

#include <memory>

#include "backbone/backbone.hpp"
#include "edge/config.hpp"

namespace sb::backbone {

[[nodiscard]] std::unique_ptr<Backbone> make_backbone(const edge::BackboneConfig& cfg, u64 edge_id);

}  // namespace sb::backbone
