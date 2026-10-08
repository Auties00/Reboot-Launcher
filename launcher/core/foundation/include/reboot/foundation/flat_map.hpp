#pragma once

#include <functional>

#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>

namespace reboot {

template <class K, class V, class Compare = std::less<>>
using FlatMap = boost::container::flat_map<K, V, Compare>;

template <class K, class Compare = std::less<>>
using FlatSet = boost::container::flat_set<K, Compare>;

}  // namespace reboot
