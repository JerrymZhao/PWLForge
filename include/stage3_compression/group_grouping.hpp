#ifndef GROUP_GROUPING_HPP
#define GROUP_GROUPING_HPP

#include <vector>
#include <map>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include "group_types.hpp"
#include "../common/common_types.hpp"

//================================================================================
// Length bucketing (based on quantized lengths)
//================================================================================

inline size_t computeLengthBucket(double length, double tolerance) {
    if (length <= 0.0 || tolerance <= 0.0) return 0;
    
    // Strict bucketing based on quantized length values
    // Example: tolerance = 0.001
    //   0.04999 -> bucket 49
    //   0.05000 -> bucket 50
    //   0.05001 -> bucket 50
    //   0.05100 -> bucket 51
    return static_cast<size_t>(std::floor(length / tolerance));
}

//================================================================================
// Validation
//================================================================================

inline bool validateGroupLengthConsistency(
    const std::vector<size_t>& indices,
    const std::vector<Interval>& intervals,
    double tolerance) {
    
    if (indices.empty()) return true;
    
    double min_len = std::numeric_limits<double>::max();
    double max_len = std::numeric_limits<double>::lowest();
    
    // Use quantized endpoint values via get_start() and get_end()
    for (size_t idx : indices) {
        double len = intervals[idx].get_end() - intervals[idx].get_start();
        min_len = std::min(min_len, len);
        max_len = std::max(max_len, len);
    }
    
    double range = max_len - min_len;
    
    if (range > tolerance) {
        std::cerr << "ERROR: Length inconsistency detected!\n";
        std::cerr << "  Min length: " << std::fixed << std::setprecision(10) 
                  << min_len << "\n";
        std::cerr << "  Max length: " << max_len << "\n";
        std::cerr << "  Range: " << range << " > tolerance " << tolerance << "\n";
        return false;
    }
    
    return true;
}

//================================================================================
// Main grouping function
//================================================================================

inline std::vector<IntervalGroup> groupIntervalsByLength(
    const std::vector<Interval>& intervals,
    const std::vector<FitParameters>& fit_params,
    const Stage3Config& config) {
    
    if (intervals.size() != fit_params.size()) {
        throw std::invalid_argument("Intervals and fit_params size mismatch");
    }
    
    if (intervals.empty()) {
        return std::vector<IntervalGroup>();
    }
    
    //------------------------------------------------------------------------
    // Step 1: Bucket intervals by quantized length
    //------------------------------------------------------------------------
    
    std::map<size_t, std::vector<size_t>> buckets;
    
    if (config.verbose) {
        std::cout << "\n=== Length Bucketing (Using Quantized Endpoints) ===\n";
        std::cout << "Tolerance: " << config.length_tolerance << "\n\n";
    }
    
    for (size_t i = 0; i < intervals.size(); ++i) {
        // Compute length using quantized endpoints
        double length = intervals[i].get_end() - intervals[i].get_start();
        size_t bucket = computeLengthBucket(length, config.length_tolerance);
        buckets[bucket].push_back(i);
        
        if (config.verbose) {
            std::cout << "  Interval " << i << ": length=" 
                      << std::fixed << std::setprecision(10) << length 
                      << " -> bucket " << bucket << "\n";
        }
    }
    
    if (config.verbose) {
        std::cout << "\nBucket summary:\n";
        for (const auto& [bucket_id, indices] : buckets) {
            std::cout << "  Bucket " << bucket_id << ": " << indices.size() 
                      << " intervals\n";
        }
        std::cout << "\n";
    }
    
    //------------------------------------------------------------------------
    // Step 2: Create groups and collect orphans
    //------------------------------------------------------------------------
    
    std::vector<IntervalGroup> groups;
    std::vector<size_t> orphan_indices;
    size_t group_counter = 0;
    
    for (const auto& [bucket_id, indices] : buckets) {
        // Validate length consistency
        if (!validateGroupLengthConsistency(indices, intervals, 
                                           config.length_tolerance)) {
            throw std::runtime_error("Bucket " + std::to_string(bucket_id) + 
                                   " failed length consistency check");
        }
        
        if (indices.size() >= config.min_group_size) {
            //----------------------------------------------------------------
            // Normal group: use delta encoding
            //----------------------------------------------------------------
            
            std::vector<size_t> sorted_indices = indices;
            std::sort(sorted_indices.begin(), sorted_indices.end(),
                [&intervals](size_t a, size_t b) {
                    // Sort by quantized start position
                    return intervals[a].get_start() < intervals[b].get_start();
                });
            
            IntervalGroup group;
            group.group_id = "G" + std::to_string(bucket_id) + 
                           "_" + std::to_string(group_counter++);
            group.storage_type = GroupStorageType::POWER_OF_2_GROUP;
            group.count = sorted_indices.size();
            
            // Compute base parameters using quantized values
            double sum_a = 0.0, sum_b = 0.0, sum_c = 0.0, sum_len = 0.0;
            
            for (size_t idx : sorted_indices) {
                sum_a += fit_params[idx].get_a();
                sum_b += fit_params[idx].get_b();
                sum_c += fit_params[idx].get_c();
                
                double len = intervals[idx].get_end() - intervals[idx].get_start();
                sum_len += len;
            }
            
            size_t n = sorted_indices.size();
            group.base_params.a = sum_a / n;
            group.base_params.b = sum_b / n;
            group.base_params.c = sum_c / n;
            group.base_params.method = fit_params[sorted_indices[0]].method;
            group.base_params.order = fit_params[sorted_indices[0]].order;
            group.avg_length = sum_len / n;
            
            // Compute length variance
            double var_sum = 0.0;
            for (size_t idx : sorted_indices) {
                double len = intervals[idx].get_end() - intervals[idx].get_start();
                double diff = len - group.avg_length;
                var_sum += diff * diff;
            }
            group.length_variance = var_sum / n;
            
            if (config.verbose) {
                double std_dev = std::sqrt(group.length_variance);
                std::cout << "Created group " << group.group_id << ":\n";
                std::cout << "  Count: " << group.count << "\n";
                std::cout << "  Avg length: " << std::setprecision(10) 
                          << group.avg_length << "\n";
                std::cout << "  Std dev: " << std_dev << "\n";
                
                if (std_dev > config.length_tolerance / 2.0) {
                    std::cout << "  WARNING: Large std dev (>" 
                              << config.length_tolerance / 2.0 << ")\n";
                }
                std::cout << "\n";
            }
            
            // Create delta encodings using quantized values
            double expected_start = intervals[sorted_indices[0]].get_start();
            
            for (size_t idx : sorted_indices) {
                DeltaEncoding delta;
                delta.original_index = idx;
                
                // Store quantized values in original_interval and original_params
                delta.original_interval = intervals[idx];
                delta.original_params = fit_params[idx];
                
                // Delta encoding based on quantized values
                delta.delta_start = intervals[idx].get_start() - expected_start;
                delta.delta_a = fit_params[idx].get_a() - group.base_params.a;
                delta.delta_b = fit_params[idx].get_b() - group.base_params.b;
                delta.delta_c = fit_params[idx].get_c() - group.base_params.c;
                
                group.members.push_back(delta);
                
                expected_start += group.avg_length;
            }
            
            groups.push_back(group);
            
        } else {
            //----------------------------------------------------------------
            // Too small: collect as orphan
            //----------------------------------------------------------------
            
            if (config.verbose) {
                std::cout << "Bucket " << bucket_id << " has only " 
                          << indices.size() << " intervals (< min_group_size=" 
                          << config.min_group_size << "), marking as orphans\n";
            }
            
            orphan_indices.insert(orphan_indices.end(), 
                                 indices.begin(), indices.end());
        }
    }
    
    //------------------------------------------------------------------------
    // Step 3: Create single ORPHAN group
    //------------------------------------------------------------------------
    
    if (!orphan_indices.empty()) {
        if (config.verbose) {
            std::cout << "\nCreating ORPHAN group with " 
                      << orphan_indices.size() << " intervals\n";
        }
        
        // Sort by quantized position
        std::sort(orphan_indices.begin(), orphan_indices.end(),
            [&intervals](size_t a, size_t b) {
                return intervals[a].get_start() < intervals[b].get_start();
            });
        
        IntervalGroup orphan_group;
        orphan_group.group_id = "ORPHAN";
        orphan_group.storage_type = GroupStorageType::ORPHAN_GROUP;
        orphan_group.count = orphan_indices.size();
        
        // No meaningful base (will store absolute quantized values)
        orphan_group.base_params.a = 0.0;
        orphan_group.base_params.b = 0.0;
        orphan_group.base_params.c = 0.0;
        orphan_group.avg_length = 0.0;
        orphan_group.length_variance = 0.0;
        
        for (size_t idx : orphan_indices) {
            DeltaEncoding delta;
            delta.original_index = idx;
            delta.original_interval = intervals[idx];
            delta.original_params = fit_params[idx];
            
            // No delta encoding for orphans (store absolute quantized values)
            delta.delta_start = 0.0;
            delta.delta_a = 0.0;
            delta.delta_b = 0.0;
            delta.delta_c = 0.0;
            
            orphan_group.members.push_back(delta);
        }
        
        groups.push_back(orphan_group);
    }
    
    //------------------------------------------------------------------------
    // Summary
    //------------------------------------------------------------------------
    
    if (config.verbose) {
        std::cout << "\n=== Grouping Complete ===\n";
        std::cout << "Total groups: " << groups.size() << "\n";
        
        size_t normal_count = 0, orphan_count = 0, orphan_intervals = 0;
        for (const auto& g : groups) {
            if (g.storage_type == GroupStorageType::ORPHAN_GROUP) {
                orphan_count++;
                orphan_intervals = g.count;
            } else {
                normal_count++;
            }
        }
        
        std::cout << "  Normal groups: " << normal_count << "\n";
        std::cout << "  Orphan groups: " << orphan_count 
                  << " (" << orphan_intervals << " intervals)\n\n";
    }
    
    return groups;
}

//================================================================================
// Statistics
//================================================================================

inline GroupingStats computeGroupingStats(const std::vector<IntervalGroup>& groups) {
    GroupingStats stats;
    stats.total_intervals = 0;
    stats.num_normal_groups = 0;
    stats.num_orphan_groups = 0;
    stats.intervals_in_normal_groups = 0;
    stats.intervals_in_orphan_groups = 0;
    
    double sum_group_size = 0.0;
    stats.max_group_size = 0.0;
    stats.min_group_size = std::numeric_limits<double>::max();
    
    for (const auto& group : groups) {
        stats.total_intervals += group.count;
        
        if (group.storage_type == GroupStorageType::ORPHAN_GROUP) {
            stats.num_orphan_groups++;
            stats.intervals_in_orphan_groups += group.count;
        } else {
            stats.num_normal_groups++;
            stats.intervals_in_normal_groups += group.count;
            
            sum_group_size += group.count;
            stats.max_group_size = std::max(stats.max_group_size, 
                                           static_cast<double>(group.count));
            stats.min_group_size = std::min(stats.min_group_size, 
                                           static_cast<double>(group.count));
        }
    }
    
    stats.total_groups = groups.size();
    
    if (stats.num_normal_groups > 0) {
        stats.avg_group_size = sum_group_size / stats.num_normal_groups;
    } else {
        stats.min_group_size = 0.0;
    }
    
    // Estimate compression ratio (already using quantized data from Phase 2.5)
    size_t uncompressed_bits = stats.total_intervals * 5 * 64; // start, end, a, b, c (FP64)
    
    size_t compressed_bits = 0;
    for (const auto& group : groups) {
        if (group.storage_type == GroupStorageType::POWER_OF_2_GROUP) {
            // Normal group: base + deltas
            compressed_bits += 3 * 64;          // base a, b, c (quantized)
            compressed_bits += 64;              // metadata (avg_length, etc.)
            compressed_bits += group.count * 4 * 16; // assume 16-bit deltas (start, a, b, c)
        } else {
            // ORPHAN group: store full quantized data for each interval
            compressed_bits += 64;              // metadata
            compressed_bits += group.count * 5 * 64; // full quantized: start, end, a, b, c
        }
    }
    
    stats.estimated_compression_ratio = 
        static_cast<double>(uncompressed_bits) / std::max(compressed_bits, size_t(1));
    
    return stats;
}

//================================================================================
// Compression (forward declaration - actual implementation in group_encode.hpp)
//================================================================================

inline CompressedIntervalData compressIntervalGroups(
    const std::vector<QuantizedGroup>& qgroups,
    const Stage3Config& config);

#endif // GROUP_GROUPING_HPP