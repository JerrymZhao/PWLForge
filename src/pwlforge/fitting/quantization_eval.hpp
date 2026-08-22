#ifndef QUANTIZATION_EVAL_HPP
#define QUANTIZATION_EVAL_HPP

#include <vector>
#include <cmath>
#include <algorithm>
#include <limits>
#include <functional>
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include "quantization_precision.hpp"
#include "../common/common_types.hpp"

//================================================================================
// Data range analyzer
//================================================================================

struct DataRange {
    double min_endpoint = std::numeric_limits<double>::max();
    double max_endpoint = std::numeric_limits<double>::lowest();
    double min_param_a = std::numeric_limits<double>::max();
    double max_param_a = std::numeric_limits<double>::lowest();
    double min_param_b = std::numeric_limits<double>::max();
    double max_param_b = std::numeric_limits<double>::lowest();
    double min_param_c = std::numeric_limits<double>::max();
    double max_param_c = std::numeric_limits<double>::lowest();

    void update_endpoint(double val) {
        min_endpoint = std::min(min_endpoint, val);
        max_endpoint = std::max(max_endpoint, val);
    }

    void update_param_a(double val) {
        min_param_a = std::min(min_param_a, val);
        max_param_a = std::max(max_param_a, val);
    }

    void update_param_b(double val) {
        min_param_b = std::min(min_param_b, val);
        max_param_b = std::max(max_param_b, val);
    }

    void update_param_c(double val) {
        min_param_c = std::min(min_param_c, val);
        max_param_c = std::max(max_param_c, val);
    }

    void print() const {
        std::cout << "Data Range Analysis:\n";
        std::cout << std::scientific << std::setprecision(6);
        std::cout << "  Endpoints: [" << min_endpoint << ", " << max_endpoint << "]\n";
        std::cout << "  Param A:   [" << min_param_a << ", " << max_param_a << "]\n";
        std::cout << "  Param B:   [" << min_param_b << ", " << max_param_b << "]\n";
        std::cout << "  Param C:   [" << min_param_c << ", " << max_param_c << "]\n";
    }
};

//================================================================================
// Quantization configuration
//================================================================================

struct QuantizationConfig {
    PrecisionConfig endpoint_precision;
    PrecisionConfig param_precision;

    QuantizationConfig()
        : endpoint_precision(NumericFormat::FP64, 0, 0),
          param_precision(NumericFormat::FP64, 0, 0) {}

    QuantizationConfig(const PrecisionConfig& ep, const PrecisionConfig& pm)
        : endpoint_precision(ep), param_precision(pm) {}

    std::string name() const {
        return endpoint_precision.name() + "_" + param_precision.name();
    }

    // Create auto-configured FIXED quantization
    static QuantizationConfig create_auto_fixed(
        uint8_t endpoint_bits,
        uint8_t param_bits,
        const DataRange& range) {

        PrecisionConfig ep_config = PrecisionConfig::create_auto_fixed(
            endpoint_bits, range.min_endpoint, range.max_endpoint);

        // For parameters, use the widest range among a, b, c
        double param_min = std::min({range.min_param_a, range.min_param_b, range.min_param_c});
        double param_max = std::max({range.max_param_a, range.max_param_b, range.max_param_c});

        PrecisionConfig param_config = PrecisionConfig::create_auto_fixed(
            param_bits, param_min, param_max);

        return QuantizationConfig(ep_config, param_config);
    }
};

//================================================================================
// Quantized interval
//================================================================================

struct QuantizedInterval {
    size_t index;

    // Original values
    double start_orig;
    double end_orig;
    double a_orig;
    double b_orig;
    double c_orig;

    // Quantized values
    double start_q;
    double end_q;
    double a_q;
    double b_q;
    double c_q;

    QuantizedInterval()
        : index(0),
          start_orig(0.0), end_orig(0.0), a_orig(0.0), b_orig(0.0), c_orig(0.0),
          start_q(0.0), end_q(0.0), a_q(0.0), b_q(0.0), c_q(0.0) {}
};

//================================================================================
// Quantization error statistics
//================================================================================

struct QuantizationErrorStats {
    // Parameter quantization errors (vs original FP64 parameters)
    double max_param_error = 0.0;
    double avg_param_error = 0.0;

    // Function approximation errors (vs TRUE function, using quantized params)
    double max_mae = 0.0;
    double avg_mae = 0.0;
    double rmse = 0.0;

    // Compression metrics
    size_t original_bits = 0;
    size_t quantized_bits = 0;
    double compression_ratio = 1.0;

    void print() const {
        std::cout << "\n=== Quantization Error Statistics ===\n";
        std::cout << std::scientific << std::setprecision(6);

        std::cout << "Parameter Quantization (vs FP64 params):\n";
        std::cout << "  Max param error:     " << max_param_error << "\n";
        std::cout << "  Avg param error:     " << avg_param_error << "\n";

        std::cout << "\nFunction Approximation (vs TRUE function):\n";
        std::cout << "  Max MAE:             " << max_mae << "\n";
        std::cout << "  Avg MAE:             " << avg_mae << "\n";
        std::cout << "  RMSE:                " << rmse << "\n";

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "\nCompression:\n";
        std::cout << "  Compression ratio:   " << compression_ratio << "x\n";
        std::cout << "======================================\n\n";
    }
};

//================================================================================
// Quantization evaluator
//================================================================================

class QuantizationEvaluator {
private:
    std::function<double(double)> true_function_;
    size_t num_samples_;

public:
    QuantizationEvaluator(const std::function<double(double)>& true_func,
                         size_t num_samples = 1000)
        : true_function_(true_func), num_samples_(num_samples) {}

    // Analyze data ranges from intervals and parameters
    static DataRange analyze_data_range(
        const std::vector<Interval>& intervals,
        const std::vector<FitParameters>& params) {

        if (intervals.size() != params.size()) {
            throw std::invalid_argument("Intervals and params size mismatch");
        }

        DataRange range;

        for (size_t i = 0; i < params.size(); ++i) {
            range.update_endpoint(params[i].range_start);
            range.update_endpoint(params[i].range_end);
            range.update_param_a(params[i].a);
            range.update_param_b(params[i].b);
            range.update_param_c(params[i].c);
        }

        return range;
    }

    // Quantize intervals and parameters
    std::vector<QuantizedInterval> quantize_intervals(
        const std::vector<Interval>& intervals,
        const std::vector<FitParameters>& params,
        const QuantizationConfig& config) const {

        if (intervals.size() != params.size()) {
            throw std::invalid_argument("Intervals and params size mismatch");
        }

        std::vector<QuantizedInterval> result;
        result.reserve(intervals.size());

        for (size_t i = 0; i < intervals.size(); ++i) {
            result.push_back(quantize_single_interval(intervals[i], params[i], i, config));
        }

        return result;
    }

    // Quantize single interval
    QuantizedInterval quantize_single_interval(
        const Interval& interval,
        const FitParameters& params,
        size_t index,
        const QuantizationConfig& config) const {

        QuantizedInterval qi;
        qi.index = index;

        // Store original values
        qi.start_orig = params.range_start;
        qi.end_orig = params.range_end;
        qi.a_orig = params.a;
        qi.b_orig = params.b;
        qi.c_orig = params.c;

        // Quantize endpoints
        qi.start_q = QuantizationUtils::quantize(params.range_start, config.endpoint_precision);
        qi.end_q = QuantizationUtils::quantize(params.range_end, config.endpoint_precision);

        // Quantize parameters
        qi.a_q = QuantizationUtils::quantize(params.a, config.param_precision);
        qi.b_q = QuantizationUtils::quantize(params.b, config.param_precision);
        qi.c_q = QuantizationUtils::quantize(params.c, config.param_precision);

        return qi;
    }

    // Evaluate all errors
    QuantizationErrorStats evaluate_errors(
        const std::vector<Interval>& original_intervals,
        const std::vector<FitParameters>& original_params,
        const std::vector<QuantizedInterval>& quantized,
        const QuantizationConfig& config) const {

        QuantizationErrorStats stats;

        // 1. Parameter quantization errors
        evaluate_param_errors(original_params, quantized, stats);

        // 2. Function approximation errors
        evaluate_function_errors(quantized, stats);

        // 3. Compression metrics
        compute_compression_stats(original_intervals.size(), config, stats);

        return stats;
    }

private:
    // Evaluate parameter quantization errors
    void evaluate_param_errors(
        const std::vector<FitParameters>& original_params,
        const std::vector<QuantizedInterval>& quantized,
        QuantizationErrorStats& stats) const {

        double sum_error = 0.0;
        stats.max_param_error = 0.0;

        for (size_t i = 0; i < quantized.size(); ++i) {
            const auto& qi = quantized[i];

            double a_err = std::abs(original_params[i].a - qi.a_q);
            double b_err = std::abs(original_params[i].b - qi.b_q);
            double c_err = std::abs(original_params[i].c - qi.c_q);

            double max_err = std::max({a_err, b_err, c_err});

            stats.max_param_error = std::max(stats.max_param_error, max_err);
            sum_error += max_err;
        }

        if (!quantized.empty()) {
            stats.avg_param_error = sum_error / quantized.size();
        }
    }

    // Evaluate function approximation errors (vs true function)
    void evaluate_function_errors(
        const std::vector<QuantizedInterval>& quantized,
        QuantizationErrorStats& stats) const {

        double sum_error = 0.0;
        double sum_squared_error = 0.0;
        size_t total_samples = 0;

        stats.max_mae = 0.0;

        for (const auto& qi : quantized) {
            double x_start = qi.start_q;
            double x_end = qi.end_q;

            if (std::abs(x_end - x_start) < 1e-15) {
                continue;
            }

            double dx = (x_end - x_start) / std::max(num_samples_ - 1, size_t(1));

            for (size_t j = 0; j < num_samples_; ++j) {
                double x = x_start + j * dx;

                // True function value
                double y_true = true_function_(x);

                // Approximation using global coordinate x (matching fitSegment logic)
                double y_approx = qi.a_q * x * x + qi.b_q * x + qi.c_q;

                double error = std::abs(y_true - y_approx);

                stats.max_mae = std::max(stats.max_mae, error);
                sum_error += error;
                sum_squared_error += error * error;
                total_samples++;
            }
        }

        if (total_samples > 0) {
            stats.avg_mae = sum_error / total_samples;
            stats.rmse = std::sqrt(sum_squared_error / total_samples);
        }
    }

    // Compute compression statistics
    void compute_compression_stats(
        size_t num_intervals,
        const QuantizationConfig& config,
        QuantizationErrorStats& stats) const {

        // Original: FP64 for all values (2 endpoints + 3 params)
        stats.original_bits = num_intervals * 5 * 64;

        // Quantized
        size_t endpoint_bits = config.endpoint_precision.bits_per_value();
        size_t param_bits = config.param_precision.bits_per_value();

        stats.quantized_bits = num_intervals * (2 * endpoint_bits + 3 * param_bits);

        stats.compression_ratio = static_cast<double>(stats.original_bits) /
                                 std::max(stats.quantized_bits, size_t(1));
    }
};

//================================================================================
// Configuration sweeper
//================================================================================

class ConfigurationSweeper {
private:
    const QuantizationEvaluator& evaluator_;

public:
    ConfigurationSweeper(const QuantizationEvaluator& eval) : evaluator_(eval) {}

    QuantizationConfig find_best_config(
        const std::vector<Interval>& intervals,
        const std::vector<FitParameters>& params,
        const std::vector<QuantizationConfig>& configs) const {

        if (configs.empty()) {
            throw std::invalid_argument("Empty config list");
        }

        QuantizationConfig best_config = configs[0];
        double best_score = std::numeric_limits<double>::max();

        for (const auto& config : configs) {
            auto quantized = evaluator_.quantize_intervals(intervals, params, config);
            auto stats = evaluator_.evaluate_errors(intervals, params, quantized, config);

            // Score: balance error and compression
            double score = stats.max_mae / stats.compression_ratio;

            if (score < best_score) {
                best_score = score;
                best_config = config;
            }
        }

        return best_config;
    }
};

#endif // QUANTIZATION_EVAL_HPP