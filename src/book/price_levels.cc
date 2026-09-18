#include "book/price_levels.h"

#include <bit>
#include <cassert>

namespace nts::book
{
    PriceLevels::PriceLevels(Side          side,
                             std::uint32_t reference_price,
                             std::uint32_t tick_span)
        : side_(side)
        , span_(tick_span)
    {
        assert(tick_span >= 64 && tick_span % 64 == 0);
        if (reference_price != 0 && reference_price % kTick == 0)
        {
            const std::uint32_t centre = reference_price / kTick;
            base_cents_                = centre > span_ / 2 ? centre - span_ / 2 : 0;
        }
    }

    std::int64_t PriceLevels::cell_of(std::uint32_t price) const noexcept
    {
        if (base_cents_ == kNoBase || price % kTick != 0)
        {
            return -1;
        }
        const std::uint32_t cents = price / kTick;
        if (cents < base_cents_ || cents - base_cents_ >= span_)
        {
            return -1;
        }
        return static_cast<std::int64_t>(cents - base_cents_);
    }

    std::uint32_t PriceLevels::far_best_price() const noexcept
    {
        return side_ == Side::Buy ? far_.rbegin()->first : far_.begin()->first;
    }

    void PriceLevels::set_bit(std::size_t i) noexcept
    {
        bits_[i >> 6] |= std::uint64_t{1} << (i & 63);
    }

    void PriceLevels::clear_bit(std::size_t i) noexcept
    {
        bits_[i >> 6] &= ~(std::uint64_t{1} << (i & 63));
    }

    std::int64_t PriceLevels::scan_best() const noexcept
    {
        // Bids: the highest occupied cell, so scan words from the top and take the
        // highest set bit. Asks: the lowest, so scan from the bottom and take the
        // lowest set bit. countl_zero and countr_zero are single instructions.
        if (side_ == Side::Buy)
        {
            for (std::size_t w = bits_.size(); w-- > 0;)
            {
                if (bits_[w] != 0)
                {
                    return static_cast<std::int64_t>(w * 64 + 63 -
                                                     static_cast<std::size_t>(std::countl_zero(bits_[w])));
                }
            }
        }
        else
        {
            for (std::size_t w = 0; w < bits_.size(); ++w)
            {
                if (bits_[w] != 0)
                {
                    return static_cast<std::int64_t>(w * 64 +
                                                     static_cast<std::size_t>(std::countr_zero(bits_[w])));
                }
            }
        }
        return -1;
    }

    void PriceLevels::reserve()
    {
        ensure_cells();
    }

    void PriceLevels::ensure_cells()
    {
        if (cells_.empty())
        {
            cells_.resize(span_);
            bits_.assign(span_ / 64, 0);
        }
    }

    void PriceLevels::place(const Level& l)
    {
        const std::int64_t c = cell_of(l.price);
        if (c < 0)
        {
            far_[l.price] = l;
            return;
        }
        const auto i = static_cast<std::size_t>(c);
        cells_[i]    = l;
        set_bit(i);
        ++array_count_;
        if (best_cell_ < 0 || better(l.price, cells_[static_cast<std::size_t>(best_cell_)].price))
        {
            best_cell_ = c;
        }
    }

    void PriceLevels::rebase(std::uint32_t centre_price)
    {
        // Gather everything, reset the window around the new centre, and put every
        // level back where it now belongs. O(levels), and by construction the new
        // centre is the best price, so after this the best is in the array.
        std::vector<Level> all;
        all.reserve(array_count_ + far_.size());
        if (!cells_.empty())
        {
            for (std::size_t w = 0; w < bits_.size(); ++w)
            {
                std::uint64_t word = bits_[w];
                while (word != 0)
                {
                    const auto bit = static_cast<std::size_t>(std::countr_zero(word));
                    all.push_back(cells_[w * 64 + bit]);
                    // Clear the cell as it is gathered. add() decides "new level" by
                    // orders == 0, so a stale cell left behind here would be read as
                    // a live level at whatever price now maps to that position.
                    cells_[w * 64 + bit] = Level{};
                    word &= word - 1;
                }
            }
        }
        for (const auto& [price, level] : far_)
        {
            (void) price;
            all.push_back(level);
        }
        far_.clear();
        ensure_cells();
        bits_.assign(span_ / 64, 0);
        array_count_ = 0;
        best_cell_   = -1;

        const std::uint32_t centre = centre_price / kTick;
        base_cents_                = centre > span_ / 2 ? centre - span_ / 2 : 0;
        ++rebases_;

        for (const Level& l : all)
        {
            place(l);
        }
    }

    void PriceLevels::add(std::uint32_t price,
                          std::uint32_t shares)
    {
        if (base_cents_ == kNoBase && price % kTick == 0)
        {
            const std::uint32_t centre = price / kTick;
            base_cents_                = centre > span_ / 2 ? centre - span_ / 2 : 0;
        }

        const std::int64_t c = cell_of(price);
        if (c >= 0)
        {
            ensure_cells();
            const auto i = static_cast<std::size_t>(c);
            Level&     l = cells_[i];
            if (l.orders == 0)
            {
                l.price = price;
                set_bit(i);
                ++array_count_;
                if (best_cell_ < 0 ||
                    better(price, cells_[static_cast<std::size_t>(best_cell_)].price))
                {
                    best_cell_ = c;
                }
            }
            l.shares += shares;
            l.orders += 1;
            return;
        }

        Level& l = far_[price];
        l.price  = price;
        l.shares += shares;
        l.orders += 1;

        // A far level that is now the best of the side means the window is in the
        // wrong place. Only a whole cent price can be a centre; a sub-penny best
        // stays in the map and the window stays where it is.
        if (price % kTick == 0 &&
            (best_cell_ < 0 || better(price, cells_[static_cast<std::size_t>(best_cell_)].price)))
        {
            rebase(price);
        }
    }

    void PriceLevels::reduce(std::uint32_t price,
                             std::uint32_t shares) noexcept
    {
        const std::int64_t c = cell_of(price);
        if (c >= 0)
        {
            cells_[static_cast<std::size_t>(c)].shares -= shares;
            return;
        }
        const auto it = far_.find(price);
        if (it != far_.end())
        {
            it->second.shares -= shares;
        }
    }

    void PriceLevels::remove(std::uint32_t price,
                             std::uint32_t shares) noexcept
    {
        const std::int64_t c = cell_of(price);
        if (c >= 0)
        {
            const auto i = static_cast<std::size_t>(c);
            Level&     l = cells_[i];
            l.shares -= shares;
            l.orders -= 1;
            if (l.orders == 0)
            {
                l.shares = 0;
                clear_bit(i);
                --array_count_;
                if (best_cell_ == c)
                {
                    best_cell_ = scan_best();
                    // The next best may now be in the map, in which case the window
                    // has drifted off the market and is re-centred. Not noexcept
                    // safe in theory (rebase allocates), but a rebase after a remove
                    // only moves levels that already exist, and the vector is
                    // reserved to their count.
                    if (!far_.empty())
                    {
                        const std::uint32_t fb = far_best_price();
                        if (fb % kTick == 0 &&
                            (best_cell_ < 0 ||
                             better(fb, cells_[static_cast<std::size_t>(best_cell_)].price)))
                        {
                            rebase(fb);
                        }
                    }
                }
            }
            return;
        }

        const auto it = far_.find(price);
        if (it == far_.end())
        {
            return;
        }
        it->second.shares -= shares;
        it->second.orders -= 1;
        if (it->second.orders == 0)
        {
            far_.erase(it);
        }
    }

    std::optional<Level> PriceLevels::best() const noexcept
    {
        if (best_cell_ >= 0)
        {
            const Level& a = cells_[static_cast<std::size_t>(best_cell_)];
            if (!far_.empty())
            {
                const std::uint32_t fb = far_best_price();
                if (better(fb, a.price))
                {
                    return side_ == Side::Buy ? far_.rbegin()->second : far_.begin()->second;
                }
            }
            return a;
        }
        if (!far_.empty())
        {
            return side_ == Side::Buy ? far_.rbegin()->second : far_.begin()->second;
        }
        return std::nullopt;
    }

    void PriceLevels::levels(std::vector<Level>& out) const
    {
        // Merge the array (in best-first cell order) with the map (in best-first key
        // order). Both are sorted, so this is one pass.
        out.clear();
        out.reserve(size());

        std::vector<Level> arr;
        arr.reserve(array_count_);
        if (!cells_.empty())
        {
            if (side_ == Side::Buy)
            {
                for (std::size_t w = bits_.size(); w-- > 0;)
                {
                    std::uint64_t word = bits_[w];
                    while (word != 0)
                    {
                        const auto bit = 63 - static_cast<std::size_t>(std::countl_zero(word));
                        arr.push_back(cells_[w * 64 + bit]);
                        word &= ~(std::uint64_t{1} << bit);
                    }
                }
            }
            else
            {
                for (std::size_t w = 0; w < bits_.size(); ++w)
                {
                    std::uint64_t word = bits_[w];
                    while (word != 0)
                    {
                        const auto bit = static_cast<std::size_t>(std::countr_zero(word));
                        arr.push_back(cells_[w * 64 + bit]);
                        word &= word - 1;
                    }
                }
            }
        }

        std::vector<Level> far;
        far.reserve(far_.size());
        if (side_ == Side::Buy)
        {
            for (auto it = far_.rbegin(); it != far_.rend(); ++it)
            {
                far.push_back(it->second);
            }
        }
        else
        {
            for (const auto& [price, level] : far_)
            {
                (void) price;
                far.push_back(level);
            }
        }

        std::size_t i = 0, j = 0;
        while (i < arr.size() && j < far.size())
        {
            if (better(arr[i].price, far[j].price))
            {
                out.push_back(arr[i++]);
            }
            else
            {
                out.push_back(far[j++]);
            }
        }
        while (i < arr.size())
        {
            out.push_back(arr[i++]);
        }
        while (j < far.size())
        {
            out.push_back(far[j++]);
        }
    }

    bool PriceLevels::check_invariants() const
    {
        std::size_t counted = 0;
        for (std::size_t i = 0; i < cells_.size(); ++i)
        {
            const bool bit = (bits_[i >> 6] >> (i & 63)) & 1u;
            const bool occ = cells_[i].orders != 0;
            if (bit != occ)
            {
                return false;
            }
            if (occ)
            {
                ++counted;
                if (cells_[i].shares == 0)
                {
                    return false;
                }
                if (cell_of(cells_[i].price) != static_cast<std::int64_t>(i))
                {
                    return false;
                }
            }
        }
        if (counted != array_count_)
        {
            return false;
        }
        if (scan_best() != best_cell_)
        {
            return false;
        }
        for (const auto& [price, level] : far_)
        {
            if (level.orders == 0 || level.shares == 0 || level.price != price)
            {
                return false;
            }
            if (cell_of(price) >= 0)
            {
                return false;  // would fit in the array, so it should not be here
            }
        }
        return true;
    }
}  // namespace nts::book
