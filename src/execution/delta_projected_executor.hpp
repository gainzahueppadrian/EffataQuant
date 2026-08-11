// effata_v33/execution/delta_projected_executor.hpp
#pragma once
#include <array>
#include <cmath>
#include <expected>
#include "../core/lock_free_ring_buffer.hpp"

namespace effata::execution {

struct MultiLegOrder {
    std::string strategy_name; // ej. "CALL_DEBIT_SPREAD"
    double underlying_price;
    double target_profit_dollars;
    double max_loss_dollars;
    double net_delta;
    double net_theta;
    int quantity;
};

/**
 * Executor que pre-calcula los niveles exactos de salida (Take Profit / Stop Loss)
 * basados en el Delta Neto, eliminando la necesidad de cálculo manual en tiempo real.
 */
class DeltaProjectedExecutor {
private:
    core::LockFreeRingBuffer<MultiLegOrder, 1024> order_queue_;

public:
    /**
     * Calcula cuántos puntos debe moverse el subyacente para alcanzar el objetivo de P&L.
     * Basado en la explicación del video: "Si el delta neto es 15, necesito 2 puntos para ganar $30".
     * Effata lo hace en O(1) con validación de Theta decay.
     */
    [[nodiscard]] std::expected<std::array<double, 2>, std::string> calculate_exit_levels(
        const MultiLegOrder& order,
        double current_underlying_price,
        double estimated_theta_decay_per_point) const {

        if (std::abs(order.net_delta) < 0.01) {
            return std::unexpected("Delta neto demasiado bajo para proyección lineal");
        }

        // Puntos necesarios para el Take Profit
        // Ajustamos el delta efectivo restando el impacto estimado del theta por punto de movimiento
        double effective_delta = order.net_delta - estimated_theta_decay_per_point;

        if (effective_delta <= 0.0) {
            return std::unexpected("Theta decay supera la ventaja del delta. Operación no viable.");
        }

        double points_to_profit = order.target_profit_dollars / (effective_delta * order.quantity * 100.0);
        double points_to_max_loss = order.max_loss_dollars / (effective_delta * order.quantity * 100.0);

        std::array<double, 2> exit_levels;
        exit_levels[0] = current_underlying_price + points_to_profit;  // Take Profit Level
        exit_levels[1] = current_underlying_price - points_to_max_loss; // Stop Loss Level

        return exit_levels;
    }

    void submit_order(const MultiLegOrder& order) {
        order_queue_.try_push(order);
        // En producción: Aquí se dispara la orden FIX/IBKR a través del ring buffer
    }
};

} // namespace effata::execution
