#include "common/timing.h"

#include <fstream>
#include <string>
#include <thread>

namespace nts
{
    namespace
    {
        // The flags are per processor and identical across them on any machine this
        // project would run on, so the first flags line answers the question and the
        // rest of a 12 core cpuinfo does not have to be read.
        bool cpuinfo_flags_contain(const char* flag)
        {
            std::ifstream info("/proc/cpuinfo");
            if (!info)
            {
                return false;
            }

            const std::string needle = std::string(" ") + flag + " ";

            std::string line;
            while (std::getline(info, line))
            {
                if (line.rfind("flags", 0) != 0)
                {
                    continue;
                }
                // Pad both ends so that a flag name that is a prefix of another one
                // cannot match. Without this, "tsc" matches "constant_tsc".
                const std::string padded = " " + line + " ";
                return padded.find(needle) != std::string::npos;
            }
            return false;
        }
    }  // namespace

    bool TscClock::invariant_tsc_available() noexcept
    {
        // noexcept over code that allocates, deliberately. There is nothing sensible
        // to do with an exception from reading /proc at startup, and terminating is
        // more honest than reporting an uncalibrated clock as usable.
        return cpuinfo_flags_contain("constant_tsc") && cpuinfo_flags_contain("nonstop_tsc");
    }

    TscClock TscClock::calibrate(std::chrono::milliseconds window)
    {
        // Order matters. The wall clock is read outside the TSC reads on both ends, so
        // the elapsed wall time is never smaller than the elapsed ticks it is divided
        // into. The error that produces is a tick rate biased low by the cost of one
        // clock_gettime over the whole window, which at 200ms is under one part in a
        // million.
        const auto          wall_start = std::chrono::steady_clock::now();
        const std::uint64_t tsc_start  = rdtscp_now();

        std::this_thread::sleep_for(window);

        const std::uint64_t tsc_end  = rdtscp_now();
        const auto          wall_end = std::chrono::steady_clock::now();

        const auto elapsed_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(wall_end - wall_start).count();
        const std::uint64_t elapsed_ticks = tsc_end - tsc_start;

        if (elapsed_ticks == 0 || elapsed_ns <= 0)
        {
            return TscClock(0.0);
        }

        return TscClock(static_cast<double>(elapsed_ns) / static_cast<double>(elapsed_ticks));
    }
}  // namespace nts
