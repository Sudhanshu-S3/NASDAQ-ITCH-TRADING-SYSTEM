#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "book/level.h"

namespace nts::book
{
    /**
     * One side of a book: a flat array of cents around a reference price for the
     * dense middle, plus a sorted map for the long tail.
     */
    class PriceLevels
    {
    public:
        static constexpr std::uint32_t kTick   = 100;   ///< one cent, in price units
        static constexpr std::uint32_t kNoBase = 0xFFFFFFFFu;

        static constexpr std::uint32_t kDefaultSpan = 1024;

        /**
         * reference_price of 0 means "centre on the first add". tick_span is the number
         * of one cent cells and must be a multiple of 64.
         */
        PriceLevels(Side          side,
                    std::uint32_t reference_price,
                    std::uint32_t tick_span);

        /** One more order of shares at price. Creates the level if needed. */
        void add(std::uint32_t price,
                 std::uint32_t shares);

        /** Takes shares off the level; the order count is unchanged. */
        void reduce(std::uint32_t price,
                    std::uint32_t shares) noexcept;

        /** Takes shares and one order off the level, erasing it if it is now empty. */
        void remove(std::uint32_t price,
                    std::uint32_t shares) noexcept;

        [[nodiscard]] std::optional<Level> best() const noexcept;

        void reserve();

        /** Every level, best first. Not hot: for the dump and the invariant check. */
        void levels(std::vector<Level>& out) const;

        [[nodiscard]] std::size_t size() const noexcept
        {
            return array_count_ + far_.size();
        }

        [[nodiscard]] std::size_t rebases() const noexcept
        {
            return rebases_;
        }

        [[nodiscard]] std::size_t far_levels() const noexcept
        {
            return far_.size();
        }

        /**
         * Occupancy bitmap agrees with the cells, the cached best agrees with a scan,
         * every level has non-zero shares and orders, and nothing in far_ would fit in
         * the array.
         */
        [[nodiscard]] bool check_invariants() const;

    private:
        [[nodiscard]] bool better(std::uint32_t a,
                                  std::uint32_t b) const noexcept
        {
            return side_ == Side::Buy ? a > b : a < b;
        }

        // The cell for price, or -1 if it is not a whole cent inside the window.
        [[nodiscard]] std::int64_t cell_of(std::uint32_t price) const noexcept;

        [[nodiscard]] std::uint32_t far_best_price() const noexcept;

        void set_bit(std::size_t i) noexcept;
        void clear_bit(std::size_t i) noexcept;

        // Best occupied cell, scanning from the far end of the array. -1 if none.
        [[nodiscard]] std::int64_t scan_best() const noexcept;

        void ensure_cells();
        void rebase(std::uint32_t centre_price);

        // Puts a whole level wherever it belongs given the current base.
        void place(const Level& l);

        Side          side_;
        std::uint32_t span_;
        std::uint32_t base_cents_ = kNoBase;

        std::vector<Level>         cells_;
        std::vector<std::uint64_t> bits_;
        std::size_t                array_count_ = 0;
        std::int64_t               best_cell_   = -1;

        std::map<std::uint32_t, Level> far_;
        std::size_t                    rebases_ = 0;
    };
}  // namespace nts::book
