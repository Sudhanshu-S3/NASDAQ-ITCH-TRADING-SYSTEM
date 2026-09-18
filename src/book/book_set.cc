#include "book/book_set.h"

namespace nts::book
{
    BookSet::BookSet(std::size_t max_live_orders) : store_(max_live_orders) {}

    void BookSet::on_stock_directory(const itch::StockDirectory& msg)
    {
        const std::size_t locate = msg.header.stock_locate;

        while (books_.size() <= locate)
        {
            books_.emplace_back(store_, static_cast<std::uint16_t>(books_.size()));
            books_.back().set_track_top(track_top_);
        }
        if (locate >= symbols_.size())
        {
            symbols_.resize(locate + 1);
            known_.resize(locate + 1, false);
        }

        // A repeated directory message for the same locate updates the symbol but must
        // not be counted twice.
        if (!known_[locate])
        {
            ++registered_;
        }

        symbols_[locate] = msg.stock;
        known_[locate]   = true;
    }

    void BookSet::reserve_locates(std::size_t count)
    {
        while (books_.size() < count)
        {
            books_.emplace_back(store_, static_cast<std::uint16_t>(books_.size()));
            books_.back().set_track_top(track_top_);
            books_.back().reserve_levels();
        }
        if (symbols_.size() < count)
        {
            symbols_.resize(count);
            known_.resize(count, false);
        }
    }

    void BookSet::set_track_top(bool on) noexcept
    {
        track_top_ = on;
        for (OrderBook& b : books_)
        {
            b.set_track_top(on);
        }
    }

    OrderBook& BookSet::book(std::uint16_t locate)
    {
        return books_[locate];
    }

    const OrderBook& BookSet::book(std::uint16_t locate) const
    {
        return books_[locate];
    }

    std::string_view BookSet::symbol(std::uint16_t locate) const
    {
        if (!known(locate))
        {
            return {};
        }
        return {symbols_[locate].data(), symbols_[locate].size()};
    }

    bool BookSet::known(std::uint16_t locate) const noexcept
    {
        return locate < known_.size() && known_[locate];
    }

    std::size_t BookSet::size() const noexcept
    {
        return registered_;
    }

    std::uint64_t BookSet::resting_shares() const noexcept
    {
        std::uint64_t total = 0;
        for (const OrderBook& b : books_)
        {
            total += b.resting_shares();
        }
        return total;
    }

    bool BookSet::check_invariants_fast() const noexcept
    {
        for (const OrderBook& b : books_)
        {
            if (!b.check_invariants_fast())
            {
                return false;
            }
        }
        return true;
    }

    bool BookSet::check_invariants() const
    {
        // known_ and books_ are resized together, so they must stay the same length.
        // If they ever diverge, the indexing below is out of bounds and every other
        // answer this class gives is meaningless.
        if (known_.size() != books_.size() || symbols_.size() != books_.size())
        {
            return false;
        }

        // One pass over the shared index, sorting every live order into its book's
        // rebuilt levels. Then each book compares its cache against its rebuild.
        std::vector<OrderBook::BidMap> bids(books_.size());
        std::vector<OrderBook::AskMap> asks(books_.size());
        std::vector<std::uint64_t>     counted(books_.size(), 0);

        bool ok = true;
        store_.index.for_each(
            [&](std::uint64_t ref, std::uint32_t handle)
            {
                (void) ref;
                const Order& order = store_.at(handle);
                if (order.locate >= books_.size())
                {
                    ok = false;
                    return;
                }
                ++counted[order.locate];
                auto& level   = (order.side == Side::Buy) ? bids[order.locate][order.price]
                                                          : asks[order.locate][order.price];
                level.price   = order.price;
                level.shares += order.shares;
                level.orders += 1;
            });
        if (!ok)
        {
            return false;
        }

        for (std::size_t i = 0; i < books_.size(); ++i)
        {
            if (counted[i] != books_[i].live_orders())
            {
                return false;
            }
            if (!books_[i].matches_levels(bids[i], asks[i]))
            {
                return false;
            }
        }
        return true;
    }
}  // namespace nts::book
