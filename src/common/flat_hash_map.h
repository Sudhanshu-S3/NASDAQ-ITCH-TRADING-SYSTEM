#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace nts
{
    /**
     * Order reference to resting order. Open addressing, linear probing.
     */
    class OrderIndex
    {
    public:
        static constexpr std::uint32_t kSideBit = 0x80000000u;
        static constexpr std::size_t   kInitial = 256;

        struct Order
        {
            std::uint32_t shares = 0;  ///< low 31 bits; bit 31 set means buy
            std::uint32_t price  = 0;

            [[nodiscard]] std::uint32_t qty() const noexcept
            {
                return shares & ~kSideBit;
            }

            [[nodiscard]] bool buy() const noexcept
            {
                return (shares & kSideBit) != 0;
            }
        };

        /**
         * Returns false and changes nothing if ref is already present. The duplicate
         * check costs no extra probe because it is the same walk the insert makes.
         */
        bool insert(std::uint64_t ref,
                    std::uint32_t qty,
                    std::uint32_t price,
                    bool          buy) noexcept
        {
            assert((qty & kSideBit) == 0 && "quantity overlaps the side bit");

            // Zero is the empty marker, so it cannot also be a live key. NASDAQ numbers
            // order references from 1, so this is unreachable on a correct decode, and
            // refusing is the safe failure: the caller reports it, replay counts it as
            // a duplicate reference, the run is marked dirty and the benchmark refuses
            // to print a number. Storing it would silently blank a slot instead.
            if (ref == 0)
            {
                return false;
            }

            if (size_ + 1 > grow_at_)
            {
                grow();
            }
            std::size_t i = home_slot(ref);
            while (slots_[i].ref != 0)
            {
                if (slots_[i].ref == ref)
                {
                    return false;
                }
                i = (i + 1) & mask_;
            }
            slots_[i].ref          = ref;
            slots_[i].order.shares = buy ? (qty | kSideBit) : qty;
            slots_[i].order.price  = price;
            ++size_;
            return true;
        }

        /** Null if ref is not live. The pointer is valid until the next insert. */
        [[nodiscard]] Order* find(std::uint64_t ref) noexcept
        {
            if (size_ == 0)
            {
                return nullptr;
            }
            std::size_t i = home_slot(ref);
            while (slots_[i].ref != 0)
            {
                if (slots_[i].ref == ref)
                {
                    return &slots_[i].order;
                }
                i = (i + 1) & mask_;
            }
            return nullptr;
        }

        [[nodiscard]] const Order* find(std::uint64_t ref) const noexcept
        {
            return const_cast<OrderIndex*>(this)->find(ref);
        }

        /** Returns false if ref was not present. */
        bool erase(std::uint64_t ref) noexcept
        {
            if (size_ == 0)
            {
                return false;
            }
            std::size_t i = home_slot(ref);
            while (slots_[i].ref != 0)
            {
                if (slots_[i].ref == ref)
                {
                    backward_shift(i);
                    --size_;
                    return true;
                }
                i = (i + 1) & mask_;
            }
            return false;
        }

        [[nodiscard]] std::size_t size() const noexcept
        {
            return size_;
        }

        [[nodiscard]] std::size_t capacity() const noexcept
        {
            return slots_.size();
        }

        /** How many times this table doubled. Non zero inside a timed run is a cost. */
        [[nodiscard]] std::size_t rehashes() const noexcept
        {
            return rehashes_;
        }

        /**
         * Starts fetching the cache line ref's probe begins on, without waiting for
         * it. For a lookup that is known to be coming but not yet needed.
         */
        void prefetch(std::uint64_t ref) const noexcept
        {
            if (size_ != 0)
            {
                __builtin_prefetch(&slots_[home_slot(ref)], 0, 3);
            }
        }

        /** Where ref would sit with no collisions. Public so a test can build a chain. */
        [[nodiscard]] std::size_t home_slot(std::uint64_t ref) const noexcept
        {
            // Fibonacci style multiply then fold the high half down. The high bits of
            // the product are the well mixed ones; masking the low bits directly would
            // throw the mixing away for a power of two table.
            const std::uint64_t h = ref * 0x9E3779B97F4A7C15ULL;
            return static_cast<std::size_t>(h ^ (h >> 32)) & mask_;
        }

        /**
         * Calls f(ref, order) for every live entry, in slot order. Not a hot path
         * function: it exists so an invariant check can rebuild the book from the
         * index alone.
         */
        template <typename F>
        void for_each(F&& f) const
        {
            for (const Slot& s : slots_)
            {
                if (s.ref != 0)
                {
                    f(s.ref, s.order);
                }
            }
        }

    private:
        struct Slot
        {
            std::uint64_t ref = 0;  ///< 0 means empty
            Order         order{};
        };
        static_assert(sizeof(Slot) == 16, "four slots per cache line");

        /**
         * Closes the hole at position hole so that no probe chain is broken.
         */
        void backward_shift(std::size_t hole) noexcept
        {
            std::size_t j = hole;
            for (;;)
            {
                j = (j + 1) & mask_;
                if (slots_[j].ref == 0)
                {
                    break;
                }
                const std::size_t home = home_slot(slots_[j].ref);

                // "home in (hole, j]" on a ring, written without branches on wrap:
                // the distance from hole to j, compared with the distance from hole
                // to home, both taken forward around the ring.
                const std::size_t dist_j    = (j - hole) & mask_;
                const std::size_t dist_home = (home - hole) & mask_;
                if (dist_home <= dist_j && dist_home != 0)
                {
                    continue;
                }
                slots_[hole] = slots_[j];
                hole         = j;
            }
            slots_[hole] = Slot{};
        }

        // Doubling rebuild. Every key has to be reinserted because the mask changed,
        // so this is O(capacity) and is exactly the spike rehashes() exists to expose.
        void grow()
        {
            const std::size_t next = slots_.empty() ? kInitial : slots_.size() * 2;

            std::vector<Slot> old;
            old.swap(slots_);
            slots_.assign(next, Slot{});
            mask_    = next - 1;
            grow_at_ = next - next / 4;  // 0.75, computed without floating point
            size_    = 0;
            if (!old.empty())
            {
                ++rehashes_;
            }
            for (const Slot& s : old)
            {
                if (s.ref == 0)
                {
                    continue;
                }
                std::size_t i = home_slot(s.ref);
                while (slots_[i].ref != 0)
                {
                    i = (i + 1) & mask_;
                }
                slots_[i] = s;
                ++size_;
            }
        }

        std::vector<Slot> slots_;
        std::size_t       mask_      = 0;
        std::size_t       size_      = 0;
        std::size_t       grow_at_   = 0;
        std::size_t       rehashes_  = 0;
    };
}  // namespace nts
