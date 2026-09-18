#include "book/order_book.h"

#include <cassert>

namespace nts::book
{
    namespace
    {
        // Smallest power of two holding n entries under a 0.7 load factor, and never
        // fewer than 16 slots so tiny stores still probe sanely.
        std::size_t index_slots_for(std::size_t n) noexcept
        {
            const auto  needed = static_cast<std::size_t>(static_cast<double>(n) / 0.7) + 1;
            std::size_t slots  = 16;
            while (slots < needed)
            {
                slots <<= 1;
            }
            return slots;
        }
    }  // namespace

    // ---------------------------------------------------------------- OrderStore

    OrderStore::OrderStore(std::size_t max_live_orders)
        : index(index_slots_for(max_live_orders))
#ifdef NTS_S4_APPEND_ONLY
    {
        orders_.reserve(max_live_orders);
    }
#else
        , pool_(max_live_orders)
    {
    }
#endif

#ifdef NTS_S4_APPEND_ONLY
    std::uint32_t OrderStore::allocate() noexcept
    {
        if (orders_.size() >= 0xFFFFFFFEu)
        {
            return Pool<Order>::kNull;
        }
        orders_.emplace_back();
        ++live_;
        peak_ = live_ > peak_ ? live_ : peak_;
        return static_cast<std::uint32_t>(orders_.size() - 1);
    }

    void OrderStore::release(std::uint32_t) noexcept
    {
        --live_;
    }

    Order& OrderStore::at(std::uint32_t handle) noexcept
    {
        return orders_[handle];
    }

    const Order& OrderStore::at(std::uint32_t handle) const noexcept
    {
        return orders_[handle];
    }

    std::size_t OrderStore::live() const noexcept
    {
        return live_;
    }

    std::size_t OrderStore::peak_live() const noexcept
    {
        return peak_;
    }

    std::size_t OrderStore::capacity() const noexcept
    {
        return orders_.capacity();
    }
#else
    std::uint32_t OrderStore::allocate() noexcept
    {
        return pool_.allocate();
    }

    void OrderStore::release(std::uint32_t handle) noexcept
    {
        pool_.release(handle);
    }

    Order& OrderStore::at(std::uint32_t handle) noexcept
    {
        return pool_[handle];
    }

    const Order& OrderStore::at(std::uint32_t handle) const noexcept
    {
        return pool_[handle];
    }

    std::size_t OrderStore::live() const noexcept
    {
        return pool_.live();
    }

    std::size_t OrderStore::peak_live() const noexcept
    {
        return pool_.peak_live();
    }

    std::size_t OrderStore::capacity() const noexcept
    {
        return pool_.capacity();
    }
#endif

    // ----------------------------------------------------------------- OrderBook

    OrderBook::OrderBook()
        : owned_(std::make_unique<OrderStore>(kStandaloneCapacity))
        , store_(owned_.get())
        , locate_(0)
    {
    }

    OrderBook::OrderBook(OrderStore&   store,
                         std::uint16_t locate) noexcept
        : store_(&store)
        , locate_(locate)
    {
    }

    std::uint64_t OrderBook::top_key(Side side) const noexcept
    {
        const std::optional<Level> best = (side == Side::Buy) ? best_bid() : best_ask();
        if (!best)
        {
            return 0;
        }
        // Shares are capped far below 2^32 per level in practice, but the key only
        // needs to change when the top changes, so folding is fine.
        return (static_cast<std::uint64_t>(best->price) << 32) ^ best->shares ^
               (static_cast<std::uint64_t>(best->orders) << 20);
    }

    ApplyResult OrderBook::add(std::uint64_t ref,
                               Side          side,
                               std::uint32_t shares,
                               std::uint32_t price)
    {
        top_changed_ = false;
        const std::uint64_t before = track_top_ ? top_key(side) : 0;

        // Allocate before inserting, so a full store leaves the index untouched. The
        // order of the two checks matters: an insert that succeeded followed by a
        // failed allocate would need the insert undone, and a duplicate found after a
        // successful allocate would need the slot released. Duplicate first would be
        // an extra probe on the common path, so allocate goes first and is released
        // on the rarer failure.
        const std::uint32_t handle = store_->allocate();
        if (handle == Pool<Order>::kNull)
        {
            return ApplyResult::Exhausted;
        }

        // Refuse a duplicate rather than half applying it. Updating the level anyway
        // would credit it shares and an order count that no live order backs, and the
        // book would drift by exactly that amount with nothing reporting an error.
        if (!store_->index.insert(ref, handle))
        {
            store_->release(handle);
            return ApplyResult::DuplicateRef;
        }

        Order& order = store_->at(handle);
        order.shares = shares;
        order.price  = price;
        order.locate = locate_;
        order.side   = side;
        order.pad    = 0;
#ifndef NTS_ORDER_12
        order.reserved = 0;
#endif

        order_shares_ += shares;
        ++live_;

#ifdef NTS_LEVELS_MAP
        auto& level   = (side == Side::Buy) ? bids_[price] : asks_[price];
        level.price   = price;
        level.shares += shares;
        level.orders += 1;
#else
        (side == Side::Buy ? bids_ : asks_).add(price, shares);
#endif

        level_shares_ += shares;
        level_orders_ += 1;

        if (track_top_)
        {
            top_changed_ = (top_key(side) != before);
        }
        return ApplyResult::Applied;
    }

    void OrderBook::reduce_level(const Order&  order,
                                 std::uint32_t by,
                                 bool          order_dies)
    {
#ifdef NTS_LEVELS_MAP
        // The two maps have different comparators and therefore different types, so
        // the side switch stays explicit. A find that misses here is a level-versus-
        // index drift, which is what check_invariants_fast reports, so it is not
        // asserted away.
        if (order.side == Side::Buy)
        {
            const auto it = bids_.find(order.price);
            if (it == bids_.end())
            {
                return;
            }
            it->second.shares -= by;
            level_shares_     -= by;
            if (order_dies)
            {
                it->second.orders -= 1;
                level_orders_     -= 1;
                if (it->second.orders == 0)
                {
                    bids_.erase(it);
                }
            }
        }
        else
        {
            const auto it = asks_.find(order.price);
            if (it == asks_.end())
            {
                return;
            }
            it->second.shares -= by;
            level_shares_     -= by;
            if (order_dies)
            {
                it->second.orders -= 1;
                level_orders_     -= 1;
                if (it->second.orders == 0)
                {
                    asks_.erase(it);
                }
            }
        }
#else
        PriceLevels& side = (order.side == Side::Buy) ? bids_ : asks_;
        if (order_dies)
        {
            side.remove(order.price, by);
            level_orders_ -= 1;
        }
        else
        {
            side.reduce(order.price, by);
        }
        level_shares_ -= by;
#endif
    }

    ApplyResult OrderBook::reduce(std::uint64_t ref,
                                  std::uint32_t shares)
    {
        top_changed_               = false;
        const std::uint32_t handle = store_->index.find(ref);
        if (handle == OrderIndex::kMissing)
        {
            return ApplyResult::UnknownRef;
        }
        Order& order = store_->at(handle);

        if (order.locate != locate_)
        {
            return ApplyResult::UnknownRef;
        }

        const bool          clamped   = shares > order.shares;
        const std::uint32_t reduce_by = clamped ? order.shares : shares;
        const Side          side      = order.side;
        const std::uint64_t before    = track_top_ ? top_key(side) : 0;

        order.shares  -= reduce_by;
        order_shares_ -= reduce_by;

        const bool dies = (order.shares == 0);
        reduce_level(order, reduce_by, dies);

        if (dies)
        {
            store_->index.erase(ref);
            store_->release(handle);
            --live_;
        }

        if (track_top_)
        {
            top_changed_ = (top_key(side) != before);
        }
        return clamped ? ApplyResult::ClampedReduce : ApplyResult::Applied;
    }

    ApplyResult OrderBook::remove(std::uint64_t ref)
    {
        top_changed_               = false;
        const std::uint32_t handle = store_->index.find(ref);
        if (handle == OrderIndex::kMissing)
        {
            return ApplyResult::UnknownRef;
        }
        const Order& order = store_->at(handle);
        if (order.locate != locate_)
        {
            return ApplyResult::UnknownRef;
        }
        const Side          side   = order.side;
        const std::uint64_t before = track_top_ ? top_key(side) : 0;

        order_shares_ -= order.shares;
        reduce_level(order, order.shares, true);

        store_->index.erase(ref);
        store_->release(handle);
        --live_;

        if (track_top_)
        {
            top_changed_ = (top_key(side) != before);
        }
        return ApplyResult::Applied;
    }

    ApplyResult OrderBook::replace(std::uint64_t original_ref,
                                   std::uint64_t new_ref,
                                   std::uint32_t shares,
                                   std::uint32_t price)
    {
        const std::uint32_t handle = store_->index.find(original_ref);
        if (handle == OrderIndex::kMissing)
        {
            return ApplyResult::UnknownRef;
        }
        const Order& original = store_->at(handle);
        if (original.locate != locate_)
        {
            return ApplyResult::UnknownRef;
        }

        if (new_ref != original_ref && store_->index.find(new_ref) != OrderIndex::kMissing)
        {
            return ApplyResult::DuplicateRef;
        }

        // Read the side before the remove. It is the one field the replace message
        // does not carry, and after remove() the slot it came from is back in the pool.
        const Side side = original.side;

        const ApplyResult removed = remove(original_ref);
        if (removed != ApplyResult::Applied)
        {
            return removed;
        }
        const bool removed_changed_top = top_changed_;
        // Exhausted cannot happen here: the remove just freed a slot.
        const ApplyResult added = add(new_ref, side, shares, price);
        top_changed_ = top_changed_ || removed_changed_top;
        return added;
    }

#ifdef NTS_LEVELS_MAP
    std::optional<Level> OrderBook::best_bid() const
    {
        if (bids_.empty())
        {
            return std::nullopt;
        }
        return bids_.begin()->second;
    }

    std::optional<Level> OrderBook::best_ask() const
    {
        if (asks_.empty())
        {
            return std::nullopt;
        }
        return asks_.begin()->second;
    }

    void OrderBook::bids(std::vector<Level>& out) const
    {
        out.clear();
        for (const auto& [price, level] : bids_)
        {
            (void) price;
            out.push_back(level);
        }
    }

    void OrderBook::asks(std::vector<Level>& out) const
    {
        out.clear();
        for (const auto& [price, level] : asks_)
        {
            (void) price;
            out.push_back(level);
        }
    }

    std::size_t OrderBook::level_rebases() const noexcept
    {
        return 0;
    }
#else
    std::optional<Level> OrderBook::best_bid() const
    {
        return bids_.best();
    }

    std::optional<Level> OrderBook::best_ask() const
    {
        return asks_.best();
    }

    void OrderBook::bids(std::vector<Level>& out) const
    {
        bids_.levels(out);
    }

    void OrderBook::asks(std::vector<Level>& out) const
    {
        asks_.levels(out);
    }

    std::size_t OrderBook::level_rebases() const noexcept
    {
        return bids_.rebases() + asks_.rebases();
    }
#endif

    std::size_t OrderBook::live_orders() const noexcept
    {
        return live_;
    }

    std::uint32_t OrderBook::shares_of(std::uint64_t ref) const noexcept
    {
        const std::uint32_t handle = store_->index.find(ref);
        if (handle == OrderIndex::kMissing)
        {
            return 0;
        }
        const Order& order = store_->at(handle);
        return order.locate == locate_ ? order.shares : 0u;
    }

    std::uint64_t OrderBook::resting_shares() const noexcept
    {
        return order_shares_;
    }

    bool OrderBook::check_invariants_fast() const noexcept
    {
        return order_shares_ == level_shares_ && live_ == level_orders_;
    }

    bool OrderBook::matches_levels(const BidMap& computed_bids,
                                   const AskMap& computed_asks) const
    {
        if (!check_invariants_fast())
        {
            return false;
        }

        // Compared as best-first sequences, so the same check serves both level
        // representations, and ordering is checked as well as content.
        std::vector<Level> mine;
        bids(mine);
        if (mine.size() != computed_bids.size())
        {
            return false;
        }
        std::size_t i = 0;
        for (const auto& [price, level] : computed_bids)
        {
            const Level& m = mine[i++];
            if (m.price != price || m.shares != level.shares || m.orders != level.orders ||
                m.shares == 0 || m.orders == 0)
            {
                return false;
            }
        }

        asks(mine);
        if (mine.size() != computed_asks.size())
        {
            return false;
        }
        i = 0;
        for (const auto& [price, level] : computed_asks)
        {
            const Level& m = mine[i++];
            if (m.price != price || m.shares != level.shares || m.orders != level.orders ||
                m.shares == 0 || m.orders == 0)
            {
                return false;
            }
        }

#ifndef NTS_LEVELS_MAP
        if (!bids_.check_invariants() || !asks_.check_invariants())
        {
            return false;
        }
#endif
        return true;
    }

    bool OrderBook::check_invariants() const
    {
        // The order index is the authority. Levels are a derived cache, so the check
        // that matters is that the cache still equals what the index implies.
        BidMap computed_bids;
        AskMap computed_asks;

        std::uint64_t seen = 0;
        store_->index.for_each([&](std::uint64_t ref, std::uint32_t handle) {
            (void) ref;
            const Order& order = store_->at(handle);
            if (order.locate != locate_)
            {
                return;
            }
            ++seen;
            auto& level =
                (order.side == Side::Buy) ? computed_bids[order.price] : computed_asks[order.price];
            level.price   = order.price;
            level.shares += order.shares;
            level.orders += 1;
        });

        if (seen != live_)
        {
            return false;
        }
        return matches_levels(computed_bids, computed_asks);
    }
}  // namespace nts::book
