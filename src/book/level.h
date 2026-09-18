#pragma once

#include <cstdint>

namespace nts::book
{
    enum class Side : char
    {
        Buy  = 'B',
        Sell = 'S',
    };

    /** One price level, aggregated. */
    struct Level
    {
        std::uint32_t price  = 0;  ///< four implied decimals
        std::uint64_t shares = 0;
        std::uint32_t orders = 0;
    };
}  // namespace nts::book
