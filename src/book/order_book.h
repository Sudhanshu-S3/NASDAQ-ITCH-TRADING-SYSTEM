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
     *
     * Correctness first. Every container here is chosen to be obviously right and
     * easy to reason about, not fast. Phase 2 replaces whichever ones the profile
     * says are hot, and it can only do that safely because this version is proven
     * first and its numbers are recorded as the baseline.
     *
     * Target complexity, stated before building so it can be missed:
     *   add            O(log L)  std::map insert
     *   execute        O(1) average lookup, plus the level update
     *   cancel         O(1) average lookup, plus the level update
     *   remove         O(1) average lookup, plus the level update
     *   best bid/ask   O(1)      std::map::begin on an ordered container
     *
     * Bids are ordered descending so that begin() is the best on both sides. Two
     * maps rather than one keyed on side, because a comparator cannot vary per key
     * and getting best bid and best ask out of one ordered container means one of
     * them is a reverse iterator, which is the asymmetry that breeds off by ones.
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

        // Maintained only in the order update paths.
        std::uint64_t order_shares_ = 0;

        // Maintained only in the level update paths. Must always equal the pair
        // above; check_invariants_fast is exactly that comparison, which is what
        // makes it constant time and callable after every message.
        std::uint64_t level_shares_ = 0;
        std::uint64_t level_orders_ = 0;
    };
}  // namespace nts::book
