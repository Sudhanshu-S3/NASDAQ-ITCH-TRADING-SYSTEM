#pragma once

#include <cstdint>

namespace nts::mold
{
    /**
     * Decides which packets to drop on purpose.
     */
    class GapInjector
    {
    public:
        static GapInjector none() noexcept
        {
            return GapInjector{};
        }

        /** Drops packets n-1, 2n-1, ... (every nth packet, counting from one). */
        static GapInjector every_nth(std::uint64_t n) noexcept
        {
            GapInjector g;
            g.every_ = n;
            return g;
        }

        static GapInjector with_probability(double        p,
                                            std::uint64_t seed) noexcept
        {
            GapInjector g;
            g.probability_ = p;
            g.state_       = seed;
            return g;
        }

        [[nodiscard]] bool should_drop(std::uint64_t packet_index) noexcept
        {
            if (every_ != 0)
            {
                return (packet_index + 1) % every_ == 0;
            }
            if (probability_ > 0.0)
            {
                // splitmix64: one multiply-xorshift step per packet, full period, and
                // the same seed always gives the same drop pattern.
                state_ += 0x9E3779B97F4A7C15ULL;
                std::uint64_t z = state_;
                z               = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
                z               = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
                z ^= z >> 31;
                const double u = static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0);
                return u < probability_;
            }
            return false;
        }

        [[nodiscard]] bool active() const noexcept
        {
            return every_ != 0 || probability_ > 0.0;
        }

    private:
        std::uint64_t every_       = 0;
        double        probability_ = 0.0;
        std::uint64_t state_       = 0;
    };
}  // namespace nts::mold
