#pragma once

#include <cstdint>
#include <string_view>

namespace nts::receive
{
    /**
     * Creates, configures and binds the UDP socket every kernel-side receiver uses.
     */
    int open_udp_socket(std::string_view bind_host,
                        std::uint16_t    port,
                        bool             multicast,
                        std::string_view multicast_group,
                        std::uint16_t*   bound_port);
}  // namespace nts::receive
