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
     * All symbols, indexed by ITCH stock locate, sharing one OrderStore.
     */
    class BookSet
    {
    public:
        static constexpr std::size_t kDefaultMaxLiveOrders = 2'000'000;

        explicit BookSet(std::size_t max_live_orders = kDefaultMaxLiveOrders);

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

        [[nodiscard]] const OrderStore& store() const noexcept
        {
            return store_;
        }

        void reserve_locates(std::size_t count);

        /** Phase 6. Switches every book, present and future, into top tracking. */
        void set_track_top(bool on) noexcept;

        /** Warms the index slot for an order reference that is about to be looked up. */
        void prefetch_order(std::uint64_t ref) const noexcept
        {
            store_.index.prefetch(ref);
        }

        [[nodiscard]] bool check_invariants_fast() const noexcept;

        [[nodiscard]] bool check_invariants() const;

    private:
        OrderStore               store_;
        std::deque<OrderBook>    books_;
        std::vector<itch::Stock> symbols_;
        std::vector<bool>        known_;
        std::size_t              registered_ = 0;
        bool                     track_top_  = false;
    };
}  // namespace nts::book
