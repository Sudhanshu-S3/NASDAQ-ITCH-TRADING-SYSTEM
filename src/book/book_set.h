#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string_view>
#include <vector>

#include "book/order_book.h"
#include "itch/messages.h"

namespace nts::book
{
    /**
     * All symbols, indexed by ITCH stock locate.
     *
     * Stock locate is a small dense integer the exchange assigns for the day, so
     * indexing by it directly is O(1) with no hashing at all. That is the one place
     * in phase 1 where the obviously correct structure is also the fast one, because
     * NASDAQ chose the key to make it so.
     *
     * Locates are 1 based, so index 0 is allocated and never used. Directory messages
     * are not assumed to arrive in locate order and the range is not assumed dense.
     *
     * The books live in a std::deque, not a std::vector, and that is load bearing.
     * The table grows as directory messages arrive, and a vector reallocates on
     * growth, which invalidates every reference previously returned by book(). That
     * is a use after free, confirmed under AddressSanitizer, not a theoretical
     * concern. A deque growing at the back keeps references to existing elements
     * valid, so a caller may hold an OrderBook& across a directory message.
     *
     * The symbol is carried only so that output can name a book. Nothing routes by
     * symbol: routing is by locate, always, because a string compare per message
     * would dominate everything else in the parser.
     */
    class BookSet
    {
    public:
        /** 'R'. Registers a locate and its symbol. Must precede any order for it. */
        void on_stock_directory(const itch::StockDirectory& msg);

        /** Undefined for an unregistered locate. Check known() first. */
        [[nodiscard]] OrderBook&       book(std::uint16_t locate);
        [[nodiscard]] const OrderBook& book(std::uint16_t locate) const;

        /** Space padded to 8, as it arrived. Empty for an unregistered locate. */
        [[nodiscard]] std::string_view symbol(std::uint16_t locate) const;

        [[nodiscard]] bool known(std::uint16_t locate) const noexcept;

        /** Number of registered symbols, not the size of the underlying table. */
        [[nodiscard]] std::size_t size() const noexcept;

        /** Total resting quantity across every book. For the conservation check. */
        [[nodiscard]] std::uint64_t resting_shares() const noexcept;

        /**
         * O(books). Every book's constant time check.
         *
         * An end of run sweep, not a per message call. A message can only break the
         * book it was applied to, so the per message check belongs on that one book.
         * Calling this per message is what turns a constant time check into a
         * quadratic run, and it was measured doing exactly that.
         */
        [[nodiscard]] bool check_invariants_fast() const noexcept;

        /** O(live orders). Full rebuild on every registered book. */
        [[nodiscard]] bool check_invariants() const;

    private:
        std::deque<OrderBook>    books_;
        std::vector<itch::Stock> symbols_;
        std::vector<bool>        known_;
        std::size_t              registered_ = 0;
    };
}  // namespace nts::book
