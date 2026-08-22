#ifndef HW_UTILS_HPP
#define HW_UTILS_HPP

#include <filesystem>
#include <vector>
#include <map>
#include <numeric>
#include <limits>
#include "group_types.hpp"

// Create directory if not exists
inline void ensure_dir(const std::string& path) {
    std::filesystem::create_directories(path);
}

// Reorder groups with configurable strategy
// orphan_first = false: normal groups first (default, better for activation functions)
// orphan_first = true:  orphan groups first
inline std::vector<size_t> reorder_groups(
    const std::vector<QuantizedGroup>& groups,
    bool orphan_first = false)
{
    std::vector<size_t> order;

    if (orphan_first) {
        // Orphans first, then normal groups
        for (size_t i = 0; i < groups.size(); i++) {
            if (groups[i].storage_type == GroupStorageType::ORPHAN_GROUP) {
                order.push_back(i);
            }
        }
        for (size_t i = 0; i < groups.size(); i++) {
            if (groups[i].storage_type != GroupStorageType::ORPHAN_GROUP) {
                order.push_back(i);
            }
        }
    } else {
        // Normal groups first, then orphans (default)
        for (size_t i = 0; i < groups.size(); i++) {
            if (groups[i].storage_type != GroupStorageType::ORPHAN_GROUP) {
                order.push_back(i);
            }
        }
        for (size_t i = 0; i < groups.size(); i++) {
            if (groups[i].storage_type == GroupStorageType::ORPHAN_GROUP) {
                order.push_back(i);
            }
        }
    }

    return order;
}

// Create logical ID -> storage index mapping
inline std::map<size_t, size_t> create_storage_map(const std::vector<size_t>& order) {
    std::map<size_t, size_t> map;
    for (size_t i = 0; i < order.size(); i++) {
        map[order[i]] = i;  // logical_id -> storage_index
    }
    return map;
}

// Get min/max bounds of a group (skip padding)
inline std::pair<double, double> get_group_bounds(const QuantizedGroup& group) {
    double min_x = std::numeric_limits<double>::max();
    double max_x = std::numeric_limits<double>::lowest();

    for (const auto& member : group.members) {
        if (member.is_padding) continue;
        min_x = std::min(min_x, member.original_interval.start);
        max_x = std::max(max_x, member.original_interval.end);
    }
    return {min_x, max_x};
}

// Count total intervals across all groups
inline size_t count_intervals(const std::vector<QuantizedGroup>& groups) {
    return std::accumulate(groups.begin(), groups.end(), size_t(0),
        [](size_t sum, const QuantizedGroup& g) { return sum + g.members.size(); });
}

// Get maximum number of intervals in any single group
inline size_t max_intervals_per_group(const std::vector<QuantizedGroup>& groups) {
    size_t max_size = 0;
    for (const auto& g : groups) {
        max_size = std::max(max_size, g.members.size());
    }
    return max_size;
}

#endif // HW_UTILS_HPP