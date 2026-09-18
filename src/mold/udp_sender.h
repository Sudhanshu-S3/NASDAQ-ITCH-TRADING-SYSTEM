#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <netinet/in.h>

namespace nts::mold
{
    /**
     * One UDP socket aimed at one destination.
     *
     * A UDP datagram is atomic: sendto either hands the whole datagram to the socket
     * or fails. There is no short send to loop over the way there is on TCP, so a
     * return value that is not the full length is a bug in the call, not a partial
     * write to retry.
     */
    class UdpSender
    {
    public:
        /**
         * multicast: treat host as a group address and set the multicast options.
         * interface: the local address to send multicast from; ignored for unicast.
         * send_buffer: SO_SNDBUF request in bytes, 0 leaves the default. Recorded
         * with every measurement because it changes where backpressure appears.
         */
        UdpSender(std::string_view host,
                  std::uint16_t    port,
                  bool             multicast,
                  std::string_view interface = "0.0.0.0",
                  int              send_buffer = 0);
        ~UdpSender();

        UdpSender(const UdpSender&)            = delete;
        UdpSender& operator=(const UdpSender&) = delete;

        /** One sendto. False means the datagram was not sent; errno is preserved. */
        [[nodiscard]] bool send(std::span<const std::byte> packet) noexcept;

        [[nodiscard]] std::uint64_t packets_sent() const noexcept
        {
            return packets_;
        }

        [[nodiscard]] std::uint64_t bytes_sent() const noexcept
        {
            return bytes_;
        }

        [[nodiscard]] std::uint64_t send_failures() const noexcept
        {
            return failures_;
        }

        /** The SO_SNDBUF the kernel actually granted, which it may have doubled or capped. */
        [[nodiscard]] int effective_send_buffer() const noexcept
        {
            return effective_sndbuf_;
        }

        [[nodiscard]] int fd() const noexcept
        {
            return fd_;
        }

    private:
        int           fd_ = -1;
        sockaddr_in   dest_{};
        std::uint64_t packets_         = 0;
        std::uint64_t bytes_           = 0;
        std::uint64_t failures_        = 0;
        int           effective_sndbuf_ = 0;
    };
}  // namespace nts::mold
