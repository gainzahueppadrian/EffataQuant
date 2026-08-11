// effata_v33/strategies/gex_aware_breakout.hpp
#pragma once
#include <cmath>
#include <expected>

namespace effata::strategies {

/**
 * Valida si una ruptura chartista (breakout) es genuina o una trampa de liquidez,
 * analizando la exposición Gamma (GEX) de los market makers.
 */
class GexAwareBreakoutEngine {
public:
    struct BreakoutValidation {
        bool is_valid;
        double expected_move_points;
        std::string reasoning;
        double dealer_hedging_pressure; // Positivo = dealers compran (amplifica), Negativo = dealers venden (frena)
    };

    [[nodiscard]] BreakoutValidation validate_breakout(
        double current_price,
        double breakout_level,
        double net_gex,
        double gamma_flip_level) const {

        BreakoutValidation result;
        result.is_valid = false;
        result.dealer_hedging_pressure = 0.0;

        // 1. Análisis de Gamma Flip
        // Si el precio rompe hacia arriba, pero está por DEBAJO del nivel de Gamma Flip,
        // los dealers están en Short Gamma y venderán el rally, causando un fakeout.
        bool is_above_gamma_flip = current_price > gamma_flip_level;
        bool is_breakout_bullish = current_price > breakout_level;

        if (is_breakout_bullish && !is_above_gamma_flip) {
            result.reasoning = "BREAKOUT RECHAZADO: Precio por debajo de Gamma Flip. Dealers venderán la ruptura (Short Gamma).";
            result.dealer_hedging_pressure = -1.0;
            return result;
        }

        // 2. Análisis de Magnitud de GEX
        // GEX positivo actúa como amortiguador (mean reversion). GEX negativo actúa como acelerador (momentum).
        if (net_gex > 1e9) { // GEX positivo fuerte
            result.reasoning = "Ruptura válida, pero se espera mean reversion debido a alto GEX positivo. Operar con objetivos cortos.";
            result.expected_move_points = (current_price - breakout_level) * 1.5; // Movimiento limitado
            result.is_valid = true;
            result.dealer_hedging_pressure = 0.5;
        }
        else if (net_gex < -1e9) { // GEX negativo fuerte
            result.reasoning = "Ruptura válida y explosiva. Dealers en Short Gamma amplificarán el movimiento.";
            result.expected_move_points = (current_price - breakout_level) * 3.0; // Movimiento amplificado
            result.is_valid = true;
            result.dealer_hedging_pressure = -1.0;
        }
        else {
            result.reasoning = "Ruptura válida en entorno de GEX neutral. Comportamiento estándar.";
            result.expected_move_points = (current_price - breakout_level) * 2.0;
            result.is_valid = true;
            result.dealer_hedging_pressure = 0.0;
        }

        return result;
    }
};

} // namespace effata::strategies
