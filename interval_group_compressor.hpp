// interval_group_compressor.hpp

#ifndef INTERVAL_GROUP_COMPRESSOR_HPP
#define INTERVAL_GROUP_COMPRESSOR_HPP

#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include "interval_optimizer.hpp"
#include "function_fitter.hpp"

// Define the IntervalGroup structure
struct IntervalGroup {
    double length;                          // All intervals in the group have the same length
    Interval base_interval;                 // base interval
    size_t base_interval_idx;               // Index of the base interval
    // std::vector<double> delta_starts;    // Delta with the start point of the base interval
    // std::vector<double> delta_ends;      // Delta with the end point of the base interval
    std::vector<size_t> member_interval_indices; // Indices of the intervals in the group
    std::vector<int8_t> quantized_delta_starts;  // Quantized deltas with the start point of the base interval
    std::vector<int8_t> quantized_delta_ends;    // Quantized deltas with the end point of the base interval
    double delta_scale_factor;              // Scale factor for quantization
    int bitwidth;                           // Bitwidth for the deltas
    double quantization_error;              // Quantization error

    IntervalGroup() : length(0.0), base_interval(Interval()), base_interval_idx(0), delta_scale_factor(1.0), bitwidth(8), quantization_error(0.0) {}
};

inline std::string serializeDeltas(const std::vector<double>& deltas) {
    std::string serialized = "\"";
    for (size_t i = 0; i < deltas.size(); ++i) {
        serialized += std::to_string(deltas[i]);
        if (i != deltas.size() - 1) {
            serialized += ";";
        }
    }
    serialized += "\"";
    return serialized;
}

// Grouping the same length intervals and delta encoding
inline void groupAndCompressIntervals(const std::vector<Interval>& intervals, std::vector<IntervalGroup>& groups, double tolerance = 1e-4, int bitwidth = 8) {
    struct IntervalWithIndex {
        Interval interval;
        size_t index;
    };

    std::vector<IntervalWithIndex> sorted_intervals;
    for (size_t i = 0; i < intervals.size(); ++i) {
        sorted_intervals.push_back({intervals[i], i});
    }
    
    // Sorting the intervals by length
    std::sort(sorted_intervals.begin(), sorted_intervals.end(), [&](const IntervalWithIndex& a, const IntervalWithIndex& b) -> bool {
        double len_a = a.interval.end - a.interval.start;
        double len_b = b.interval.end - b.interval.start;
        if (std::abs(len_a - len_b) < tolerance)
            return a.interval.start < b.interval.start; // Start point as tie-breaker
        return len_a < len_b;
    });
    
    // Grouping intervals by length
    std::vector<std::vector<IntervalWithIndex>> grouped_intervals;
    if (!sorted_intervals.empty()) {
        std::vector<IntervalWithIndex> current_group;
        current_group.push_back(sorted_intervals[0]);
        double current_length = sorted_intervals[0].interval.end - sorted_intervals[0].interval.start;
        
        for (size_t i = 1; i < sorted_intervals.size(); ++i) {
            double len = sorted_intervals[i].interval.end - sorted_intervals[i].interval.start;
            if (std::abs(len - current_length) < tolerance) {
                current_group.push_back(sorted_intervals[i]);
            } else {
                grouped_intervals.push_back(current_group);
                current_group.clear();
                current_group.push_back(sorted_intervals[i]);
                current_length = len;
            }
        }
        grouped_intervals.push_back(current_group);
    }
    
    // Delta encoding for each group, with compression and quantization
    for (const auto& group : grouped_intervals) {
        if (group.empty()) continue;
        
        IntervalGroup interval_group;
        interval_group.length = group[0].interval.end - group[0].interval.start;
        interval_group.base_interval = group[0].interval;
        interval_group.base_interval_idx = group[0].index;
        interval_group.member_interval_indices.push_back(group[0].index);
        interval_group.bitwidth = bitwidth;
        
        std::vector<double> delta_starts;
        std::vector<double> delta_ends;
        double max_abs_delta = 0.0;

        for (size_t i = 1; i < group.size(); ++i) {
            double delta_start = group[i].interval.start - interval_group.base_interval.start;
            double delta_end = group[i].interval.end - interval_group.base_interval.end;
            delta_starts.push_back(delta_start);
            delta_ends.push_back(delta_end);
            max_abs_delta = std::max(max_abs_delta, std::abs(delta_start));
            max_abs_delta = std::max(max_abs_delta, std::abs(delta_end));
            interval_group.member_interval_indices.push_back(group[i].index);
        }
        
        // Compute the scale factor for quantization
        int max_quantized_value = (1 << (bitwidth - 1)) - 1;
        if (max_abs_delta > 0) {
            interval_group.delta_scale_factor = max_abs_delta / max_quantized_value;
        } else {
            interval_group.delta_scale_factor = 1.0;
        }

        double quantization_error = 0.0;
        for (size_t i = 0; i < delta_starts.size(); ++i) {
            int8_t quantized_delta_start = static_cast<int8_t>(std::round(delta_starts[i] / interval_group.delta_scale_factor));
            int8_t quantized_delta_end = static_cast<int8_t>(std::round(delta_ends[i] / interval_group.delta_scale_factor));
            interval_group.quantized_delta_starts.push_back(quantized_delta_start);
            interval_group.quantized_delta_ends.push_back(quantized_delta_end);

            double reconstructed_delta_start = quantized_delta_start * interval_group.delta_scale_factor;
            double reconstructed_delta_end = quantized_delta_end * interval_group.delta_scale_factor;
            double error_start = std::abs(reconstructed_delta_start - delta_starts[i]);
            double error_end = std::abs(reconstructed_delta_end - delta_ends[i]);
            quantization_error += error_start + error_end;
        }
        interval_group.quantization_error = quantization_error;

        // Sorting the group by start points
        // std::vector<Interval> sorted_group = group;
        // std::sort(sorted_group.begin(), sorted_group.end(), [&](const Interval& a, const Interval& b) -> bool {
        //     return a.start < b.start;
        // });
        
        // Delta encoding: start and end points
        // double prev_start = sorted_group[0].start;
        // double prev_end = sorted_group[0].end;
        
        // for (size_t i = 1; i < sorted_group.size(); ++i) {
        //     double delta_start = sorted_group[i].start - prev_start;
        //     double delta_end = sorted_group[i].end - prev_end;
        //     interval_group.delta_starts.push_back(delta_start);
        //     interval_group.delta_ends.push_back(delta_end);
        //     prev_start = sorted_group[i].start;
        //     prev_end = sorted_group[i].end;
        // }

        // for (size_t i = 1; i < sorted_group.size(); ++i) {
        //     double delta_start = sorted_group[i].start - interval_group.base_interval.start;
        //     double delta_end = sorted_group[i].end - interval_group.base_interval.end;
        //     interval_group.delta_starts.push_back(delta_start);
        //     interval_group.delta_ends.push_back(delta_end);
        // }
        
        groups.push_back(interval_group);
    }
}

// Evaluate the compressed error including quantization error
inline double evaluateCompressedErrorWithQuantization(
    const std::string& expression_str,
    const std::vector<Interval>& intervals,
    const std::vector<IntervalGroup>& groups,
    const std::vector<CompressedFitParameters>& compressed_params_list) {

    double total_error = 0.0;
    size_t total_points = 0;

    // Build a map from interval index to group
    std::unordered_map<size_t, IntervalGroup*> interval_to_group_map;
    for (const auto& group : groups) {
        // Map the base interval
        // interval_to_group_map[group.base_interval_idx] = &group;
        size_t base_interval_idx = group.base_interval_idx;
        interval_to_group_map[base_interval_idx] = const_cast<IntervalGroup*>(&group);

        // Other intervals in the group
        for (size_t i = 1; i < group.quantized_delta_starts.size() + 1; ++i) {
            interval_to_group_map[base_interval_idx + i] = const_cast<IntervalGroup*>(&group);
        }
    }

    for (const auto& comp_param : compressed_params_list) {
        const FitParameters& params = comp_param.params;

        for (size_t idx = 0; idx < comp_param.interval_indices.size(); ++idx) {
            size_t interval_idx = static_cast<size_t>(comp_param.interval_indices[idx]);
            double offset = comp_param.offsets[idx];
            const Interval& interval = intervals[interval_idx];

            // Find the group for the interval
            auto it = interval_to_group_map.find(interval_idx);
            if (it == interval_to_group_map.end()) {
                std::cout << "Error: Interval not found in the group map!" << std::endl;
                continue;
            }
            const IntervalGroup* group = it->second;
            
            // Reconstruct the interval
            double base_start = group->base_interval.start;
            double base_end = group->base_interval.end;
            double delta_scale = group->delta_scale_factor;
            double interval_start = base_start;
            double interval_end = base_end;

            if (interval_idx != group->base_interval_idx) {
                size_t group_idx = interval_idx - group->base_interval_idx - 1;
                if (group_idx >= group->quantized_delta_starts.size()) {
                    std::cout << "Error: Delta index out of range!" << std::endl;
                    continue;
                }
                int8_t quantized_delta_start = group->quantized_delta_starts[group_idx];
                int8_t quantized_delta_end = group->quantized_delta_ends[group_idx];

                double delta_start = quantized_delta_start * delta_scale;
                double delta_end = quantized_delta_end * delta_scale;

                interval_start += delta_start;
                interval_end += delta_end;
            }

            // Sample several points
            size_t num_samples = 10; 
            double step = (interval.end - interval.start) / (num_samples - 1);

            for (size_t i = 0; i < num_samples; ++i) {
                double x = interval.start + i * step;
                double y_true = computeFunctionValue(expression_str, x);
                double y_pred = RecoveredFunctionValue(comp_param, x, offset);
                double y_group_pred = RecoveredFunctionValue(comp_param, x, offset);

                double error = std::abs(y_true - y_pred);
                total_error += error;
                total_points++;
            }
        }
    }

    for (size_t i = 0; i < intervals.size(); ++i) {
        if (interval_to_group_map.find(i) == interval_to_group_map.end()) {
            std::cout << "Warning: Interval" << i << " is not mapped to any group." << std::endl;
            continue;
        }
    }

    return (total_points > 0) ? total_error / total_points : 0.0;
}

// Save the compressed groups to a file
inline void saveCompressedGroupsToFile(const std::vector<IntervalGroup>& groups, const std::string& filename) {
    std::ofstream file(filename);
    if (file.is_open()) {
        // File format:
        // GroupID,Length,BaseStart,BaseEnd,DeltaStarts,DeltaEnds
        file << "GroupID,Length,BaseStart,BaseEnd,DeltaStarts,DeltaEnds\n";
        size_t group_id = 0;
        for (const auto& group : groups) {
            file << group_id << "," << group.length << "," << group.base_interval.start << "," << group.base_interval.end << ",";
            file << group.bitwidth << "," << group.delta_scale_factor << ",";

            // Save DeltaStarts
            file << "\"";
            for (size_t i = 0; i < group.quantized_delta_starts.size(); ++i) {
                file << static_cast<int>(group.quantized_delta_starts[i]);
                if (i != group.quantized_delta_starts.size() - 1) {
                    file << ";";
                }
            }
            file << "\",";
            
            // Save DeltaEnds
            file << "\"";
            for (size_t i = 0; i < group.quantized_delta_ends.size(); ++i) {
                file << static_cast<int>(group.quantized_delta_ends[i]);
                if (i != group.quantized_delta_ends.size() - 1) {
                    file << ";";
                }
            }
            file << "\"\n";
            
            group_id++;
        }
        file.close();
        std::cout << "Compressed Interval Groups saved to: " << filename << std::endl;
    } else {
        std::cout << "Failed to open file to save compressed Interval Groups!" << std::endl;
    }
}

#endif // INTERVAL_GROUP_COMPRESSOR_HPP
