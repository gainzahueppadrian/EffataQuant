#pragma once
#include <Eigen/Dense>
#include <Eigen/Cholesky>
#include <vector>
#include <cmath>
#include <limits>
#include <iostream>

#define FORCE_INLINE __attribute__((always_inline)) inline

namespace effata::ml {

using namespace Eigen;

class RobustHMM {
public:
    RobustHMM(int n_states, int n_features)
        : N_(n_states), D_(n_features),
          means_(MatrixXd::Zero(n_states, n_features)),
          trans_(MatrixXd::Zero(n_states, n_states)),
          init_(VectorXd::Zero(n_states)) {
        trans_.setConstant(1.0 / n_states);
        init_.setConstant(1.0 / n_states);
        covs_.resize(n_states);
        cholesky_L_.resize(n_states);
        log_det_.resize(n_states);
        for (auto& C : covs_) C = MatrixXd::Identity(n_features, n_features);
        recompute_decompositions();
    }

    void recompute_decompositions() noexcept {
        for (int i = 0; i < N_; ++i) {
            MatrixXd C = covs_[i];
            for (int it = 0; it < 5 && C.ldlt().info() != Eigen::Success; ++it) {
                C.diagonal().array() += 1e-5 * (it + 1);
            }
            Eigen::LLT<MatrixXd> llt(C);
            cholesky_L_[i] = llt.matrixL();
            log_det_[i] = 0.0;
            for (int d = 0; d < D_; ++d) log_det_[i] += 2.0 * std::log(cholesky_L_[i](d,d));
            covs_[i] = C;
        }
    }

    FORCE_INLINE double log_gaussian(int state, const VectorXd& x) const noexcept {
        VectorXd diff = x - means_.row(state).transpose();
        VectorXd y = cholesky_L_[state].triangularView<Lower>().solve(diff);
        return -0.5 * (D_ * std::log(2.0 * M_PI) + log_det_[state] + y.squaredNorm());
    }

    std::vector<int> decode(const MatrixXd& obs) const noexcept {
        const int T = obs.rows();
        if (T == 0) return {};
        MatrixXd delta(T, N_);
        MatrixXi psi(T, N_);
        MatrixXd log_A = trans_.array().log().matrix();

        for (int i = 0; i < N_; ++i)
            delta(0,i) = std::log(init_(i) + 1e-300) + log_gaussian(i, obs.row(0).transpose());

        for (int t = 1; t < T; ++t) {
            for (int j = 0; j < N_; ++j) {
                VectorXd cand = delta.row(t-1).transpose() + log_A.col(j);
                int best_i;
                delta(t,j) = cand.maxCoeff(&best_i) + log_gaussian(j, obs.row(t).transpose());
                psi(t,j) = best_i;
            }
        }
        std::vector<int> path(T);
        delta.row(T-1).transpose().maxCoeff(&path[T-1]);
        for (int t = T-2; t >= 0; --t) path[t] = psi(t+1, path[t+1]);
        return path;
    }

    int filter_step(int prev_state, const VectorXd& obs) const noexcept {
        int best = 0;
        double best_score = -std::numeric_limits<double>::infinity();
        for (int j = 0; j < N_; ++j) {
            double score = std::log(trans_(prev_state, j) + 1e-300) + log_gaussian(j, obs);
            if (score > best_score) { best_score = score; best = j; }
        }
        return best;
    }

private:
    int N_, D_;
    MatrixXd means_;
    std::vector<MatrixXd> covs_;
    std::vector<MatrixXd> cholesky_L_;
    VectorXd log_det_;
    MatrixXd trans_;
    VectorXd init_;
};

} // namespace effata::ml
