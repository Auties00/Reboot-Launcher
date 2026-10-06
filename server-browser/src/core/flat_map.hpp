#pragma once

#include <functional>

// Open-addressing hash map for production builds; std::unordered_map keeps headers usable
// where Boost is unavailable (quick local builds of the core).
#if __has_include(<boost/unordered/unordered_flat_map.hpp>)
#include <boost/unordered/unordered_flat_map.hpp>
namespace sb {
template <class K, class V, class H = boost::hash<K>, class E = std::equal_to<K>>
using FlatMap = boost::unordered_flat_map<K, V, H, E>;
}
#else
#include <unordered_map>
namespace sb {
template <class K, class V, class H = std::hash<K>, class E = std::equal_to<K>>
using FlatMap = std::unordered_map<K, V, H, E>;
}
#endif
