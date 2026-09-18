#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "book/level.h"
#include "book/price_levels.h"
#include "common/flat_hash_map.h"

namespace nts::book
{
    enum class ApplyResult : std::uint8_t
    {
        Applied,
        UnknownRef,
        DuplicateRef,
        ClampedReduce,
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

        /** Remaining quantity of a live order, or 0 if the reference is not live. */
        [[nodiscard]] std::uint32_t shares_of(std::uint64_t ref) const noexcept;

        /**
         * Total resting quantity across both sides, maintained incrementally.
         *
         * The driver's conservation check compares this against what the message
         * stream said should be here: added minus executed minus cancelled minus
         * deleted. That ties every message to the state it produced, and no share
         * field can be misdecoded without breaking it.
         */
        [[nodiscard]] std::uint64_t resting_shares() const noexcept;

        /**
         * Rebuilds both sides from the order index and compares against the stored
         * levels. Debug builds only, and called per message there.
         *
         * Best bid strictly below best ask is NOT checked, because a locked or crossed
         * book is legal transiently.
         */

        [[nodiscard]] bool check_invariants_fast() const noexcept;
        [[nodiscard]] bool check_invariants() const;

        [[nodiscard]] std::size_t index_rehashes() const noexcept;

        [[nodiscard]] std::size_t rebases() const noexcept;

        /** Allocates both cell arrays now, so the first add pays no page faults. */
        void reserve();

    private:
        OrderIndex  orders_;
        PriceLevels bids_{Side::Buy, 0, PriceLevels::kDefaultSpan};
        PriceLevels asks_{Side::Sell, 0, PriceLevels::kDefaultSpan};

        // Maintained only in the order update paths.
        std::uint64_t order_shares_ = 0;

        std::uint64_t level_shares_ = 0;
        std::uint64_t level_orders_ = 0;
    };
}  // namespace nts::book
