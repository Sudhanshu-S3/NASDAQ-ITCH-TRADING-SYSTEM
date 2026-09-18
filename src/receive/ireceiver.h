#pragma once

#include <cstdint>
#include <functional>

#include "receive/packet_view.h"

namespace nts::receive
{
    /**
     * Produces raw datagram buffers. Knows nothing about ITCH or MoldUDP64.
     */
    class IReceiver
    {
    public:
        using Sink = std::function<void(const PacketView&)>;

        virtual ~IReceiver() = default;

        virtual void run(const Sink& sink) = 0;
        virtual void stop() noexcept       = 0;

        [[nodiscard]] virtual std::uint64_t packets_received() const noexcept = 0;

        /** Blocking calls into the kernel made to receive packets_received() packets. */
        [[nodiscard]] virtual std::uint64_t syscalls() const noexcept = 0;

        /** A short name for reports. */
        [[nodiscard]] virtual const char* name() const noexcept = 0;
    };
}  // namespace nts::receive
