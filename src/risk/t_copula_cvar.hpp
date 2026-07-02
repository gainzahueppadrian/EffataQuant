#pragma once

#include <Eigen/Dense>
#include <vector>
#include <random>
#include <cmath>

#define HOT [[gnu::hot]]
#define ALWAYS_INLINE [[gnu::always_inline]] inline

namespace berkshire::risk {

using namespace Eigen;

/**
 * @brief Student-t Copula for Systemic Contagion & Tail Risk evaluation.
 * Correctly models tail-dependence (when everything drops at once during crashes),
 * completely replacing the flawed Gaussian Copula standard.
 */
class TCopulaCVaR {
public:
    TCopulaCVaR(int dim, double degrees_of_freedom)
        : dim_(dim), nu_(degrees_of_freedom), rng_(std::random_device{}())
    {}

    /**
     * @brief Generates correlated multivariate Student-t samples to model systemic tail drops.
     * @param correlation_matrix The asset correlation matrix
     * @param num_samples Number of MC paths to generate
     */
    HOT MatrixXd generate_tail_samples(const MatrixXd& correlation_matrix, int num_samples) {
        // 1. Cholesky Decomposition of correlation matrix
        LLT<MatrixXd> llt(correlation_matrix);
        MatrixXd L = llt.matrixL();

        MatrixXd samples(num_samples, dim_);

        std::normal_distribution<double> norm_dist(0.0, 1.0);
        std::chi_squared_distribution<double> chi_sq_dist(nu_);

        for (int i = 0; i < num_samples; ++i) {
            // Generate independent normal vector Z
            VectorXd Z(dim_);
            for (int j = 0; j < dim_; ++j) {
                Z(j) = norm_dist(rng_);
            }

            // Generate chi-squared scalar W
            double W = chi_sq_dist(rng_);

            // Correlate and scale to form Student-t vector
            // X = \sqrt{\nu / W} * L * Z
            double scale = std::sqrt(nu_ / W);
            VectorXd X = scale * (L * Z);

            samples.row(i) = X.transpose();
        }

        return samples;
    }

private:
    int dim_;
    double nu_; // Degrees of freedom. Lower = fatter tails.
    std::mt19937_64 rng_;
};

} // namespace berkshire::risk
