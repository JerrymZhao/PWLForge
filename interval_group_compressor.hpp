// interval_group_compressor.hpp

#ifndef INTERVAL_GROUP_COMPRESSOR_HPP
#define INTERVAL_GROUP_COMPRESSOR_HPP

#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <iostream>
#include "interval_optimizer.hpp"

// Define the IntervalGroup structure
struct IntervalGroup {
    double length; // All intervals in the group have the same length
    Interval base_interval; // base interval
    std::vector<double> delta_starts; // Delta with the start point of the base interval
    std::vector<double> delta_ends; // Delta with the end point of the base interval

    IntervalGroup() : length(0.0), base_interval(Interval()), delta_starts(), delta_ends() {}
};

// Grouping the same length intervals and delta encoding
inline void groupAndCompressIntervals(const std::vector<Interval>& intervals, std::vector<IntervalGroup>& groups, double tolerance = 1e-9) {
    // Sorting the intervals by length
    std::vector<Interval> sorted_intervals = intervals;
    std::sort(sorted_intervals.begin(), sorted_intervals.end(), [&](const Interval& a, const Interval& b) -> bool {
        double len_a = a.end - a.start;
        double len_b = b.end - b.start;
        if (std::abs(len_a - len_b) < tolerance)
            return a.start < b.start; // Start point as tie-breaker
        return len_a < len_b;
    });
    
    // Grouping intervals by length
    std::vector<std::vector<Interval>> grouped_intervals;
    
    if (!sorted_intervals.empty()) {
        std::vector<Interval> current_group;
        current_group.push_back(sorted_intervals[0]);
        double current_length = sorted_intervals[0].end - sorted_intervals[0].start;
        
        for (size_t i = 1; i < sorted_intervals.size(); ++i) {
            double len = sorted_intervals[i].end - sorted_intervals[i].start;
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
    
    // Delta encoding for each group
    for (const auto& group : grouped_intervals) {
        if (group.empty()) continue;
        
        IntervalGroup interval_group;
        interval_group.length = group[0].end - group[0].start;
        interval_group.base_interval = group[0];
        
        // Sorting the group by start points
        std::vector<Interval> sorted_group = group;
        std::sort(sorted_group.begin(), sorted_group.end(), [&](const Interval& a, const Interval& b) -> bool {
            return a.start < b.start;
        });
        
        // Delta encoding: start and end points
        double prev_start = sorted_group[0].start;
        double prev_end = sorted_group[0].end;
        
        for (size_t i = 1; i < sorted_group.size(); ++i) {
            double delta_start = sorted_group[i].start - prev_start;
            double delta_end = sorted_group[i].end - prev_end;
            interval_group.delta_starts.push_back(delta_start);
            interval_group.delta_ends.push_back(delta_end);
            prev_start = sorted_group[i].start;
            prev_end = sorted_group[i].end;
        }
        
        groups.push_back(interval_group);
    }
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
            
            // Save DeltaStarts
            file << "\"";
            for (size_t i = 0; i < group.delta_starts.size(); ++i) {
                file << group.delta_starts[i];
                if (i != group.delta_starts.size() - 1) {
                    file << ";";
                }
            }
            file << "\",";
            
            // Save DeltaEnds
            file << "\"";
            for (size_t i = 0; i < group.delta_ends.size(); ++i) {
                file << group.delta_ends[i];
                if (i != group.delta_ends.size() - 1) {
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
