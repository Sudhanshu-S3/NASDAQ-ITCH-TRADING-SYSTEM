#pragma once

#include <cstddef>
#include <cstdint>

namespace nts
{
    /**
     * Big endian field readers over a raw wire buffer.
     */

    [[nodiscard]] inline constexpr std::uint16_t be16(const std::byte* p) noexcept
    {
        std::uint16_t res = 0;
        for (int i = 0; i < 2; ++i)
        {
            res = (res << 8) | (std::to_integer<std::uint16_t>(p[i]));
        }
        return res;
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