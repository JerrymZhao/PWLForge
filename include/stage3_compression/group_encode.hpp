#ifndef GROUP_ENCODE_HPP
#define GROUP_ENCODE_HPP

#include <vector>
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include "group_types.hpp"
#include "group_grouping.hpp"
#include "group_quantization.hpp"
#include "group_symmetry.hpp"
#include "../common/common_types.hpp"

//================================================================================
// Stage 3 encoder - main encoding pipeline
//================================================================================

class Stage3Encoder {
private:
    Stage3Config config_;
    
    // Input (already contains quantized values from Phase 2.5)
    std::vector<Interval> intervals_;         // Has quantized endpoints via is_quantized flag
    std::vector<FitParameters> fit_params_;   // Has quantized params via is_quantized flag
    std::vector<double> original_samples_;
    std::function<double(double)> original_function_;
    
    // Intermediate results
    std::vector<IntervalGroup> groups_;
    std::vector<QuantizedGroup> qgroups_;
    
    // Statistics
    GroupingStats grouping_stats_;
    QuantizationStats quant_stats_;
    
public:
    Stage3Encoder() = default;
    
    // Initialize encoder with data from Phase 2.5 (contains quantized values)
    void initialize(const std::vector<Interval>& intervals,
                   const std::vector<FitParameters>& fit_params,
                   const std::vector<double>& original_samples,
                   const std::function<double(double)>& original_function,
                   const Stage3Config& config) {
        
        if (intervals.empty() || fit_params.empty()) {
            throw std::invalid_argument("Empty intervals or fit_params");
        }
        
        if (intervals.size() != fit_params.size()) {
            throw std::invalid_argument("Intervals and fit_params size mismatch");
        }
        
        intervals_ = intervals;
        fit_params_ = fit_params;
        original_samples_ = original_samples;
        original_function_ = original_function;
        config_ = config;
        
        if (config_.verbose) {
            std::cout << "\n=== Stage 3: Delta Encoding (Using Quantized Data) ===\n";
            std::cout << "Input: " << intervals_.size() << " intervals ";
            
            // Check if data is quantized
            if (!intervals_.empty() && intervals_[0].is_quantized) {
                std::cout << "(quantized from Phase 2.5)\n";
            } else {
                std::cout << "(FP64 - quantization skipped)\n";
            }
            
            config_.print();
        }
    }
    
    // Step 1: Group intervals by length (uses quantized lengths via get_start/get_end)
    void groupIntervals() {
        if (config_.verbose) {
            std::cout << "\nStep 1: Grouping by interval length...\n";
            std::cout << "  (Using quantized endpoints if available)\n";
        }
        
        groups_ = groupIntervalsByLength(intervals_, fit_params_, config_);
        
        // Validate grouping
        validateGroupingCoverage(groups_, intervals_.size());
        
        // Compute statistics
        grouping_stats_ = computeGroupingStats(groups_);
        
        if (config_.verbose) {
            printGroupingStats();
        }
    }
    
    // Step 2: Detect symmetry patterns
    void detectSymmetry() {
        if (!config_.enable_symmetry) {
            if (config_.verbose) {
                std::cout << "\nStep 2: Symmetry detection disabled\n";
            }
            return;
        }
        
        if (config_.verbose) {
            std::cout << "\nStep 2: Detecting symmetry patterns...\n";
        }
        
        detectSymmetryInGroups(groups_, config_);
        
        if (config_.verbose) {
            printSymmetryReport(groups_, config_);
        }
    }
    
    // Step 3: Quantize DELTAS (incremental encoding compression)
    // Note: Endpoints and params were already quantized in Phase 2.5
    //       This step only quantizes the DELTA values for compression
    void quantizeGroups() {
        if (config_.verbose) {
            std::cout << "\nStep 3: Quantizing delta values...\n";
            
            if (!intervals_.empty() && intervals_[0].is_quantized) {
                std::cout << "  (Base endpoints/params already quantized in Phase 2.5)\n";
                std::cout << "  (This step compresses DELTA values via incremental encoding)\n";
            } else {
                std::cout << "  (Operating on FP64 data - Phase 2.5 quantization was skipped)\n";
            }
        }
        
        // Quantize delta values (incremental encoding)
        qgroups_ = ::quantizeGroups(groups_, config_);
        
        // Validate quantization
        if (!validateQuantization(qgroups_, intervals_)) {
            throw std::runtime_error("Delta quantization validation failed");
        }
        
        // Compute statistics with true function
        quant_stats_ = computeQuantizationStatsWithFunction(
            qgroups_, original_function_, config_);
        
        if (config_.verbose) {
            quant_stats_.print();
        }
    }
    
    // Accessors
    const std::vector<IntervalGroup>& getGroups() const { return groups_; }
    const std::vector<QuantizedGroup>& getQuantizedGroups() const { return qgroups_; }
    const GroupingStats& getGroupingStats() const { return grouping_stats_; }
    const QuantizationStats& getQuantizationStats() const { return quant_stats_; }
    
private:
    // Validate that all intervals are covered exactly once
    void validateGroupingCoverage(const std::vector<IntervalGroup>& groups, 
                                  size_t total_intervals) {
        std::vector<bool> covered(total_intervals, false);
        
        for (const auto& group : groups) {
            // Validate length consistency (except ORPHAN groups)
            // Uses get_start() and get_end() which automatically use quantized values
            if (group.storage_type != GroupStorageType::ORPHAN_GROUP) {
                std::vector<size_t> indices;
                for (const auto& member : group.members) {
                    indices.push_back(member.original_index);
                }
                
                if (!validateGroupLengthConsistency(indices, intervals_, 
                                                   config_.length_tolerance)) {
                    throw std::runtime_error("Group length validation failed");
                }
            }
            
            // Check coverage
            for (const auto& member : group.members) {
                size_t idx = member.original_index;
                
                if (idx >= total_intervals) {
                    throw std::runtime_error("Invalid interval index in group");
                }
                
                if (covered[idx]) {
                    throw std::runtime_error("Interval covered by multiple groups");
                }
                
                covered[idx] = true;
            }
        }
        
        // Ensure all covered
        for (size_t i = 0; i < covered.size(); ++i) {
            if (!covered[i]) {
                throw std::runtime_error("Interval " + std::to_string(i) + 
                                       " not covered by any group");
            }
        }
        
        if (config_.verbose) {
            std::cout << "  Grouping validation passed (all " 
                      << total_intervals << " intervals covered)\n";
        }
    }
    
    void printGroupingStats() const {
        std::cout << "\nGrouping results:\n";
        std::cout << "  Total intervals: " << grouping_stats_.total_intervals << "\n";
        std::cout << "  Total groups:    " << grouping_stats_.total_groups << "\n";
        std::cout << "  Normal groups:   " << grouping_stats_.num_normal_groups 
                  << " (" << grouping_stats_.intervals_in_normal_groups << " intervals)\n";
        std::cout << "  Orphan groups:   " << grouping_stats_.num_orphan_groups
                  << " (" << grouping_stats_.intervals_in_orphan_groups << " intervals)\n";
        std::cout << "  Avg group size:  " << std::fixed << std::setprecision(1)
                  << grouping_stats_.avg_group_size << "\n";
        std::cout << "  Estimated compression: " << std::setprecision(2)
                  << grouping_stats_.estimated_compression_ratio << "x\n";
    }
};

//================================================================================
// File I/O functions
//================================================================================

inline void saveCompressedData(const CompressedIntervalData& compressed,
                               const std::string& output_dir) {
    std::string cmd = "mkdir -p " + output_dir;
    int ret = system(cmd.c_str());
    (void)ret;
    
    // Save compression summary
    {
        std::ofstream f(output_dir + "/compression_summary.csv");
        if (!f) throw std::runtime_error("Cannot create compression_summary.csv");
        
        f << "total_intervals,total_groups,compression_ratio\n";
        f << compressed.total_intervals << ","
          << compressed.total_groups << ","
          << std::fixed << std::setprecision(6) << compressed.compression_ratio << "\n";
    }
    
    // Save quantized groups (base params are quantized from Phase 2.5)
    {
        std::ofstream f(output_dir + "/quantized_groups.csv");
        if (!f) throw std::runtime_error("Cannot create quantized_groups.csv");
        
        f << "group_id,storage_type,count,base_a,base_b,base_c,"
          << "delta_pos_bits,delta_a_bits,delta_b_bits,delta_c_bits,"
          << "pos_scale,pos_offset,a_scale,a_offset,b_scale,b_offset,c_scale,c_offset,"
          << "has_symmetry,symmetry_center\n";
        
        for (const auto& g : compressed.groups) {
            std::string type;
            switch (g.storage_type) {
                case GroupStorageType::POWER_OF_2_GROUP: type = "NORMAL"; break;
                case GroupStorageType::ORPHAN_GROUP: type = "ORPHAN"; break;
                case GroupStorageType::SYMMETRIC_PAIR: type = "SYMMETRIC"; break;
            }
            
            f << g.group_id << "," << type << "," << g.count << ","
              << std::scientific << std::setprecision(15)
              << g.base_params.a << "," << g.base_params.b << "," << g.base_params.c << ","
              << (int)g.delta_position_bits << "," << (int)g.delta_a_bits << ","
              << (int)g.delta_b_bits << "," << (int)g.delta_c_bits << ","
              << g.delta_start_scale << "," << g.delta_start_offset << ","
              << g.delta_a_scale << "," << g.delta_a_offset << ","
              << g.delta_b_scale << "," << g.delta_b_offset << ","
              << g.delta_c_scale << "," << g.delta_c_offset << ","
              << (g.has_symmetry ? 1 : 0) << "," << g.symmetry_center << "\n";
        }
    }
    
    // Save deltas (quantized delta values for incremental encoding)
    {
        std::ofstream f(output_dir + "/quantized_deltas.csv");
        if (!f) throw std::runtime_error("Cannot create quantized_deltas.csv");
        
        f << "group_id,member_idx,original_idx,start,end,"
          << "delta_start_q,delta_a_q,delta_b_q,delta_c_q,"
          << "is_y_reflected,is_x_reflected,is_padding\n";
        
        for (const auto& g : compressed.groups) {
            for (size_t i = 0; i < g.members.size(); ++i) {
                const auto& m = g.members[i];
                
                // Save the actual endpoint values (quantized if available)
                double start_val = m.original_interval.is_quantized ? 
                                  m.original_interval.start_quantized : 
                                  m.original_interval.start;
                double end_val = m.original_interval.is_quantized ? 
                                m.original_interval.end_quantized : 
                                m.original_interval.end;
                
                f << g.group_id << "," << i << "," << m.original_index << ","
                  << std::fixed << std::setprecision(15)
                  << start_val << "," << end_val << ","
                  << m.delta_start_q << "," << m.delta_a_q << ","
                  << m.delta_b_q << "," << m.delta_c_q << ","
                  << (m.is_y_reflected ? 1 : 0) << ","
                  << (m.is_x_reflected ? 1 : 0) << ","
                  << (m.is_padding ? 1 : 0) << "\n";
            }
        }
    }
}

inline void saveCompressionStats(const QuantizationStats& stats,
                                 const GroupingStats& grouping_stats,
                                 const std::string& output_dir) {
    std::ofstream f(output_dir + "/compression_stats.csv");
    if (!f) throw std::runtime_error("Cannot create compression_stats.csv");
    
    f << "metric,value\n" << std::scientific << std::setprecision(15);
    
    // Grouping stats
    f << "total_intervals," << grouping_stats.total_intervals << "\n";
    f << "total_groups," << grouping_stats.total_groups << "\n";
    f << "num_normal_groups," << grouping_stats.num_normal_groups << "\n";
    f << "num_orphan_groups," << grouping_stats.num_orphan_groups << "\n";
    f << "avg_group_size," << grouping_stats.avg_group_size << "\n";
    
    // Delta quantization stats
    f << "avg_delta_position_bits," << stats.avg_delta_position_bits << "\n";
    f << "avg_delta_a_bits," << stats.avg_delta_a_bits << "\n";
    f << "avg_delta_b_bits," << stats.avg_delta_b_bits << "\n";
    f << "avg_delta_c_bits," << stats.avg_delta_c_bits << "\n";
    f << "max_quantization_error," << stats.max_quantization_error << "\n";
    f << "max_fitting_error," << stats.max_fitting_error << "\n";
    f << "avg_fitting_error," << stats.avg_fitting_error << "\n";
    f << "rmse," << stats.rmse << "\n";
    f << "compression_ratio," << stats.compression_ratio << "\n";
    
    // Precision errors (if available)
    if (!stats.precision_errors.empty()) {
        std::ofstream pf(output_dir + "/precision_errors.csv");
        if (pf) {
            pf << "format,max_error,avg_error,rmse,overflow_count\n"
               << std::scientific << std::setprecision(15);
            
            for (const auto& [format, ps] : stats.precision_errors) {
                pf << format << "," << ps.max_error << "," << ps.avg_error << ","
                   << ps.rmse << "," << ps.overflow_count << "\n";
            }
        }
    }
}

//================================================================================
// Compression function (implementation for forward declaration in group_grouping.hpp)
//================================================================================

inline CompressedIntervalData compressIntervalGroups(
    const std::vector<QuantizedGroup>& qgroups,
    const Stage3Config& config) {
    
    CompressedIntervalData result;
    result.groups = qgroups;
    result.total_groups = qgroups.size();
    
    result.total_intervals = 0;
    for (const auto& g : qgroups) {
        result.total_intervals += g.count;
    }
    
    // Compute compression statistics
    // Baseline: FP64 storage (5 values × 64 bits per interval)
    size_t uncompressed_bits = result.total_intervals * 5 * 64; // start, end, a, b, c
    size_t compressed_bits = 0;
    
    for (const auto& g : qgroups) {
        if (g.storage_type == GroupStorageType::POWER_OF_2_GROUP) {
            // Normal group: quantized base params + delta encoding
            compressed_bits += 3 * 64;  // base a, b, c (quantized from Phase 2.5)
            compressed_bits += 64;      // metadata (avg_length, etc.)
            
            // Delta storage (incremental encoding)
            compressed_bits += g.count * (
                g.delta_position_bits + 
                g.delta_a_bits + 
                g.delta_b_bits + 
                g.delta_c_bits
            );
        } else if (g.storage_type == GroupStorageType::ORPHAN_GROUP) {
            // ORPHAN group: store full quantized data for each interval
            compressed_bits += 64;              // metadata
            compressed_bits += g.count * 5 * 64; // full: start, end, a, b, c (quantized)
        }
        
        // Symmetry overhead
        if (g.has_symmetry) {
            compressed_bits += 64;  // symmetry center
            compressed_bits += g.symmetric_pairs.size() * 32; // pair indices
        }
    }
    
    result.compression_ratio = static_cast<double>(uncompressed_bits) / 
                              std::max(compressed_bits, size_t(1));
    
    if (config.verbose) {
        std::cout << "\n=== Compression Summary ===\n";
        std::cout << "Total intervals: " << result.total_intervals << "\n";
        std::cout << "Total groups:    " << result.total_groups << "\n";
        std::cout << "Baseline (FP64): " << uncompressed_bits << " bits ("
                  << std::fixed << std::setprecision(2)
                  << (uncompressed_bits / 8192.0) << " KB)\n";
        std::cout << "Compressed:      " << compressed_bits << " bits ("
                  << (compressed_bits / 8192.0) << " KB)\n";
        std::cout << "Ratio:           " << result.compression_ratio << "x\n";
        std::cout << "Note: Compression includes Phase 2.5 quantization + delta encoding\n\n";
    }
    
    return result;
}

#endif // GROUP_ENCODE_HPP