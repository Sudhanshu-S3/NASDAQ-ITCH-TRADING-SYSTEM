#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>

namespace nts::book
{
    enum class Side : char
    {
        Buy  = 'B',
        Sell = 'S',
    };

    enum class ApplyResult : std::uint8_t
    {
        Applied,
        UnknownRef,
        DuplicateRef,
        ClampedReduce,
    };

    /** One price level, aggregated. */
    struct Level
    {
        std::uint32_t price  = 0;  ///< four implied decimals
        std::uint64_t shares = 0;
        std::uint32_t orders = 0;
    };

    /**
     * The displayed limit order book for one symbol.
     */
    class OrderBook
    {
    public:
        /** Ignored if ref is already live, since accepting it would double count. */
        [[nodiscard]] ApplyResult add(std::uint64_t ref,
                 Side          side,
                 std::uint32_t shares,
                 std::uint32_t price);

        /**
         * Reduces an order by a partial quantity. Used by both execute ('E', 'C') and
         * cancel ('X'), which differ in what they mean, not in what they do here.
         */
        [[nodiscard]] ApplyResult reduce(std::uint64_t ref,
                    std::uint32_t shares);

        /** 'D'. Removes the whole remaining quantity. */
        [[nodiscard]] ApplyResult remove(std::uint64_t ref);

        /**
         * 'U'. Delete the original, then add the new reference at the new price and
         * quantity, carrying side over from the original.
         */
        [[nodiscard]] ApplyResult replace(std::uint64_t original_ref,
                     std::uint64_t new_ref,
                     std::uint32_t shares,
                     std::uint32_t price);

        [[nodiscard]] std::optional<Level> best_bid() const;
        [[nodiscard]] std::optional<Level> best_ask() const;

        [[nodiscard]] std::size_t live_orders() const noexcept;
        

        /**
         * Rebuilds both sides from the order index and compares against the stored
         * levels. Debug builds only, and called per message there.
         *
         * Best bid strictly below best ask is NOT checked, because a locked or crossed
         * book is legal transiently.
         */

        [[nodiscard]] bool check_invariants_fast() const noexcept;
        [[nodiscard]] bool check_invariants() const;

    private:
        struct Order
        {
            std::uint32_t shares = 0;
            std::uint32_t price  = 0;
            Side          side   = Side::Buy;
        };

        std::unordered_map<std::uint64_t, Order> orders_;

        std::map<std::uint32_t, Level, std::greater<>> bids_;
        std::map<std::uint32_t, Level>                 asks_;
    };
}  // namespace nts::book
