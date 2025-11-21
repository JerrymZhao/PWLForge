#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <iomanip>
#include <algorithm>
#include <fstream>
#include <sys/stat.h>
#include "exprtk.hpp"
#include "interval_types.hpp"
#include "common_utils.hpp"
#include "interval_optimizer.hpp"
#include "function_fitter.hpp"

// ============================================================================
// Helper Functions
// ============================================================================

std::string normalizeExpression(const std::string& expr) {
    std::string result = expr;
    
    // x^2 -> (x*x)
    size_t pos = 0;
    while ((pos = result.find("x^2", pos)) != std::string::npos) {
        result.replace(pos, 3, "(x*x)");
        pos += 5;
    }
    
    // x^3 -> (x*x*x)
    pos = 0;
    while ((pos = result.find("x^3", pos)) != std::string::npos) {
        result.replace(pos, 3, "(x*x*x)");
        pos += 7;
    }
    
    return result;
}

double calculateRMSE(const std::vector<double>& errors) {
    double sum_sq = 0.0;
    for (double e : errors) {
        sum_sq += e * e;
    }
    return std::sqrt(sum_sq / errors.size());
}

void createDirectory(const std::string& path) {
    #ifdef _WIN32
        system(("mkdir " + path + " 2>nul").c_str());
    #else
        system(("mkdir -p " + path).c_str());
    #endif
}

// ============================================================================
// Main Test Function
// ============================================================================

void testStage1Pipeline(const std::string& test_name,
                        const std::string& original_expr,
                        double domain_start,
                        double domain_end,
                        double error_threshold) {
    
    std::cout << "\n┌────────────────────────────────────────┐\n";
    std::cout << "│ " << std::left << std::setw(38) << test_name << " │\n";
    std::cout << "└────────────────────────────────────────┘\n";
    
    // Normalize expression
    std::string expression_str = normalizeExpression(original_expr);
    
    // Quick validation
    double mid_val = computeFunctionValue(expression_str, (domain_start + domain_end) / 2.0);
    if (std::isnan(mid_val) || std::isinf(mid_val)) {
        std::cout << "❌ FAIL: Invalid function\n";
        return;
    }
    
    // ========================================================================
    // Interval Optimization
    // ========================================================================
    
    FittingParametersConfig config;
    config.epsilon_start = error_threshold / 10.0;
    config.epsilon_end = error_threshold / 2.0;
    config.acceptable_error = error_threshold;
    config.min_unit_length = (domain_end - domain_start) / 100000.0;
    config.merge_relax_factor = 1.0;
    config.epsilon_steps = 1;
    
    OptimizationResult opt_result = optimizeIntervals(
        domain_start, domain_end, expression_str, config);
    
    if (opt_result.intervals.empty()) {
        std::cout << "❌ FAIL: No intervals generated\n";
        return;
    }
    
    // ========================================================================
    // Polynomial Fitting
    // ========================================================================
    
    std::vector<FitParameters> fit_params_list;
    fitAllSegments(expression_str, opt_result.intervals, fit_params_list, error_threshold);
    
    // ========================================================================
    // Error Analysis
    // ========================================================================
    
    const int SAMPLES_PER_INTERVAL = 100;
    std::vector<double> all_errors;
    double max_abs_error = 0.0;
    
    for (size_t i = 0; i < opt_result.intervals.size(); ++i) {
        const auto& interval = opt_result.intervals[i];
        const auto& fit = fit_params_list[i];
        
        for (int j = 0; j < SAMPLES_PER_INTERVAL; ++j) {
            double t = static_cast<double>(j) / (SAMPLES_PER_INTERVAL - 1);
            double x = interval.start + t * (interval.end - interval.start);
            
            double true_val = computeFunctionValue(expression_str, x);
            double approx_val = evaluateSegment(x, fit);
            double error = std::abs(true_val - approx_val);
            
            all_errors.push_back(error);
            if (error > max_abs_error) {
                max_abs_error = error;
            }
        }
    }
    
    std::sort(all_errors.begin(), all_errors.end());
    
    double mae = 0.0;
    for (double e : all_errors) mae += e;
    mae /= all_errors.size();
    
    double rmse = calculateRMSE(all_errors);
    
    int over_threshold = 0;
    for (double e : all_errors) {
        if (e > error_threshold) over_threshold++;
    }
    
    // ========================================================================
    // Save Results to Subdirectory
    // ========================================================================
    
    std::string safe_name = test_name;
    std::replace(safe_name.begin(), safe_name.end(), '(', '_');
    std::replace(safe_name.begin(), safe_name.end(), ')', '_');
    std::replace(safe_name.begin(), safe_name.end(), '/', '_');
    std::replace(safe_name.begin(), safe_name.end(), '^', '_');
    std::replace(safe_name.begin(), safe_name.end(), ' ', '_');
    std::replace(safe_name.begin(), safe_name.end(), '[', '_');
    std::replace(safe_name.begin(), safe_name.end(), ']', '_');
    std::replace(safe_name.begin(), safe_name.end(), ',', '_');
    std::replace(safe_name.begin(), safe_name.end(), '-', '_');
    
    std::string result_dir = "results/" + safe_name;
    createDirectory(result_dir);
    
    std::string interval_file = result_dir + "/intervals.csv";
    std::string fit_file = result_dir + "/fit_params.csv";
    
    saveIntervalsToFile(opt_result.intervals, interval_file);
    saveFitParametersToFile(fit_params_list, fit_file);
    
    // ========================================================================
    // Output Summary
    // ========================================================================
    
    bool pass = (mae <= error_threshold);
    
    std::cout << std::fixed << std::setprecision(0);
    std::cout << "  Intervals:  " << std::setw(6) << opt_result.final_count << "\n";
    
    std::cout << std::scientific << std::setprecision(2);
    std::cout << "  MAE:        " << std::setw(10) << mae;
    std::cout << (mae <= error_threshold ? " ✓" : " ✗") << "\n";
    
    std::cout << "  RMSE:       " << std::setw(10) << rmse << "\n";
    std::cout << "  Max Error:  " << std::setw(10) << max_abs_error << "\n";
    
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Over Thr:   " << std::setw(6) << over_threshold 
              << " (" << (100.0 * over_threshold / all_errors.size()) << "%)\n";
    
    std::cout << "\n  " << (pass ? "✅ PASS" : "❌ FAIL") << "\n";
    std::cout << "  Saved to: " << result_dir << "/\n";
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════╗\n";
    std::cout << "║     Algorithm Performance Test Suite             ║\n";
    std::cout << "╚═══════════════════════════════════════════════════╝\n";
    
    createDirectory("results");
    
    // ========================================================================
    // Standard Tests
    // ========================================================================
    
    std::cout << "\n═══ Standard Function Tests ═══\n";
    
    testStage1Pipeline("tanh_0_1_1e-4", "tanh(x)", 0.0, 1.0, 1.0e-4);
    testStage1Pipeline("tanh_0_1_1e-5", "tanh(x)", 0.0, 1.0, 1.0e-5);
    testStage1Pipeline("tanh_-2_2_1e-4", "tanh(x)", -2.0, 2.0, 1.0e-4);
    
    testStage1Pipeline("exp_0_1_1e-4", "exp(x)", 0.0, 1.0, 1.0e-4);
    testStage1Pipeline("exp_-1_1_1e-3", "exp(x)", -1.0, 1.0, 1.0e-3);
    
    testStage1Pipeline("sin_0_1_1e-4", "sin(x)", 0.0, 1.0, 1.0e-4);
    testStage1Pipeline("sin_0_pi2_1e-4", "sin(x)", 0.0, M_PI/2.0, 1.0e-4);
    
    testStage1Pipeline("sqrt_0_1_1e-4", "sqrt(x)", 0.0, 1.0, 1.0e-4);
    testStage1Pipeline("sqrt_0_2_1e-4", "sqrt(x)", 0.0, 2.0, 1.0e-4);
    
    testStage1Pipeline("gelu_0_1_1e-4", 
                       "0.5*x*(1+tanh(sqrt(0.6366197723675814)*(x+0.044715*x*x*x)))", 
                       0.0, 1.0, 1.0e-4);
    
    testStage1Pipeline("gelu_-2_1_1e-4", 
                       "0.5*x*(1+tanh(sqrt(0.6366197723675814)*(x+0.044715*x*x*x)))", 
                       -2.0, 1.0, 1.0e-4);
    
    testStage1Pipeline("silu_0_1_1e-4", "x/(1+exp(-x))", 0.0, 1.0, 1.0e-4);
    testStage1Pipeline("silu_-2_1_1e-4", "x/(1+exp(-x))", -2.0, 1.0, 1.0e-4);
    
    // ========================================================================
    // Split Domain Analysis
    // ========================================================================
    
    std::cout << "\n═══ Split Domain Merge Analysis ═══\n";
    std::cout << "Testing [0,2] vs [0,1]+[1,2] interval efficiency\n";
    
    // sqrt(x) analysis
    std::cout << "\n--- sqrt(x) ---\n";
    testStage1Pipeline("sqrt_0_1_split", "sqrt(x)", 0.0, 1.0, 1.0e-4);
    testStage1Pipeline("sqrt_1_2_split", "sqrt(x)", 1.0, 2.0, 1.0e-4);
    testStage1Pipeline("sqrt_0_2_merged", "sqrt(x)", 0.0, 2.0, 1.0e-4);
    
    // exp(x) analysis
    std::cout << "\n--- exp(x) ---\n";
    testStage1Pipeline("exp_0_1_split", "exp(x)", 0.0, 1.0, 1.0e-4);
    testStage1Pipeline("exp_1_2_split", "exp(x)", 1.0, 2.0, 1.0e-4);
    testStage1Pipeline("exp_0_2_merged", "exp(x)", 0.0, 2.0, 1.0e-4);
    
    // tanh(x) analysis
    std::cout << "\n--- tanh(x) ---\n";
    testStage1Pipeline("tanh_0_1_split", "tanh(x)", 0.0, 1.0, 1.0e-4);
    testStage1Pipeline("tanh_1_2_split", "tanh(x)", 1.0, 2.0, 1.0e-4);
    testStage1Pipeline("tanh_0_2_merged", "tanh(x)", 0.0, 2.0, 1.0e-4);
    
    // sin(x) analysis
    std::cout << "\n--- sin(x) ---\n";
    testStage1Pipeline("sin_0_1_split", "sin(x)", 0.0, 1.0, 1.0e-4);
    testStage1Pipeline("sin_1_2_split", "sin(x)", 1.0, 2.0, 1.0e-4);
    testStage1Pipeline("sin_0_2_merged", "sin(x)", 0.0, 2.0, 1.0e-4);
    
    std::cout << "\n";
    std::cout << "All Tests Completed\n";
    std::cout << "\n";
    
    return 0;
}