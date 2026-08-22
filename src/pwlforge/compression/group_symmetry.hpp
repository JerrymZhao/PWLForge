#ifndef GROUP_SYMMETRY_HPP
#define GROUP_SYMMETRY_HPP

#include <vector>
#include <cmath>
#include <algorithm>
#include "group_types.hpp"
#include "../common/common_types.hpp"

// Check if two parameters are symmetric about y-axis
inline bool isYSymmetric(const FitParameters& p1, const FitParameters& p2,
                        double tol = 1e-6) {
    if (p1.method != p2.method || p1.order != p2.order) {
        return false;
    }

    // For y-axis symmetry:
    // f(x) and f(-x) should have: a same, b opposite sign, c same
    bool a_match = std::abs(p1.a - p2.a) < tol;
    bool b_opposite = std::abs(p1.b + p2.b) < tol;
    bool c_match = std::abs(p1.c - p2.c) < tol;

    return a_match && b_opposite && c_match;
}

// Check if two parameters are symmetric about x-axis
inline bool isXSymmetric(const FitParameters& p1, const FitParameters& p2,
                        double tol = 1e-6) {
    if (p1.method != p2.method || p1.order != p2.order) {
        return false;
    }

    // For x-axis symmetry:
    // All coefficients should be opposite sign
    bool a_opposite = std::abs(p1.a + p2.a) < tol;
    bool b_opposite = std::abs(p1.b + p2.b) < tol;
    bool c_opposite = std::abs(p1.c + p2.c) < tol;

    return a_opposite && b_opposite && c_opposite;
}

// Check if two intervals are positioned symmetrically
inline bool arePositionsSymmetric(const Interval& i1, const Interval& i2,
                                 double center, double tol = 1e-4) {
    double mid1 = (i1.start + i1.end) / 2.0;
    double mid2 = (i2.start + i2.end) / 2.0;

    // Check if midpoints are symmetric about center
    double expected_mid2 = 2.0 * center - mid1;

    return std::abs(mid2 - expected_mid2) < tol;
}

// Find symmetric pairs in a group
inline std::vector<size_t> findSymmetricPairs(const IntervalGroup& group,
                                             const Stage3Config& config) {
    std::vector<size_t> pairs;

    if (!config.enable_symmetry || group.members.size() < 2) {
        return pairs;
    }

    const size_t n = group.members.size();
    std::vector<bool> paired(n, false);

    // Try to find pairs
    for (size_t i = 0; i < n; ++i) {
        if (paired[i]) continue;

        const auto& member_i = group.members[i];

        for (size_t j = i + 1; j < n; ++j) {
            if (paired[j]) continue;

            const auto& member_j = group.members[j];

            // Check parameter symmetry
            bool y_sym = isYSymmetric(member_i.original_params,
                                     member_j.original_params,
                                     config.symmetry_tolerance);

            bool x_sym = isXSymmetric(member_i.original_params,
                                     member_j.original_params,
                                     config.symmetry_tolerance);

            // Check position symmetry
            bool pos_sym = arePositionsSymmetric(member_i.original_interval,
                                                member_j.original_interval,
                                                group.symmetry_center,
                                                config.symmetry_position_tol);

            if ((y_sym || x_sym) && pos_sym) {
                // Found a symmetric pair
                pairs.push_back(i);
                pairs.push_back(j);
                paired[i] = true;
                paired[j] = true;
                break;
            }
        }
    }

    return pairs;
}

// Compute symmetry center for a group
inline double computeSymmetryCenter(const IntervalGroup& group) {
    if (group.members.empty()) {
        return 0.0;
    }

    // Use median of interval centers
    std::vector<double> centers;
    centers.reserve(group.members.size());

    for (const auto& member : group.members) {
        double center = (member.original_interval.start +
                        member.original_interval.end) / 2.0;
        centers.push_back(center);
    }

    std::sort(centers.begin(), centers.end());

    size_t mid = centers.size() / 2;
    if (centers.size() % 2 == 0) {
        return (centers[mid - 1] + centers[mid]) / 2.0;
    } else {
        return centers[mid];
    }
}

// Apply symmetry detection to groups
inline void detectSymmetryInGroups(std::vector<IntervalGroup>& groups,
                                  const Stage3Config& config) {
    if (!config.enable_symmetry) {
        return;
    }

    for (auto& group : groups) {
        // Skip orphan groups
        if (group.storage_type == GroupStorageType::ORPHAN_GROUP) {
            continue;
        }

        // Compute symmetry center
        group.symmetry_center = computeSymmetryCenter(group);

        // Find symmetric pairs
        group.symmetric_pairs = findSymmetricPairs(group, config);

        // Mark if group has symmetry
        group.has_symmetry = !group.symmetric_pairs.empty();

        // Update storage type if applicable
        if (group.has_symmetry && group.symmetric_pairs.size() == group.members.size()) {
            group.storage_type = GroupStorageType::SYMMETRIC_PAIR;
        }
    }
}

// Reconstruct symmetric partner parameters
inline FitParameters reconstructSymmetricPartner(const FitParameters& params,
                                                bool y_symmetric,
                                                bool x_symmetric) {
    FitParameters partner = params;

    if (y_symmetric) {
        partner.b = -params.b;
    }

    if (x_symmetric) {
        partner.a = -params.a;
        partner.b = -params.b;
        partner.c = -params.c;
    }

    return partner;
}

// Compute symmetry statistics
inline void computeSymmetryStats(const std::vector<IntervalGroup>& groups,
                                size_t& num_symmetric_groups,
                                size_t& num_symmetric_pairs,
                                double& symmetry_ratio) {
    num_symmetric_groups = 0;
    num_symmetric_pairs = 0;
    size_t total_intervals = 0;

    for (const auto& group : groups) {
        total_intervals += group.count;

        if (group.has_symmetry) {
            num_symmetric_groups++;
            num_symmetric_pairs += group.symmetric_pairs.size() / 2;
        }
    }

    if (total_intervals > 0) {
        symmetry_ratio = static_cast<double>(num_symmetric_pairs * 2) / total_intervals;
    } else {
        symmetry_ratio = 0.0;
    }
}

// Validate symmetry detection
inline bool validateSymmetry(const IntervalGroup& group,
                            const Stage3Config& config) {
    if (!group.has_symmetry) {
        return true;
    }

    // Check that symmetric_pairs has even size
    if (group.symmetric_pairs.size() % 2 != 0) {
        return false;
    }

    // Check that all pair indices are valid
    for (size_t idx : group.symmetric_pairs) {
        if (idx >= group.members.size()) {
            return false;
        }
    }

    // Check that pairs are actually symmetric
    for (size_t i = 0; i < group.symmetric_pairs.size(); i += 2) {
        size_t idx1 = group.symmetric_pairs[i];
        size_t idx2 = group.symmetric_pairs[i + 1];

        const auto& m1 = group.members[idx1];
        const auto& m2 = group.members[idx2];

        bool sym = isYSymmetric(m1.original_params, m2.original_params,
                               config.symmetry_tolerance) ||
                   isXSymmetric(m1.original_params, m2.original_params,
                               config.symmetry_tolerance);

        if (!sym) {
            return false;
        }
    }

    return true;
}

// Optimize storage using symmetry
inline size_t estimateSymmetryStorageSaving(const IntervalGroup& group) {
    if (!group.has_symmetry || group.symmetric_pairs.empty()) {
        return 0;
    }

    // Each symmetric pair saves storage of second member
    size_t num_pairs = group.symmetric_pairs.size() / 2;

    // Assume each member needs 4 * 16 bits (deltas)
    size_t bits_per_member = 4 * 16;

    // Pairs only need to store first member + 1 bit symmetry flag
    size_t saved_bits = num_pairs * (bits_per_member - 1);

    return saved_bits;
}

// Print symmetry report
inline void printSymmetryReport(const std::vector<IntervalGroup>& groups,
                               const Stage3Config& config) {
    if (!config.verbose || !config.enable_symmetry) {
        return;
    }

    size_t num_symmetric_groups = 0;
    size_t num_symmetric_pairs = 0;
    double symmetry_ratio = 0.0;

    computeSymmetryStats(groups, num_symmetric_groups, num_symmetric_pairs,
                        symmetry_ratio);

    std::cout << "\nSymmetry Detection Report:\n";
    std::cout << "  Symmetric groups: " << num_symmetric_groups
              << " / " << groups.size() << "\n";
    std::cout << "  Symmetric pairs:  " << num_symmetric_pairs << "\n";
    std::cout << "  Coverage:         " << std::fixed << std::setprecision(1)
              << (symmetry_ratio * 100.0) << "%\n";

    // Compute storage savings
    size_t total_saved = 0;
    for (const auto& group : groups) {
        total_saved += estimateSymmetryStorageSaving(group);
    }

    std::cout << "  Estimated saving: " << total_saved << " bits ("
              << std::setprecision(2) << (total_saved / 8192.0) << " KB)\n\n";
}

#endif // GROUP_SYMMETRY_HPP