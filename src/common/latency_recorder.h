#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "common/timing.h"

namespace nts
{
    /**
     * Stamps into a pre-sized array; analyses after the run.
     */
    class LatencyRecorder
    {
    public:
        explicit LatencyRecorder(std::size_t capacity)
            : samples_(capacity)
        {
        }

        void record(std::uint64_t ticks) noexcept
        {
            if (count_ < samples_.size())
            {
                samples_[count_++] = ticks;
            }
            else
            {
                ++dropped_;
            }
        }

        /** Forgets the first n samples. Warm-up discard, applied after the run. */
        void discard_first(std::size_t n) noexcept
        {
            if (n >= count_)
            {
                count_ = 0;
                return;
            }
            std::copy(samples_.begin() + static_cast<std::ptrdiff_t>(n),
                      samples_.begin() + static_cast<std::ptrdiff_t>(count_), samples_.begin());
            count_ -= n;
        }

        [[nodiscard]] std::size_t count() const noexcept
        {
            return count_;
        }

        [[nodiscard]] std::uint64_t dropped() const noexcept
        {
            return dropped_;
        }

        /** p in [0, 1]. Ticks. 0 if nothing was recorded. */
        [[nodiscard]] std::uint64_t percentile(double p) const
        {
            if (count_ == 0)
            {
                return 0;
            }
            std::vector<std::uint64_t> sorted(samples_.begin(),
                                              samples_.begin() + static_cast<std::ptrdiff_t>(count_));
            std::sort(sorted.begin(), sorted.end());
            auto idx = static_cast<std::size_t>(p * static_cast<double>(count_ - 1) + 0.5);
            if (idx >= count_)
            {
                idx = count_ - 1;
            }
            return sorted[idx];
        }

        [[nodiscard]] std::uint64_t max_ticks() const noexcept
        {
            std::uint64_t m = 0;
            for (std::size_t i = 0; i < count_; ++i)
            {
                m = samples_[i] > m ? samples_[i] : m;
            }
            return m;
        }

        /** One line per percentile in nanoseconds. Never called inside a timed region. */
        void report(std::FILE*      out,
                    const TscClock& clock,
                    const char*     label) const
        {
            std::fprintf(out, "%-14s samples %10zu  dropped %8llu  p50 %9llu ns  p99 %9llu ns  "
                              "p99.9 %9llu ns  max %9llu ns\n",
                         label, count_, static_cast<unsigned long long>(dropped_),
                         static_cast<unsigned long long>(clock.to_ns(percentile(0.50))),
                         static_cast<unsigned long long>(clock.to_ns(percentile(0.99))),
                         static_cast<unsigned long long>(clock.to_ns(percentile(0.999))),
                         static_cast<unsigned long long>(clock.to_ns(max_ticks())));
        }

        /** Raw access for tools that join samples to other logs. */
        [[nodiscard]] const std::uint64_t* data() const noexcept
        {
            return samples_.data();
        }

    private:
        std::vector<std::uint64_t> samples_;
        std::size_t                count_   = 0;
        std::uint64_t              dropped_ = 0;
    };
}  // namespace nts
