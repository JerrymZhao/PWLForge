#ifndef COMMON_TYPES_HPP
#define COMMON_TYPES_HPP

#include <vector>
#include <cstdint>
#include <cstddef>
#include <iostream>
#include <iomanip>

// Fitting method enumeration
enum class FittingMethod {
    Linear,
    Quadratic
};

// Interval structure (used throughout all stages)
struct Interval {
    double start;
    double end;
    double hessian;
    size_t level;

    // Quantized values (if quantization enabled in Phase 2.5)
    bool is_quantized = false;
    double start_quantized;
    double end_quantized;

    Interval() : start(0.0), end(0.0), hessian(0.0), level(0) {}
    
    Interval(double s, double e) : start(s), end(e), hessian(0.0), level(0) {}
    
    Interval(double s, double e, double h, size_t l)
        : start(s), end(e), hessian(h), level(l) {}
    
    double length() const { return end - start; }
    
    // Get actual start value (quantized or original)
    double get_start() const { return is_quantized ? start_quantized : start; }
    
    // Get actual end value (quantized or original)
    double get_end() const { return is_quantized ? end_quantized : end; }
};

// Interval metrics (for optimization decisions)
struct IntervalMetrics {
    double entropy;
    double complexity;
    double error_sensitivity;
    double merge_score;
    double max_abs_error;
    double avg_abs_error;

    IntervalMetrics() 
        : entropy(0.0), complexity(0.0), error_sensitivity(0.0),
          merge_score(0.0), max_abs_error(0.0), avg_abs_error(0.0) {}
};

// Floating-point fit parameters (Stage 2: float fitting baseline)
struct FitParameters {
    double a;
    double b;
    double c;
    
    FittingMethod method;
    int order;
    
    double range_start;
    double range_end;
    
    double max_error;
    double avg_error;
    
    // Quantized values (if quantization enabled in Phase 2.5)
    bool is_quantized = false;
    double a_quantized;
    double b_quantized;
    double c_quantized;
    
    FitParameters() 
        : a(0.0), b(0.0), c(0.0), method(FittingMethod::Linear), order(1),
          range_start(0.0), range_end(0.0), max_error(0.0), avg_error(0.0) {}
    
    explicit FitParameters(FittingMethod m) 
        : a(0.0), b(0.0), c(0.0), method(m), 
          order(m == FittingMethod::Linear ? 1 : 2),
          range_start(0.0), range_end(0.0), max_error(0.0), avg_error(0.0) {}
    
    FitParameters(double _b, double _c) 
        : a(0.0), b(_b), c(_c), method(FittingMethod::Linear), order(1),
          range_start(0.0), range_end(0.0), max_error(0.0), avg_error(0.0) {}
    
    FitParameters(double _a, double _b, double _c) 
        : a(_a), b(_b), c(_c), method(FittingMethod::Quadratic), order(2),
          range_start(0.0), range_end(0.0), max_error(0.0), avg_error(0.0) {}
    
    // Get actual parameter values (quantized or original)
    double get_a() const { return is_quantized ? a_quantized : a; }
    double get_b() const { return is_quantized ? b_quantized : b; }
    double get_c() const { return is_quantized ? c_quantized : c; }
};

// Stage 2 float fitting baseline (reference point - minimum error)
struct Stage2FittingBaseline {
    std::vector<FitParameters> fit_params_list;
    
    double max_fitting_error;
    double avg_fitting_error;
    double rmse;
    
    size_t num_segments;
    
    Stage2FittingBaseline()
        : max_fitting_error(0.0), avg_fitting_error(0.0),
          rmse(0.0), num_segments(0) {}
    
    void printBaseline() const {
        std::cout << "\nStage 2 Float Baseline (Quality Upper Bound)\n";
        std::cout << "Segments: " << num_segments << "\n";
        std::cout << std::scientific << std::setprecision(6);
        std::cout << "Max Error: " << max_fitting_error << " (float)\n";
        std::cout << "Avg Error: " << avg_fitting_error << "\n";
        std::cout << "RMSE: " << rmse << "\n";
        std::cout << "Note: Best achievable quality.\n";
        std::cout << "      Stage3 trades quality for compression.\n\n";
    }
};

// Compressed fit parameters (used by function_fitter.hpp)
struct CompressedFitParameters {
    FitParameters params;
    std::vector<double> interval_indices;
    std::vector<double> offsets;
    
    CompressedFitParameters() = default;
};

#endif // COMMON_TYPES_HPP