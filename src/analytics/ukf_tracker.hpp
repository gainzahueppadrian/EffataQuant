#pragma once

#include <Eigen/Dense>
#include <vector>

#define HOT [[gnu::hot]]
#define ALWAYS_INLINE [[gnu::always_inline]] inline

namespace berkshire::analytics {

using namespace Eigen;

/**
 * @brief Unscented Kalman Filter (UKF)
 * Tracks highly non-linear financial time series (like Implied Volatility surface dynamics)
 * without utilizing unstable Jacobian approximations found in Extended Kalman Filters (EKF).
 */
class UnscentedKalmanFilter {
public:
    UnscentedKalmanFilter(int state_dim, int obs_dim)
        : n_(state_dim), m_(obs_dim)
    {
        // Tuning parameters for unscented transform
        alpha_ = 1e-3;
        kappa_ = 0.0;
        beta_  = 2.0; // Optimal for Gaussian distributions
        lambda_ = alpha_ * alpha_ * (n_ + kappa_) - n_;

        int num_sigma = 2 * n_ + 1;
        Wm_.resize(num_sigma);
        Wc_.resize(num_sigma);

        Wm_(0) = lambda_ / (n_ + lambda_);
        Wc_(0) = Wm_(0) + (1.0 - alpha_ * alpha_ + beta_);

        for (int i = 1; i < num_sigma; ++i) {
            Wm_(i) = 1.0 / (2.0 * (n_ + lambda_));
            Wc_(i) = Wm_(i);
        }

        // State initialization
        x_ = VectorXd::Zero(n_);
        P_ = MatrixXd::Identity(n_, n_);
        Q_ = MatrixXd::Identity(n_, n_) * 1e-5; // Process noise
        R_ = MatrixXd::Identity(m_, m_) * 1e-3; // Observation noise
    }

    /**
     * @brief Predict step: Propagates state and covariance through unscented transform.
     */
    HOT void predict() {
        int num_sigma = 2 * n_ + 1;
        MatrixXd sigmas = compute_sigma_points();

        // 1. Predict sigma points (Assuming simple random walk state transition for IV)
        // In production, this would pass through a non-linear transition function f(x)
        MatrixXd sigmas_pred = sigmas;

        // 2. Compute predicted state mean
        x_.setZero();
        for (int i = 0; i < num_sigma; ++i) {
            x_ += Wm_(i) * sigmas_pred.col(i);
        }

        // 3. Compute predicted covariance
        P_.setZero();
        for (int i = 0; i < num_sigma; ++i) {
            VectorXd diff = sigmas_pred.col(i) - x_;
            P_ += Wc_(i) * (diff * diff.transpose());
        }
        P_ += Q_;
    }

    /**
     * @brief Update step: Incorporates new non-linear observations.
     */
    HOT void update(const VectorXd& z) {
        int num_sigma = 2 * n_ + 1;
        MatrixXd sigmas = compute_sigma_points();

        // 1. Map sigma points to observation space (Assuming linear map for simplicity here,
        // but would be h(x) non-linear in full implementation).
        MatrixXd Z_sigmas(m_, num_sigma);
        for(int i=0; i < num_sigma; ++i) {
            Z_sigmas.col(i) = sigmas.col(i).head(m_); // h(x) mapping
        }

        // 2. Observation Mean
        VectorXd z_pred = VectorXd::Zero(m_);
        for (int i = 0; i < num_sigma; ++i) {
            z_pred += Wm_(i) * Z_sigmas.col(i);
        }

        // 3. Observation Covariance and Cross Covariance
        MatrixXd S = MatrixXd::Zero(m_, m_);
        MatrixXd C = MatrixXd::Zero(n_, m_);
        for (int i = 0; i < num_sigma; ++i) {
            VectorXd z_diff = Z_sigmas.col(i) - z_pred;
            VectorXd x_diff = sigmas.col(i) - x_;
            S += Wc_(i) * (z_diff * z_diff.transpose());
            C += Wc_(i) * (x_diff * z_diff.transpose());
        }
        S += R_;

        // 4. Kalman Gain
        MatrixXd K = C * S.inverse();

        // 5. State Update
        x_ += K * (z - z_pred);
        P_ -= K * S * K.transpose();
    }

    VectorXd get_state() const { return x_; }

private:
    int n_, m_;
    double alpha_, kappa_, beta_, lambda_;
    VectorXd Wm_, Wc_;

    VectorXd x_; // State vector
    MatrixXd P_; // State covariance
    MatrixXd Q_; // Process noise
    MatrixXd R_; // Observation noise

    ALWAYS_INLINE MatrixXd compute_sigma_points() const {
        MatrixXd sigmas(n_, 2 * n_ + 1);
        sigmas.col(0) = x_;

        // Cholesky decomposition of scaled covariance
        LLT<MatrixXd> llt((n_ + lambda_) * P_);
        MatrixXd L = llt.matrixL();

        for (int i = 0; i < n_; ++i) {
            sigmas.col(i + 1)      = x_ + L.col(i);
            sigmas.col(i + 1 + n_) = x_ - L.col(i);
        }
        return sigmas;
    }
};

} // namespace berkshire::analytics
