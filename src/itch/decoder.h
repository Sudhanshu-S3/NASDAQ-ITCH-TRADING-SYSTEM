#pragma once

#include <cstddef>

#include "itch/messages.h"

namespace nts::itch
{
    /**
     * Turns the bytes of one framed message into a decoded struct.
     */

    [[nodiscard]] Header decode_header(const std::byte* p) noexcept;

    [[nodiscard]] SystemEvent    decode_system_event(const std::byte* p) noexcept;
    [[nodiscard]] StockDirectory decode_stock_directory(const std::byte* p) noexcept;

    /** Handles both 'A' and 'F'; the 4 byte MPID at offset 36 on 'F' is skipped. */
    [[nodiscard]] AddOrder decode_add_order(const std::byte* p) noexcept;

    /** Handles both 'E' and 'C'; price and printable are set only for 'C'. */
    [[nodiscard]] OrderExecuted decode_order_executed(const std::byte* p) noexcept;

    [[nodiscard]] OrderCancel  decode_order_cancel(const std::byte* p) noexcept;
    [[nodiscard]] OrderDelete  decode_order_delete(const std::byte* p) noexcept;
    [[nodiscard]] OrderReplace decode_order_replace(const std::byte* p) noexcept;

    /**
     * Handles both 'P' and 'Q'. They have different layouts, so this one reads the
     * type byte to choose. A cross has no resting order and no side, which is what
     * has_order records.
     */
    [[nodiscard]] Trade decode_trade(const std::byte* p) noexcept;
}  // namespace nts::itch
