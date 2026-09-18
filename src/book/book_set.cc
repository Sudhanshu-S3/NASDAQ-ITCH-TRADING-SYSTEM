#include "book/book_set.h"

namespace nts::book
{
    void BookSet::on_stock_directory(const itch::StockDirectory& msg)
    {
        const std::size_t locate = msg.header.stock_locate;

        if (locate >= books_.size())
        {
            books_.resize(locate + 1);
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

        for (std::size_t i = 0; i < books_.size(); ++i)
        {
            if (known_[i] && !books_[i].check_invariants())
            {
                return false;
            }
        }
        return true;
    }
}  // namespace nts::book
