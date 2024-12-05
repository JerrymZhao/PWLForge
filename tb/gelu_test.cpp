#include <iostream>
#include <vector>
#include <cmath>
#include "../function_fitter.hpp"
#include "../interval_optimizer.hpp"

// GELU activation function
double gelu(double x) {
    return 0.5 * x * (1.0 + std::erf(x / std::sqrt(2.0)));
}

// Compute function value based on input expression
// double computeFunctionValue(const std::string& expression_str, double x) {
//     if (expression_str == "gelu(x)") {
//         return gelu(x);
//     }
//     // Add more functions if needed
//     return 0.0;
// }

// Compute error weight based on sensitivity
// double computeErrorWeight(double x) {
//     // Higher weight in regions where derivative is large (sensitive regions)
//     double derivative = 0.5 * (1 + std::erf(x / std::sqrt(2.0))) +
//                         (x / (std::sqrt(2 * M_PI))) * std::exp(-0.5 * x * x);
//     return std::abs(derivative);
// }

int main() {
    double start = -10.0;  // Start Point
    double end = 10.0;     // End Point
    size_t num_points = 1024;  // Number of Points
    std::string expression_str = "gelu(x)";

    // Generate intervals with variable precision
    std::vector<Interval> intervals = generateAdaptiveIntervals(
        start, end, num_points, expression_str);

    // Fit the function over the intervals
    std::vector<FitParameters> fit_params_list;
    fitAllSegments(expression_str, intervals, fit_params_list);

    // Compress the fit parameters
    std::vector<CompressedFitParameters> compressed_params_list;
    compressFitParameters(fit_params_list, intervals, compressed_params_list);

    // Evaluate the compressed error
    double compressed_error = evaluateCompressedError(
        expression_str, intervals, compressed_params_list);

    std::cout << "Compressed Error: " << compressed_error << std::endl;

    // Save parameters if needed
    saveCompressedParametersToFile(compressed_params_list, "gelu_compressed_params.csv");

    return 0;
}