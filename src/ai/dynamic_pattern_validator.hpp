// effata_v33/ai/dynamic_pattern_validator.hpp
#pragma once
#include <vector>
#include <string>
#include <expected>
#include <xgboost/c_api.h>

namespace effata::ai {

enum class ChartPattern {
    HEAD_AND_SHOULDERS,      // HCH (Base Bulkowski: 83%)
    RECTANGLE,               // Rectángulo (Base: 78%)
    DOUBLE_BOTTOM,           // Doble Suelo (Base: 78%)
    PENNANT,                 // Penant (Base: 50% - Moneda al aire)
    UNKNOWN
};

struct MarketContextFeatures {
    double vix_level;
    double gex_net;
    double iv_rank;
    double volume_ratio;
    double trend_strength; // ADX o similar
};

class DynamicPatternValidator {
private:
    BoosterHandle xgb_model_;

    // Tasas base históricas (Bulkowski)
    std::unordered_map<ChartPattern, double> base_probabilities_ = {
        {ChartPattern::HEAD_AND_SHOULDERS, 0.83},
        {ChartPattern::RECTANGLE, 0.78},
        {ChartPattern::DOUBLE_BOTTOM, 0.78},
        {ChartPattern::PENNANT, 0.50}
    };

public:
    DynamicPatternValidator(const std::string& model_path) {
        XGBoosterCreate(nullptr, 0, &xgb_model_);
        XGBoosterLoadModel(xgb_model_, model_path.c_str());
    }

    ~DynamicPatternValidator() {
        if (xgb_model_) XGBoosterFree(xgb_model_);
    }

    /**
     * Ajusta la probabilidad histórica de Bulkowski según el contexto de mercado actual.
     * Ejemplo: Un HCH tiene 83% de éxito en general, pero si GEX es fuertemente negativo,
     * la probabilidad cae al 45% debido al riesgo de dealer hedging.
     */
    [[nodiscard]] std::expected<double, std::string> get_adjusted_probability(
        ChartPattern pattern,
        const MarketContextFeatures& features) const {

        auto it = base_probabilities_.find(pattern);
        if (it == base_probabilities_.end()) {
            return std::unexpected("Patrón no reconocido");
        }

        double base_prob = it->second;

        // Vector de características para XGBoost
        std::vector<float> features_vec = {
            static_cast<float>(features.vix_level / 50.0),
            static_cast<float>(features.gex_net / 1e9), // Normalizado a billones
            static_cast<float>(features.iv_rank / 100.0),
            static_cast<float>(features.volume_ratio),
            static_cast<float>(features.trend_strength / 100.0)
        };

        DMatrixHandle dmatrix;
        XGDMatrixCreateFromMat(features_vec.data(), 1, features_vec.size(), NAN, &dmatrix);

        bst_ulong out_len;
        const float* out_result;
        XGBoosterPredict(xgb_model_, dmatrix, 0, 0, 0, &out_len, &out_result);

        // El modelo predice un "multiplicador de ajuste" (ej. 0.8 si el entorno es hostil)
        double adjustment_factor = std::clamp(out_result[0], 0.5, 1.2);
        double adjusted_prob = std::clamp(base_prob * adjustment_factor, 0.1, 0.95);

        XGDMatrixFree(dmatrix);

        // Regla Effata: Rechazar patrones de baja calidad (como Penants) a menos que el ML dé una señal extremadamente fuerte
        if (pattern == ChartPattern::PENNANT && adjusted_prob < 0.65) {
            return std::unexpected("Patrón de baja fiabilidad (Penant) rechazado por contexto");
        }

        return adjusted_prob;
    }
};

} // namespace effata::ai
