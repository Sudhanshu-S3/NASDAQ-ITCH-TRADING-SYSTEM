#include "book/order_book.h"

namespace nts::book
{
    ApplyResult OrderBook::add(std::uint64_t ref,
                               Side          side,
                               std::uint32_t shares,
                               std::uint32_t price)
    {
        const auto [it, inserted] = orders_.emplace(ref, Order{shares, price, side});
        (void) it;

        // Refuse a duplicate rather than half applying it. emplace leaves the index
        // untouched when the key exists, so updating the level anyway would credit it
        // shares and an order count that no live order backs, and the book would drift
        // by exactly that amount with nothing reporting an error.
        if (!inserted)
        {
            return ApplyResult::DuplicateRef;
        }

        order_shares_ += shares;

        auto& level   = (side == Side::Buy) ? bids_[price] : asks_[price];
        level.price   = price;
        level.shares += shares;
        level.orders += 1;

        level_shares_ += shares;
        level_orders_ += 1;

        return ApplyResult::Applied;
    }

    ApplyResult OrderBook::reduce(std::uint64_t ref,
                                  std::uint32_t shares)
    {
        const auto it = orders_.find(ref);
        if (it == orders_.end())
        {
            return ApplyResult::UnknownRef;
        }

        Order& order = it->second;

        // Clamp rather than underflow. A reduction larger than the order holds is a
        // corrupt stream or a decode bug, and wrapping a uint32_t would turn it into a
        // four billion share level that looks like real liquidity.
        const bool          clamped   = shares > order.shares;
        const std::uint32_t reduce_by = clamped ? order.shares : shares;

        order.shares  -= reduce_by;
        order_shares_ -= reduce_by;

        // The order dies at zero. Leaving a dead reference live is what lets a later
        // replace resurrect it, and the book then drifts by that quantity for the rest
        // of the day with nothing reporting an error.
        const bool dies = (order.shares == 0);

        // The two maps have different comparators and therefore different types, so
        // the side switch stays explicit rather than being hidden behind a reference.
        if (order.side == Side::Buy)
        {
            const auto level_it = bids_.find(order.price);
            if (level_it != bids_.end())
            {
                level_it->second.shares -= reduce_by;
                level_shares_           -= reduce_by;
                if (dies)
                {
                    level_it->second.orders -= 1;
                    level_orders_           -= 1;
                    if (level_it->second.orders == 0)
                    {
                        bids_.erase(level_it);
                    }
                }
            }
        }
        else
        {
            const auto level_it = asks_.find(order.price);
            if (level_it != asks_.end())
            {
                level_it->second.shares -= reduce_by;
                level_shares_           -= reduce_by;
                if (dies)
                {
                    level_it->second.orders -= 1;
                    level_orders_           -= 1;
                    if (level_it->second.orders == 0)
                    {
                        asks_.erase(level_it);
                    }
                }
            }
        }

        if (dies)
        {
            orders_.erase(it);
        }

        return clamped ? ApplyResult::ClampedReduce : ApplyResult::Applied;
    }

    ApplyResult OrderBook::remove(std::uint64_t ref)
    {
        const auto it = orders_.find(ref);
        if (it == orders_.end())
        {
            return ApplyResult::UnknownRef;
        }

        const Order& order = it->second;

        order_shares_ -= order.shares;

        if (order.side == Side::Buy)
        {
            const auto level_it = bids_.find(order.price);
            if (level_it != bids_.end())
            {
                level_it->second.shares -= order.shares;
                level_it->second.orders -= 1;
                level_shares_           -= order.shares;
                level_orders_           -= 1;
                if (level_it->second.orders == 0)
                {
                    bids_.erase(level_it);
                }
            }
        }
        else
        {
            const auto level_it = asks_.find(order.price);
            if (level_it != asks_.end())
            {
                level_it->second.shares -= order.shares;
                level_it->second.orders -= 1;
                level_shares_           -= order.shares;
                level_orders_           -= 1;
                if (level_it->second.orders == 0)
                {
                    asks_.erase(level_it);
                }
            }
        }

        orders_.erase(it);
        return ApplyResult::Applied;
    }

    ApplyResult OrderBook::replace(std::uint64_t original_ref,
                                   std::uint64_t new_ref,
                                   std::uint32_t shares,
                                   std::uint32_t price)
    {
        const auto it = orders_.find(original_ref);
        if (it == orders_.end())
        {
            return ApplyResult::UnknownRef;
        }

        // Check the new reference before touching anything. Discovering the collision
        // after the remove would leave the original deleted and the replacement
        // refused, losing the order outright.
        if (new_ref != original_ref && orders_.count(new_ref) != 0)
        {
            return ApplyResult::DuplicateRef;
        }

        // Read the side before the erase. It is the one field the replace message does
        // not carry, and after remove() the order it came from is gone.
        const Side side = it->second.side;

        const ApplyResult removed = remove(original_ref);
        if (removed != ApplyResult::Applied)
        {
            return removed;
        }
        return add(new_ref, side, shares, price);
    }

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

    std::size_t OrderBook::live_orders() const noexcept
    {
        return orders_.size();
    }

    std::uint32_t OrderBook::shares_of(std::uint64_t ref) const noexcept
    {
        const auto it = orders_.find(ref);
        return it == orders_.end() ? 0u : it->second.shares;
    }

    std::uint64_t OrderBook::resting_shares() const noexcept
    {
        return order_shares_;
    }

    bool OrderBook::check_invariants_fast() const noexcept
    {
        // Two totals maintained down separate paths. order_shares_ moves only where an
        // order changes, level_shares_ only where a level changes, and the level path
        // is the one guarded by a find() that can miss. A divergence here is exactly
        // the level-versus-index drift, caught in constant time.
        return order_shares_ == level_shares_ && orders_.size() == level_orders_;
    }

    bool OrderBook::check_invariants() const
    {
        if (!check_invariants_fast())
        {
            return false;
        }

        std::map<std::uint32_t, Level, std::greater<>> computed_bids;
        std::map<std::uint32_t, Level>                 computed_asks;

        // The order index is the authority. Levels are a derived cache, so the check
        // that matters is that the cache still equals what the index implies.
        for (const auto& [ref, order] : orders_)
        {
            (void) ref;
            auto& level =
                (order.side == Side::Buy) ? computed_bids[order.price] : computed_asks[order.price];
            level.price   = order.price;
            level.shares += order.shares;
            level.orders += 1;
        }

        if (bids_.size() != computed_bids.size() || asks_.size() != computed_asks.size())
        {
            return false;
        }

        for (const auto& [price, level] : bids_)
        {
            if (level.shares == 0 || level.orders == 0)
            {
                return false;
            }
            const auto it = computed_bids.find(price);
            if (it == computed_bids.end())
            {
                return false;
            }
            if (level.shares != it->second.shares || level.orders != it->second.orders)
            {
                return false;
            }
        }

        for (const auto& [price, level] : asks_)
        {
            if (level.shares == 0 || level.orders == 0)
            {
                return false;
            }
            const auto it = computed_asks.find(price);
            if (it == computed_asks.end())
            {
                return false;
            }
            if (level.shares != it->second.shares || level.orders != it->second.orders)
            {
                return false;
            }
        }

        return true;
    }
}  // namespace nts::book
