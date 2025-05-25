#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

using NodeID = std::uint16_t;
const static NodeID UNDEFINED_NODE = std::numeric_limits<NodeID>::max();