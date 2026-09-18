#pragma once

#include <cstddef>
#include <cstdint>

namespace nts
{
    /**
     * Big endian field readers over a raw wire buffer.
     *
     * Every multi byte field in ITCH, MoldUDP64, and OUCH is read through these and
     * through nothing else. The width is in the name, so there is no length argument
     * and no way for a caller to pass the wrong one silently.
     *
     * The caller owns the buffer and owns bounds checking. These read exactly their
     * width from p and cannot fail, so a read past the end is a caller bug, not a
     * condition to report. That is why they are noexcept, and why bounds checking
     * lives in itch::Framer, where the message length is known. Changing that split
     * changes both files.
     */

    [[nodiscard]] inline constexpr std::uint16_t be16(const std::byte* p) noexcept
    {
        // Accumulate in a type that is not narrower than int. A uint16_t accumulator
        // is promoted to int by the shift, and assigning that back is a narrowing
        // conversion that clang flags and gcc does not.
        std::uint32_t res = 0;
        for (int i = 0; i < 2; ++i)
        {
            res = (res << 8) | (std::to_integer<std::uint32_t>(p[i]));
        }
        return static_cast<std::uint16_t>(res);
    }

    [[nodiscard]] inline constexpr std::uint32_t be32(const std::byte* p) noexcept
    {
        std::uint32_t res = 0;
        for (int i = 0; i < 4; ++i)
        {
            res = (res << 8) | (std::to_integer<std::uint32_t>(p[i]));
        }
        return res;
    }

    // No native 48 bit type and no 48 bit byte swap instruction, so this one is
    // assembled by hand and widened to 64.
    [[nodiscard]] inline constexpr std::uint64_t be48(const std::byte* p) noexcept
    {
        std::uint64_t res = 0;
        for (int i = 0; i < 6; ++i)
        {
            res = (res << 8) | (std::to_integer<std::uint64_t>(p[i]));
        }
        return res;
    }

    [[nodiscard]] inline constexpr std::uint64_t be64(const std::byte* p) noexcept
    {
        std::uint64_t res = 0;
        for (int i = 0; i < 8; ++i)
        {
            res = (res << 8) | (std::to_integer<std::uint64_t>(p[i]));
        }
        return res;
    }
}  // namespace nts
