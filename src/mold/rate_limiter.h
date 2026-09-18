#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

namespace nts::mold
{
    /**
     * Paces packets at a fixed rate from a fixed origin.
     *
     * Packet i is due at origin + i * interval. Scheduling from the origin rather
     * than from "now plus interval" is the whole design: the second form adds every
     * overshoot to the next deadline, and the actual rate sags below the reported one
     * with nothing saying so. From a fixed origin an overshoot is caught up on the
     * next packet, and the long run rate is exact.
     */
    class RateLimiter
    {
    public:
        using Clock = std::chrono::steady_clock;

        static constexpr std::chrono::microseconds kSleepThreshold{500};

        explicit RateLimiter(double packets_per_second) noexcept
            : interval_ns_(packets_per_second > 0.0 ? 1e9 / packets_per_second : 0.0)
            , origin_(Clock::now())
        {
        }

        /** Blocks until packet_index is due. Returns the scheduled send time in ns. */
        std::uint64_t wait(std::uint64_t packet_index) noexcept
        {
            if (interval_ns_ <= 0.0)
            {
                return now_ns();
            }
            const auto due_ns = static_cast<std::int64_t>(static_cast<double>(packet_index) *
                                                          interval_ns_);
            const Clock::time_point due = origin_ + std::chrono::nanoseconds(due_ns);

            Clock::time_point now = Clock::now();
            if (due - now > kSleepThreshold)
            {
                std::this_thread::sleep_for(due - now - kSleepThreshold);
            }
            while ((now = Clock::now()) < due)
            {
                // Spin. The remainder is under kSleepThreshold by construction.
            }
            return to_ns(due);
        }

        [[nodiscard]] bool unthrottled() const noexcept
        {
            return interval_ns_ <= 0.0;
        }

        [[nodiscard]] double interval_ns() const noexcept
        {
            return interval_ns_;
        }

        [[nodiscard]] std::uint64_t now_ns() const noexcept
        {
            return to_ns(Clock::now());
        }

    private:
        static std::uint64_t to_ns(Clock::time_point t) noexcept
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch())
                    .count());
        }

        double            interval_ns_;
        Clock::time_point origin_;
    };
}  // namespace nts::mold
