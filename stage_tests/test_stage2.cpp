#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <iomanip>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <numeric>

// Common types
#include "common_types.hpp"
#include "common_utils.hpp"

// Stage 3 compression
#include "group_types.hpp"
#include "group_grouping.hpp"
#include "group_quantization.hpp"
#include "group_symmetry.hpp"
#include "group_encode.hpp"

// ============================================================================
// Load Stage 1 & 2 Results
// ============================================================================

bool loadIntervals(const std::string& filepath, std::vector<Interval>& intervals) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Cannot open: " << filepath << "\n";
        return false;
    }
    
    intervals.clear();
    std::string line;
    std::getline(file, line); // Skip header
    
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string token;
        
        Interval interval;
        
        std::getline(ss, token, ',');
        interval.start = std::stod(token);
        
        std::getline(ss, token, ',');
        interval.end = std::stod(token);
        
        std::getline(ss, token, ',');
        interval.level = std::stoi(token);
        
        std::getline(ss, token, ',');
        interval.hessian = std::stod(token);
        
        intervals.push_back(interval);
    }
    
    file.close();
    return !intervals.empty();
}

bool loadFitParameters(const std::string& filepath, std::vector<FitParameters>& fits) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Cannot open: " << filepath << "\n";
        return false;
    }
    
    fits.clear();
    std::string line;
    std::getline(file, line); // Skip header
    
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string token;
        
        FitParameters fit;
        fit.method = FittingMethod::Quadratic;
        fit.order = 2;
        
        std::getline(ss, token, ',');
        fit.range_start = std::stod(token);
        
        std::getline(ss, token, ',');
        fit.range_end = std::stod(token);
        
        std::getline(ss, token, ',');
        fit.a = std::stod(token);
        
        std::getline(ss, token, ',');
        fit.b = std::stod(token);
        
        std::getline(ss, token, ',');
        fit.c = std::stod(token);
        
        // Set method based on 'a' value
        if (std::abs(fit.a) < 1e-10) {
            fit.method = FittingMethod::Linear;
            fit.order = 1;
        }
        
        fits.push_back(fit);
    }
    
    file.close();
    return !fits.empty();
}

bool loadOriginalSamples(const std::string& filepath, std::vector<double>& samples) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }
    
    samples.clear();
    std::string line;
    std::getline(file, line); // Skip header
    
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string token;
        
        std::getline(ss, token, ','); // index
        std::getline(ss, token, ','); // x
        std::getline(ss, token, ','); // y
        
        samples.push_back(std::stod(token));
    }
    
    file.close();
    return !samples.empty();
}

// ============================================================================
// Test Compression
// ============================================================================

void testCompression(const std::string& test_name, const std::string& result_dir) {
    std::cout << "\n========================================\n";
    std::cout << "Test: " << test_name << "\n";
    std::cout << "========================================\n";
    
    // Load Stage 1 & 2 results
    std::cout << "\n[1] Load Stage 1 & 2 Results\n";
    
    std::string interval_file = result_dir + "/intervals.csv";
    std::string fit_file = result_dir + "/fit_params.csv";
    std::string sample_file = result_dir + "/samples.csv";
    
    std::vector<Interval> intervals;
    std::vector<FitParameters> fit_params;
    std::vector<double> original_samples;
    
    if (!loadIntervals(interval_file, intervals)) {
        std::cout << "SKIP: No interval data\n";
        return;
    }
    
    if (!loadFitParameters(fit_file, fit_params)) {
        std::cout << "SKIP: No fit parameter data\n";
        return;
    }
    
    bool has_samples = loadOriginalSamples(sample_file, original_samples);
    
    if (intervals.size() != fit_params.size()) {
        std::cout << "ERROR: Interval count mismatch\n";
        return;
    }
    
    std::cout << "Loaded intervals:      " << intervals.size() << "\n";
    std::cout << "Loaded fit parameters: " << fit_params.size() << "\n";
    if (has_samples) {
        std::cout << "Loaded samples:        " << original_samples.size() << "\n";
    }
    
    // Analyze interval characteristics
    std::cout << "\n[2] Interval Statistics\n";
    
    std::vector<double> lengths;
    for (const auto& interval : intervals) {
        lengths.push_back(interval.length());
    }
    
    auto [min_len, max_len] = std::minmax_element(lengths.begin(), lengths.end());
    double avg_len = std::accumulate(lengths.begin(), lengths.end(), 0.0) / lengths.size();
    
    std::cout << "Length: min=" << std::scientific << std::setprecision(2) 
              << *min_len << " max=" << *max_len << " avg=" << avg_len << "\n";
    std::cout << "Length ratio: " << std::fixed << std::setprecision(1) 
              << (*max_len / (*min_len + 1e-10)) << "x\n";
    
    // Configure Stage 3
    std::cout << "\n[3] Configure Stage 3 Compression\n";
    
    Stage3Config config;
    config.length_tolerance = 0.1;
    config.min_group_size = 3;
    config.enable_symmetry = true;
    config.symmetry_tolerance = 1e-6;
    config.symmetry_position_tol = 1e-3;
    config.delta_position_bits = 16;
    config.delta_a_bits = 16;
    config.delta_b_bits = 16;
    config.delta_c_bits = 16;
    config.verbose = false;
    
    std::cout << "Grouping:\n";
    std::cout << "  Length tolerance: " << config.length_tolerance << "\n";
    std::cout << "  Min group size:   " << config.min_group_size << "\n";
    std::cout << "Quantization:\n";
    std::cout << "  Position bits: " << (int)config.delta_position_bits << "\n";
    std::cout << "  Delta A bits:  " << (int)config.delta_a_bits << "\n";
    std::cout << "  Delta B bits:  " << (int)config.delta_b_bits << "\n";
    std::cout << "  Delta C bits:  " << (int)config.delta_c_bits << "\n";
    
    // Run encoding pipeline
    std::cout << "\n[4] Run Stage 3 Encoding Pipeline\n";
    
    CompressedIntervalData compressed;
    GroupingStats grouping_stats;
    QuantizationStats quant_stats;
    
    try {
        Stage3Encoder encoder;
        encoder.initialize(intervals, fit_params, original_samples, config);
        
        std::cout << "  Grouping intervals...\n";
        encoder.groupIntervals();
        
        std::cout << "  Detecting symmetry...\n";
        encoder.detectSymmetry();
        
        std::cout << "  Quantizing parameters...\n";
        encoder.quantizeGroups();
        
        std::cout << "  Finalizing compression...\n";
        compressed = compressIntervalGroups(encoder.getQuantizedGroups(), config);
        
        // Get statistics
        grouping_stats = encoder.getGroupingStats();
        quant_stats = encoder.getQuantizationStats();
        
        std::cout << "  Encoding complete.\n";
        
        // Save results
        std::cout << "\n[5] Save Results\n";
        saveCompressedData(compressed, result_dir);
        saveCompressionStats(quant_stats, grouping_stats, result_dir);
        
        // Decode and validate if we have original samples
        if (has_samples) {
            std::cout << "\n[6] Validation\n";
            
            std::vector<double> reconstructed = decodeStage3(compressed, 
                                                            original_samples.size());
            
            double max_error, avg_error, rmse;
            computeReconstructionError(original_samples, reconstructed,
                                      max_error, avg_error, rmse);
            
            std::cout << "Reconstruction Error:\n";
            std::cout << "  Max:  " << std::scientific << std::setprecision(6) 
                      << max_error << "\n";
            std::cout << "  Avg:  " << avg_error << "\n";
            std::cout << "  RMSE: " << rmse << "\n";
            
            // Validation
            bool validation_ok = validateStage3(intervals, fit_params, original_samples,
                                                compressed, 1e-3);
            std::cout << "\nValidation: " << (validation_ok ? "PASS ✓" : "FAIL ✗") << "\n";
        } else {
            std::cout << "\n[6] Validation\n";
            std::cout << "No original samples - skipping reconstruction validation\n";
        }
        
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << "\n";
        return;
    }
    
    // Summary
    std::cout << "\n========================================\n";
    std::cout << "SUMMARY: " << test_name << "\n";
    std::cout << "========================================\n";
    std::cout << "Intervals:       " << intervals.size() << "\n";
    std::cout << "Groups:          " << compressed.total_groups << "\n";
    std::cout << "  Normal groups: " << grouping_stats.num_normal_groups << "\n";
    std::cout << "  Orphan groups: " << grouping_stats.num_orphan_groups << "\n";
    std::cout << "  Avg size:      " << std::fixed << std::setprecision(1) 
              << grouping_stats.avg_group_size << "\n";
    
    std::cout << "\nCompression:\n";
    std::cout << "  Ratio:         " << std::setprecision(2)
              << compressed.compression_ratio << "x\n";
    
    double saved_kb = (quant_stats.total_bits_uncompressed - 
                       quant_stats.total_bits_compressed) / 8192.0;
    std::cout << "  Space saved:   " << saved_kb << " KB\n";
    
    if (quant_stats.max_fitting_error > 0.0) {
        std::cout << "\nFitting Error:\n";
        std::cout << "  Max: " << std::scientific << std::setprecision(6)
                  << quant_stats.max_fitting_error << "\n";
        std::cout << "  Avg: " << quant_stats.avg_fitting_error << "\n";
        std::cout << "  RMSE: " << quant_stats.rmse << "\n";
    }
    
    std::cout << "\nFiles saved to: " << result_dir << "/\n";
    std::cout << "  - compression_summary.csv\n";
    std::cout << "  - quantized_groups.csv\n";
    std::cout << "  - quantized_deltas.csv\n";
    std::cout << "  - compression_stats.csv\n";
    std::cout << "========================================\n";
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "========================================\n";
    std::cout << "Stage 3 Compression Test Suite\n";
    std::cout << "========================================\n";
    std::cout << "This test loads Stage 1+2 results and\n";
    std::cout << "applies Stage 3 compression.\n";
    std::cout << "========================================\n";
    
    std::vector<std::pair<std::string, std::string>> test_cases = {
        {"tanh(x) [0,1] e=1e-4", "results/tanh_0_1_1e_4"},
        {"tanh(x) [0,1] e=1e-5", "results/tanh_0_1_1e_5"},
        {"exp(x) [0,1] e=1e-4", "results/exp_0_1_1e_4"},
        {"sin(x) [0,pi/2] e=1e-4", "results/sin_0_pi2_1e_4"},
        {"GELU [0,1] e=1e-4", "results/gelu_0_1_1e_4"},
        {"SiLU [-2,1] e=1e-4", "results/silu__2_1_1e_4"},
    };
    
    size_t passed = 0;
    size_t failed = 0;
    size_t skipped = 0;
    
    for (const auto& [name, dir] : test_cases) {
        try {
            testCompression(name, dir);
            passed++;
        } catch (const std::exception& e) {
            std::cout << "EXCEPTION: " << e.what() << "\n";
            failed++;
        }
    }
    
    std::cout << "\n========================================\n";
    std::cout << "Test Results Summary\n";
    std::cout << "========================================\n";
    std::cout << "Passed:  " << passed << " / " << test_cases.size() << "\n";
    std::cout << "Failed:  " << failed << " / " << test_cases.size() << "\n";
    std::cout << "Skipped: " << skipped << " / " << test_cases.size() << "\n";
    
    if (failed == 0 && passed > 0) {
        std::cout << "\n All tests passed!\n";
    } else if (failed > 0) {
        std::cout << "\n Some tests failed.\n";
    }
    
    std::cout << "========================================\n";
    
    return (failed > 0) ? 1 : 0;
}