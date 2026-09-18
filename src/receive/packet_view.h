#pragma once

#include <cstddef>
#include <cstdint>

namespace nts::receive
{
    /**
     * One received datagram, as a view into a buffer the receiver owns.
     */
    struct PacketView
    {
        const std::byte* data   = nullptr;
        std::size_t      len    = 0;
        std::uint64_t    rx_tsc = 0;
    };
}  // namespace nts::receive
