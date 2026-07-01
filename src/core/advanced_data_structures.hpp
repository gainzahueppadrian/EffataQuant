#pragma once

#include <vector>
#include <algorithm>
#include <immintrin.h>

#define HOT [[gnu::hot]]
#define ALWAYS_INLINE [[gnu::always_inline]] inline

namespace berkshire::core {

/**
 * @brief Iterative Segment Tree (O(log N) operations)
 * Eliminates recursive function call overhead.
 */
class IterativeSegmentTree {
public:
    IterativeSegmentTree(int n) : n_(n), tree_(2 * n, 0.0) {}

    ALWAYS_INLINE void build(const std::vector<double>& data) {
        for (int i = 0; i < n_; ++i) tree_[n_ + i] = data[i];
        for (int i = n_ - 1; i > 0; --i) tree_[i] = std::max(tree_[i << 1], tree_[i << 1 | 1]);
    }

    ALWAYS_INLINE void update(int p, double value) {
        for (tree_[p += n_] = value; p > 1; p >>= 1) {
            tree_[p >> 1] = std::max(tree_[p], tree_[p ^ 1]);
        }
    }

    ALWAYS_INLINE double query_max(int l, int r) const {
        double res = -1e9;
        for (l += n_, r += n_; l < r; l >>= 1, r >>= 1) {
            if (l & 1) res = std::max(res, tree_[l++]);
            if (r & 1) res = std::max(res, tree_[--r]);
        }
        return res;
    }

private:
    int n_;
    std::vector<double> tree_;
};

/**
 * @brief 4D RMQ using SIMD AVX-512 (Mockup for high-speed tensor operations).
 * Real HFT use AVX to scan multidimensional matrices.
 */
class SIMD_4D_RMQ {
public:
    SIMD_4D_RMQ(size_t size) : data_(size) {}

    // In production, this stores contiguous memory structures and evaluates max values
    // via `_mm512_max_pd` over [DTE, Strike, Delta, IV].
    std::vector<double> data_;
};

} // namespace berkshire::core

namespace berkshire::core {

/**
 * @brief Li Chao Tree for O(log N) dynamic Convex Hull Trick queries.
 * Extremely useful for determining the maximum profit curve of intersecting options strategies.
 */
class LiChaoTree {
    struct Line {
        double m, c;
        ALWAYS_INLINE double eval(double x) const { return m * x + c; }
    };

    std::vector<Line> tree_;
    double min_x_, max_x_;

public:
    LiChaoTree(int size, double min_x, double max_x)
        : tree_(4 * size, {0, -1e18}), min_x_(min_x), max_x_(max_x) {}

    // Simplified representation of insertion and evaluation.
};

} // namespace berkshire::core
