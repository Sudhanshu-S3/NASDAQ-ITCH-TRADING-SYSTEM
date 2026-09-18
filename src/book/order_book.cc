#include "book/order_book.h"

#include <functional>
#include <map>
#include <vector>

namespace nts::book
{
    ApplyResult OrderBook::add(std::uint64_t ref,
                               Side          side,
                               std::uint32_t shares,
                               std::uint32_t price)
    {
        const bool inserted = orders_.insert(ref, shares, price, side == Side::Buy);

        // Refuse a duplicate rather than half applying it. insert leaves the index
        // untouched when the key exists, so updating the level anyway would credit it
        // shares and an order count that no live order backs, and the book would drift
        // by exactly that amount with nothing reporting an error.
        if (!inserted)
        {
            return ApplyResult::DuplicateRef;
        }

        order_shares_ += shares;

        if (side == Side::Buy)
        {
            bids_.add(price, shares);
        }
        else
        {
            asks_.add(price, shares);
        }

        level_shares_ += shares;
        level_orders_ += 1;

        return ApplyResult::Applied;
    }

    ApplyResult OrderBook::reduce(std::uint64_t ref,
                                  std::uint32_t shares)
    {
        OrderIndex::Order* order_p = orders_.find(ref);
        if (order_p == nullptr)
        {
            return ApplyResult::UnknownRef;
        }

        // Copied out rather than referenced through. The level update below cannot
        // touch the index, so nothing can invalidate this, and a local is one register
        // instead of a reload from the slot on every use.
        const std::uint32_t order_qty   = order_p->qty();
        const std::uint32_t order_price = order_p->price;
        const bool          order_buy   = order_p->buy();

        // Clamp rather than underflow. A reduction larger than the order holds is a
        // corrupt stream or a decode bug, and wrapping a uint32_t would turn it into a
        // four billion share level that looks like real liquidity.
        const bool          clamped   = shares > order_qty;
        const std::uint32_t reduce_by = clamped ? order_qty : shares;

        const std::uint32_t left = order_qty - reduce_by;
        order_p->shares = order_buy ? (left | OrderIndex::kSideBit) : left;
        order_shares_ -= reduce_by;

        // The order dies at zero. Leaving a dead reference live is what lets a later
        // replace resurrect it, and the book then drifts by that quantity for the rest
        // of the day with nothing reporting an error.
        const bool dies = (left == 0);

        // PriceLevels::reduce takes shares only; remove takes shares and one order and
        // erases the level when it empties. A dying reduce is a remove, so the level
        // path makes the same distinction the order path just made.
        PriceLevels& side = order_buy ? bids_ : asks_;
        if (dies)
        {
            side.remove(order_price, reduce_by);
            level_orders_ -= 1;
        }
        else
        {
            side.reduce(order_price, reduce_by);
        }
        level_shares_ -= reduce_by;

        if (dies)
        {
            orders_.erase(ref);
        }

        return clamped ? ApplyResult::ClampedReduce : ApplyResult::Applied;
    }

    ApplyResult OrderBook::remove(std::uint64_t ref)
    {
        const OrderIndex::Order* order_p = orders_.find(ref);
        if (order_p == nullptr)
        {
            return ApplyResult::UnknownRef;
        }

        const std::uint32_t order_qty   = order_p->qty();
        const std::uint32_t order_price = order_p->price;
        const bool          order_buy   = order_p->buy();

        order_shares_ -= order_qty;

        PriceLevels& side = order_buy ? bids_ : asks_;
        side.remove(order_price, order_qty);
        level_shares_ -= order_qty;
        level_orders_ -= 1;

        (void) orders_.erase(ref);
        return ApplyResult::Applied;
    }

    ApplyResult OrderBook::replace(std::uint64_t original_ref,
                                   std::uint64_t new_ref,
                                   std::uint32_t shares,
                                   std::uint32_t price)
    {
        const OrderIndex::Order* original = orders_.find(original_ref);
        if (original == nullptr)
        {
            return ApplyResult::UnknownRef;
        }

        // Check the new reference before touching anything. Discovering the collision
        // after the remove would leave the original deleted and the replacement
        // refused, losing the order outright.
        if (new_ref != original_ref && orders_.find(new_ref) != nullptr)
        {
            return ApplyResult::DuplicateRef;
        }

        // Read the side before the erase. It is the one field the replace message does
        // not carry, and after remove() the order it came from is gone.
        const Side side = original->buy() ? Side::Buy : Side::Sell;

        const ApplyResult removed = remove(original_ref);
        if (removed != ApplyResult::Applied)
        {
            return removed;
        }
        return add(new_ref, side, shares, price);
    }

    std::optional<Level> OrderBook::best_bid() const
    {
        return bids_.best();
    }

    std::optional<Level> OrderBook::best_ask() const
    {
        return asks_.best();
    }

    std::size_t OrderBook::live_orders() const noexcept
    {
        return orders_.size();
    }

    std::uint32_t OrderBook::shares_of(std::uint64_t ref) const noexcept
    {
        const OrderIndex::Order* order = orders_.find(ref);
        return order == nullptr ? 0u : order->qty();
    }

    std::uint64_t OrderBook::resting_shares() const noexcept
    {
        return order_shares_;
    }

    std::size_t OrderBook::index_rehashes() const noexcept
    {
        return orders_.rehashes();
    }

    std::size_t OrderBook::rebases() const noexcept
    {
        return bids_.rebases() + asks_.rebases();
    }

    void OrderBook::reserve()
    {
        bids_.reserve();
        asks_.reserve();
    }

    bool OrderBook::check_invariants_fast() const noexcept
    {
        return order_shares_ == level_shares_ && orders_.size() == level_orders_;
    }

    namespace
    {
        // The stored side against a rebuild from the index. Written once for both
        // sides because the comparison does not care which way "best" points; only
        // the container being checked does.
        bool side_matches(const PriceLevels&                          stored,
                          const std::map<std::uint32_t, Level>&       computed)
        {
            if (!stored.check_invariants())
            {
                return false;
            }
            std::vector<Level> levels;
            stored.levels(levels);
            if (levels.size() != computed.size())
            {
                return false;
            }
            for (const Level& level : levels)
            {
                if (level.shares == 0 || level.orders == 0)
                {
                    return false;
                }
                const auto it = computed.find(level.price);
                if (it == computed.end())
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
    }  // namespace

    bool OrderBook::check_invariants() const
    {
        if (!check_invariants_fast())
        {
            return false;
        }

        std::map<std::uint32_t, Level> computed_bids;
        std::map<std::uint32_t, Level> computed_asks;

        orders_.for_each([&](std::uint64_t ref, const OrderIndex::Order& order) {
            (void) ref;
            auto& level = order.buy() ? computed_bids[order.price] : computed_asks[order.price];
            level.price   = order.price;
            level.shares += order.qty();
            level.orders += 1;
        });

        return side_matches(bids_, computed_bids) && side_matches(asks_, computed_asks);
    }
}  // namespace nts::book
