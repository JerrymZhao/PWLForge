// interval_group_compressor.hpp

#ifndef INTERVAL_GROUP_COMPRESSOR_HPP
#define INTERVAL_GROUP_COMPRESSOR_HPP

#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include "interval_optimizer.hpp"
#include "function_fitter.hpp"

struct DeltaEncoding {
    enum class TransformType {
        Raw,
        Translation,
        YReflection
    };
    TransformType transform{TransformType::Raw};
    double y_offset{0.0};
    std::vector<int8_t> quantized_deltas;

    DeltaEncoding() = default;
    DeltaEncoding(const DeltaEncoding&) = default;
    DeltaEncoding& operator=(const DeltaEncoding&) = default;
};

struct GroupStatistics {
    size_t total_intervals;
    size_t symmetry_pairs;
    size_t translation_pairs;
    double avg_delta;
    double max_delta;
    int optimal_bitwidth{8};
};

// Define the IntervalGroup structure
struct IntervalGroup {
    double length;                          // All intervals in the group have the same length
    Interval base_interval;                 // base interval
    size_t base_interval_idx;               // Index of the base interval
    std::vector<size_t> member_interval_indices; // Indices of the intervals in the group
    FitParameters base_params;              // Base fit parameters
    std::vector<DeltaEncoding> delta_encodings;    // Deltas with the base interval
    std::vector<int8_t> quantized_delta_starts;  // Quantized deltas with the start point of the base interval
    std::vector<int8_t> quantized_delta_ends;    // Quantized deltas with the end point of the base interval
    double delta_scale_factor;              // Scale factor for quantization
    int bitwidth;                           // Bitwidth for the deltas
    double quantization_error;              // Quantization error
    GroupStatistics stats;                  // Group statistics

    IntervalGroup() : length(0.0), base_interval(Interval()), 
                    base_interval_idx(0), delta_scale_factor(1.0), 
                    bitwidth(8), quantization_error(0.0) {}
};

struct SymmetryInfo {
    bool is_symmetric;
    bool is_translated;
    bool is_y_reflected;
    double y_offset;
};

inline int calculateOptimalBitwidth(const std::vector<double>& deltas) {
    double max_delta = 0.0;
    for (const auto& d : deltas) {
        max_delta = std::max(max_delta, std::abs(d));
    }
    return std::ceil(std::log2(max_delta + 1)) + 1;
}

inline SymmetryInfo checkIntervalEquivalence(const FitParameters& params1, 
                                           const FitParameters& params2,
                                           double error_threshold = 1e-5) {
    SymmetryInfo info{false, false, false, 0.0};
    
    if (params1.method != params2.method) return info;

    if (params1.method == FittingMethod::Linear) {
        // Check direct translation (same slope)
        if (std::abs(params1.b - params2.b) < error_threshold) {
            info.is_translated = true;
            info.y_offset = params2.c - params1.c;
            info.is_symmetric = true;
        }
        // Check y-axis reflection + translation
        else if (std::abs(params1.b + params2.b) < error_threshold) {
            info.is_y_reflected = true;
            info.y_offset = params2.c - params1.c;
            info.is_symmetric = true;
        }
    } 
    else if (params1.method == FittingMethod::Quadratic) {
        // Check direct translation (same quadratic and linear terms)
        if (std::abs(params1.a - params2.a) < error_threshold &&
            std::abs(params1.b - params2.b) < error_threshold) {
            info.is_translated = true;
            info.y_offset = params2.c - params1.c;
            info.is_symmetric = true;
        }
        // Check y-axis reflection + translation
        else if (std::abs(params1.a - params2.a) < error_threshold &&
                 std::abs(params1.b + params2.b) < error_threshold) {
            info.is_y_reflected = true;
            info.y_offset = params2.c - params1.c;
            info.is_symmetric = true;
        }
    }

    return info;
}

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
inline void groupAndCompressIntervals(const std::vector<Interval>& intervals, 
                                    const std::vector<FitParameters>& fit_params,
                                    std::vector<IntervalGroup>& groups, 
                                    double tolerance = 1e-4) {
    struct IntervalWithIndex {
        Interval interval;
        size_t index;
    };

    std::vector<IntervalWithIndex> sorted_intervals;
    sorted_intervals.reserve(intervals.size());

    // Track full range coverage
    double full_start = intervals[0].start;
    double full_end = intervals[0].end;

    for (size_t i = 0; i < intervals.size(); ++i) {
        sorted_intervals.push_back({intervals[i], i});
        full_start = std::min(full_start, intervals[i].start);
        full_end = std::max(full_end, intervals[i].end);
    }
    
    // Sorting the intervals by length
    std::sort(sorted_intervals.begin(), sorted_intervals.end(), 
            [&](const IntervalWithIndex& a, const IntervalWithIndex& b) -> bool {
        double len_a = a.interval.end - a.interval.start;
        double len_b = b.interval.end - b.interval.start;
        return (std::abs(len_a - len_b) < tolerance) ?
            a.interval.start < b.interval.start : len_a < len_b; // Start point as tie-breaker
    });

    std::cout << "\nInterval Distribution Analysis:\n";
    std::cout << "Total intervals: " << intervals.size() << "\n";

    // Grouping intervals by length
    std::vector<std::vector<IntervalWithIndex>> grouped_intervals;
    if (!sorted_intervals.empty()) {
        std::vector<IntervalWithIndex> current_group;
        current_group.push_back(sorted_intervals[0]);
        double current_length = sorted_intervals[0].interval.end - 
                                sorted_intervals[0].interval.start;
        double prev_end = sorted_intervals[0].interval.end;
        current_group.push_back(sorted_intervals[0]);
        
        for (size_t i = 1; i < sorted_intervals.size(); ++i) {
            double len = sorted_intervals[i].interval.end - 
                        sorted_intervals[i].interval.start;

            if (std::abs(len - current_length) < tolerance && 
                sorted_intervals[i].interval.start <= prev_end + tolerance) {
                current_group.push_back(sorted_intervals[i]);
                prev_end = sorted_intervals[i].interval.end;
            } else {
                if (!current_group.empty()) {
                    grouped_intervals.push_back(current_group);
                }
                current_group.clear();
                current_group.push_back(sorted_intervals[i]);
                current_length = len;
                prev_end = sorted_intervals[i].interval.end;
            }
        }

        if (!current_group.empty()) {
            std::cout << "Final group size: " << current_group.size() 
                    << ", length: " << current_length << "\n";
            grouped_intervals.push_back(current_group);
        }
    }

    std::cout << "Total groups: " << grouped_intervals.size() << "\n";
    
    // Delta encoding for each group, with compression and quantization
    for (const auto& group : grouped_intervals) {
        if (group.empty()) continue;
        
        size_t symmetry_pairs = 0;
        size_t translation_pairs = 0;
        std::vector<double> deltas;
        
        IntervalGroup new_group;
        new_group.length = group[0].interval.end - group[0].interval.start;
        new_group.base_interval = group[0].interval;
        new_group.base_interval_idx = group[0].index;
        new_group.base_params = fit_params[group[0].index];
        new_group.member_interval_indices.push_back(group[0].index);
        // new_group.bitwidth = bitwidth;
        
        std::vector<double> delta_starts, delta_ends;
        double prev_end = group[0].interval.end;
        double max_abs_delta = 0.0;

        for (size_t i = 1; i < group.size(); ++i) {
            // Check symmtery with the base interval
            auto sym_info = checkIntervalEquivalence(
                new_group.base_params, 
                fit_params[group[i].index], 
                tolerance
            );
            
            if (sym_info.is_symmetric) {
                symmetry_pairs++;
                if (sym_info.is_translated) {
                    translation_pairs++;
                }
                DeltaEncoding delta;
                delta.transform = sym_info.is_translated ?
                    DeltaEncoding::TransformType::Translation : 
                    DeltaEncoding::TransformType::YReflection;
                delta.y_offset = sym_info.y_offset;
                deltas.push_back(sym_info.y_offset);
                new_group.delta_encodings.push_back(std::move(delta));
            } else {
                double delta_start = group[i].interval.start - new_group.base_interval.start;
                double delta_end = group[i].interval.end - new_group.base_interval.end;
                delta_starts.push_back(delta_start);
                delta_ends.push_back(delta_end);
                deltas.push_back(delta_start);
                deltas.push_back(delta_end);
                max_abs_delta = std::max({max_abs_delta, std::abs(delta_start), std::abs(delta_end)});

                DeltaEncoding delta;
                delta.transform = DeltaEncoding::TransformType::Raw;
                new_group.delta_encodings.push_back(std::move(delta));
            }
            new_group.member_interval_indices.push_back(group[i].index);
        }
        
        int optimal_bitwidth = calculateOptimalBitwidth(deltas);
        new_group.bitwidth = std::min(8, optimal_bitwidth);
        
        // Compute the scale factor for quantization
        int max_quantized_value = (1 << (new_group.bitwidth - 1)) - 1;
        if (max_abs_delta > 0) {
            new_group.delta_scale_factor = max_abs_delta / max_quantized_value;
        } else {
            new_group.delta_scale_factor = 1.0;
        }

        new_group.stats.symmetry_pairs = symmetry_pairs;
        new_group.stats.translation_pairs = translation_pairs;
        new_group.stats.total_intervals = group.size();
        new_group.stats.optimal_bitwidth = optimal_bitwidth;
        new_group.stats.max_delta = max_abs_delta;

        double quantization_error = 0.0;
        for (size_t i = 0; i < delta_starts.size(); ++i) {
            int8_t quantized_delta_start = static_cast<int8_t>(std::round(delta_starts[i] / new_group.delta_scale_factor));
            int8_t quantized_delta_end = static_cast<int8_t>(std::round(delta_ends[i] / new_group.delta_scale_factor));
            new_group.quantized_delta_starts.push_back(quantized_delta_start);
            new_group.quantized_delta_ends.push_back(quantized_delta_end);

            double reconstructed_delta_start = quantized_delta_start * new_group.delta_scale_factor;
            double reconstructed_delta_end = quantized_delta_end * new_group.delta_scale_factor;
            double error_start = std::abs(reconstructed_delta_start - delta_starts[i]);
            double error_end = std::abs(reconstructed_delta_end - delta_ends[i]);
            quantization_error += error_start + error_end;
        }
        new_group.quantization_error = quantization_error;
        
        std::sort(new_group.member_interval_indices.begin(),
                  new_group.member_interval_indices.end()),
                  [&intervals](size_t a, size_t b) {
                        return intervals[a].start < intervals[b].start;
                  };

        groups.push_back(new_group);
        std::cout << "\nGroup Statistics:\n"
                  << "Size: " << group.size() << "\n"
                  << "Symmetry pairs: " << symmetry_pairs << "\n"
                  << "Max delta: " << max_abs_delta << "\n";
    }
}

// inline double RecoveredFunctionValue(const CompressedFitParameters& comp_param, double x, double offset) {
//     const FitParameters& shared_params = comp_param.params;
//     double y_pred = 0.0;
//     if (shared_params.order == 1) {
//         y_pred = shared_params.b * x + shared_params.c + offset;
//     } else {
//         y_pred = shared_params.a * x * x + shared_params.b * x + shared_params.c + offset;
//     }
//     return y_pred;
// }

// Evaluate the compressed error including quantization error
inline double evaluateCompressedErrorWithQuantization(
    const std::string& expression_str,
    const std::vector<Interval>& intervals,
    const std::vector<IntervalGroup>& groups,
    const std::vector<CompressedFitParameters>& compressed_params_list) {

    double total_error = 0.0;
    size_t total_points = 0;
    const double eps = 1e-10;

    // Build a map from interval index to group
    std::unordered_map<size_t, IntervalGroup*> interval_to_group_map;
    std::vector<std::pair<double, double>> covered_ranges;
    double prev_end = intervals[0].start - eps;

    for (const auto& group : groups) {
        // Map the base interval
        size_t base_interval_idx = group.base_interval_idx;
        interval_to_group_map[base_interval_idx] = const_cast<IntervalGroup*>(&group);

        if (group.base_interval.start > prev_end + eps) {
            covered_ranges.push_back({prev_end, group.base_interval.start});
            std::cout << "Warning: Gap before base interval " 
                    << base_interval_idx << std::endl;
        }
        covered_ranges.push_back({group.base_interval.start, group.base_interval.end});
        prev_end = group.base_interval.end;

        // Other intervals in the group
        for (size_t i = 0; i < group.quantized_delta_starts.size(); i++) {
            size_t current_idx = base_interval_idx + i + 1;
            double current_start = group.base_interval.start +
                                 group.quantized_delta_starts[i] * group.delta_scale_factor;
            double current_end = group.base_interval.end +
                               group.quantized_delta_ends[i] * group.delta_scale_factor;

            if (current_start > prev_end + eps) {
                covered_ranges.push_back({prev_end, current_start});
                std::cout << "Warning: Gap detected between intervals " 
                         << current_idx - 1 << " and " << current_idx 
                         << " (" << prev_end << " -> " << current_start << ")" << std::endl;
            }
            covered_ranges.push_back({current_start, current_end});
            
            interval_to_group_map[current_idx] = const_cast<IntervalGroup*>(&group);
            prev_end = current_end;
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

inline std::string serializeFitParameters(const FitParameters& params) {
    std::stringstream ss;
    ss << (params.method == FittingMethod::Linear ? "L," : "Q,")
       << params.a << "," << params.b << "," << params.c;
    return ss.str();
}

// Save the compressed groups to a file
inline void saveCompressedGroupsToFile(const std::vector<IntervalGroup>& groups, const std::string& filename) {
    // Print group statistics first
    std::map<double, std::vector<size_t>> length_groups;
    std::vector<std::pair<double, double>> gaps;
    size_t total_intervals = 0;
    size_t total_bits = 0;
    double prev_end = groups[0].base_interval.start;
    
    std::cout << "\nGroup and LUT Mapping Analysis:\n";
    std::cout << "------------------------\n";
    
    for (size_t i = 0; i < groups.size(); i++) {
        const auto& group = groups[i];
        double length = group.base_interval.end - group.base_interval.start;
        length_groups[length].push_back(i);
        
        if (group.base_interval.start > prev_end + 1e-10) {
            gaps.push_back({prev_end, group.base_interval.start});
        }

        // Calculate bits for this group
        size_t group_bits = 0;
        // Base interval (start, end) and fit parameters (a, b, c)
        group_bits += 5 * 16; // 16-bit fixed point for each parameter
        if (!gaps.empty()) {
            group_bits += 16 * gaps.size(); // 16-bit fixed point for each gap
        }
        // Delta encodings
        for (const auto& delta : group.delta_encodings) {
            if (delta.transform == DeltaEncoding::TransformType::Raw) {
                group_bits += 2 * group.bitwidth; // start and end deltas
            } else {
                group_bits += 16; // y_offset for symmetric/translated intervals
                group_bits += 2;  // 2 bits for transform type
            }
        }
        
        total_bits += group_bits;
        total_intervals += group.member_interval_indices.size();
        
        std::cout << "Group " << i << ":\n"
                  << "  Length: " << length << "\n"
                  << "  Base interval: [" << group.base_interval.start 
                  << ", " << group.base_interval.end << "]\n"
                  << "  Members: " << group.member_interval_indices.size() << "\n"
                  << "  Bits required: " << group_bits << "\n"
                  << "  Bits per interval: " << (double)group_bits / group.member_interval_indices.size() << "\n";
    }
    
    std::cout << "\nLUT Mapping Summary:\n";
    std::cout << "------------------------\n";
    std::cout << "Total intervals: " << total_intervals << "\n";
    std::cout << "Total bits: " << total_bits << "\n";
    std::cout << "Average bits per interval: " << (double)total_bits / total_intervals << "\n";
    
    std::cout << "\nLength Distribution:\n";
    std::cout << "------------------------\n";
    for (const auto& [length, group_ids] : length_groups) {
        std::cout << "Length " << length << ":\n"
                  << "  Count: " << group_ids.size() << "\n"
                  << "  Groups: ";
        for (size_t id : group_ids) {
            std::cout << id << " ";
        }
        std::cout << "\n";
    }
    std::cout << "------------------------\n";

    std::ofstream file(filename);
    if (file.is_open()) {
        // File format:
        // GroupID,Length,BaseStart,BaseEnd,DeltaStarts,DeltaEnds
        file << "GroupID,Length,BaseStart,BaseEnd,FitMethod,FitA,FitB,FitC,"
             << "BitWidth,ScaleFactor,DeltaEncodings\n";

        size_t group_id = 0;
        for (const auto& group : groups) {
            // Group metadata and base parameters
            file << group_id << ","
                 << (group.base_interval.end - group.base_interval.start) << ","
                 << std::scientific 
                 << group.base_interval.start << ","
                 << group.base_interval.end << ","
                 << (group.base_params.method == FittingMethod::Linear ? "L" : "Q") << ","
                 << group.base_params.a << ","
                 << group.base_params.b << ","
                 << group.base_params.c << ","
                 << group.bitwidth << ","
                 << group.delta_scale_factor << ",\"";

            // Delta encodings
            for (size_t i = 0; i < group.delta_encodings.size(); ++i) {
                const auto& delta = group.delta_encodings[i];
                file << static_cast<int>(delta.transform) << ":"
                     << delta.y_offset;
                if (delta.transform == DeltaEncoding::TransformType::Raw) {
                    file << ":" << static_cast<int>(group.quantized_delta_starts[i])
                         << ":" << static_cast<int>(group.quantized_delta_ends[i]);
                }
                if (i < group.delta_encodings.size() - 1) file << ";";
            }
            file << "\"\n";
            
            group_id++;
        }
        file.close();
        std::cout << "Compressed groups saved to: " << filename << "\n";
        std::cout << "\nTo reconstruct function f(x):\n"
                  << "1. Find group containing x\n"
                  << "2. If x in base interval: f(x) = ax² + bx + c\n"
                  << "3. If x in delta interval:\n"
                  << "   - Raw: reconstruct using quantized deltas\n"
                  << "   - Translation: apply y_offset\n"
                  << "   - YReflection: reflect and apply y_offset\n";
    } else {
        std::cout << "Failed to open file to save compressed Interval Groups!" << std::endl;
    }
}

#endif // INTERVAL_GROUP_COMPRESSOR_HPP
