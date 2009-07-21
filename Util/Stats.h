// -*- C++ -*-
#ifndef UTIL_STATS_H
#define UTIL_STATS_H

#ifndef __cplusplus
#error This header expects to be included only in C++ source
#endif

#include "llvm/Support/MutexGuard.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <numeric>
#include <vector>

// Calculate the median of a given data set. This assumes that the data is
// sorted.
template<typename ValueTy>
ValueTy
Median(const std::vector<ValueTy> &data)
{
    size_t mid_point = data.size() / 2;
    if (data.size() % 2 == 0) {
        ValueTy first = data[mid_point];
        ValueTy second = data[mid_point - 1];
        return (first + second) / 2;
    } else {
        return data[mid_point];
    }
}


// Base class useful for collecting stats on vectors of individual data points.
// This is intended to be used with llvm::ManagedStatic and will print
// min, median, mean, max and sum statistics about the data vector when the
// process shuts down.
template<typename ValueTy>
class DataVectorStats {
public:
    typedef std::vector<ValueTy> DataType;

    // Append a new data point to the vector. This is thread-safe.
    void RecordDataPoint(ValueTy data_point) {
        llvm::MutexGuard locked(this->lock_);
        this->data_.push_back(data_point);
    }

    DataVectorStats(const char *const name) : name_(name) {}

    ~DataVectorStats() {
        DataType data = this->data_;
        if (data.size() == 0)
            return;

        llvm::errs() << "\n" << this->name_ << " (n=" << data.size() << "):\n";
        std::sort(data.begin(), data.end());
        ValueTy sum = std::accumulate(data.begin(), data.end(), 0);
        llvm::errs() << "Min: " << data[0] << "\n";
        llvm::errs() << "Median: " << Median(data) << "\n";
        llvm::errs() << "Mean: " << sum / data.size() << "\n";
        llvm::errs() << "Max: " << *(data.end() - 1) << "\n";
        llvm::errs() << "Sum: " << sum << "\n";
    }

private:
    const char *const name_;
    llvm::sys::Mutex lock_;
    DataType data_;
};

#endif  // UTIL_STATS_H
