#pragma once

#include <chrono>
#include <cstdint>

#if defined(__x86_64__) || defined(__i386__)
    #include <x86intrin.h>
#else
    #error "timing.h is x86 only: the TSC has no portable equivalent"
#endif

namespace nts
{
    /**
     * The measurement clock. D7.
     *
     * Two clocks, used for two different jobs, and confusing them is the usual way a
     * latency number turns out to be wrong.
     *
     * std::chrono::steady_clock is correct for a region measured in milliseconds: one
     * call costs tens of nanoseconds, which disappears against a region a million
     * times longer. Phase 2 times whole-file runs, so the benchmark uses it.
     *
     * The TSC is for the regions phase 6 measures, where the region itself is tens of
     * nanoseconds and the cost of reading the clock is the same order as the thing
     * being timed. It is a raw cycle counter, so it is cheap, and it counts ticks, not
     * nanoseconds, which is what calibrate() exists to convert.
     *
     * This header is settled here and used unchanged by phases 4, 5 and 6.
     */

    /**
     * Reads the timestamp counter.
     *
     * rdtscp, not rdtsc. rdtsc is not a serialising instruction: the processor may
     * hoist it above loads that were issued before it, so the timestamp can be taken
     * before the work it is meant to bracket. rdtscp waits for every prior load to
     * retire. It does not stop later instructions from being hoisted above it, so the
     * end of a timed region still wants a barrier if the region ends in a store.
     *
     * The aux argument is the processor id the read happened on, written by the
     * instruction whether the caller wants it or not. It is discarded here; phase 6
     * pins the thread to a core, which is the real answer to a counter that is only
     * comparable against itself on one core.
     */
    [[nodiscard]] inline std::uint64_t rdtscp_now() noexcept
    {
        unsigned aux = 0;
        return __rdtscp(&aux);
    }

    /**
     * Ticks to nanoseconds, calibrated against CLOCK_MONOTONIC on this machine.
     *
     * The TSC frequency is not the clock frequency and is not discoverable portably,
     * so the only honest way to get it is to measure both clocks over a window and
     * divide. Calibration is done once at startup and never on a measured path.
     */
    class TscClock
    {
    public:
        /**
         * Measures the tick rate over the window. Longer is more accurate; 200ms is
         * enough for the resolution phase 2 needs, and the cost is paid once.
         *
         * Meaningless unless invariant_tsc_available() is true, so callers check that
         * first. Constructing one anyway is allowed, because a caller that has already
         * recorded the consequence is entitled to proceed.
         */
        [[nodiscard]] static TscClock calibrate(std::chrono::milliseconds window);

        /**
         * True only if the CPU reports both constant_tsc and nonstop_tsc.
         *
         * Without constant_tsc the counter changes rate with the core frequency, so a
         * tick means a different amount of time depending on what the governor was
         * doing. Without nonstop_tsc it halts in deep C states. Either way every
         * number derived from it is wrong, and wrong in the direction that flatters
         * the result, because a core that idled looks fast.
         *
         * This returning false is a blocking finding: record it and stop, do not print
         * a warning and carry on reporting nanoseconds.
         */
        [[nodiscard]] static bool invariant_tsc_available() noexcept;

        [[nodiscard]] double ns_per_tick() const noexcept
        {
            return ns_per_tick_;
        }

        [[nodiscard]] std::uint64_t to_ns(std::uint64_t ticks) const noexcept
        {
            return static_cast<std::uint64_t>(static_cast<double>(ticks) * ns_per_tick_);
        }

    private:
        explicit TscClock(double ns_per_tick) noexcept
            : ns_per_tick_(ns_per_tick)
        {
        }

        double ns_per_tick_ = 0.0;
    };
}  // namespace nts
