#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace nts::mold
{
    /**
     * MoldUDP64 framing.
     */

    inline constexpr std::size_t   kSessionSize  = 10;
    inline constexpr std::size_t   kHeaderSize   = 20;
    inline constexpr std::uint16_t kEndOfSession = 0xFFFF;
    inline constexpr std::uint16_t kHeartbeat    = 0;

    inline constexpr std::size_t kMaxPayload = 1400;

    using Session = std::array<char, kSessionSize>;

    struct Header
    {
        Session       session{};
        std::uint64_t sequence      = 0;
        std::uint16_t message_count = 0;
    };

    /** Writes exactly kHeaderSize bytes at out. Byte by byte, no casts. */
    void encode_header(const Header& h,
                       std::byte*    out) noexcept;

    /** Reads exactly kHeaderSize bytes at p. The caller has proved they are present. */
    [[nodiscard]] Header decode_header(const std::byte* p) noexcept;

    /** Space pads a short id to kSessionSize; truncates a long one. */
    [[nodiscard]] Session make_session(const char* text) noexcept;
}  // namespace nts::mold
