#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "receive/ireceiver.h"

namespace nts::receive
{
    /**
     * The baseline: one blocking recvfrom per datagram.
     */
    class RecvfromReceiver final : public IReceiver
    {
    public:
        static constexpr std::size_t kBufferSize = 2048;

        RecvfromReceiver(std::string_view bind_host,
                         std::uint16_t    port,
                         bool             multicast,
                         std::string_view multicast_group = "");
        ~RecvfromReceiver() override;

        RecvfromReceiver(const RecvfromReceiver&)            = delete;
        RecvfromReceiver& operator=(const RecvfromReceiver&) = delete;

        void run(const Sink& sink) override;
        void stop() noexcept override;

        [[nodiscard]] std::uint64_t packets_received() const noexcept override
        {
            return packets_;
        }

        [[nodiscard]] std::uint64_t syscalls() const noexcept override
        {
            return syscalls_;
        }

        [[nodiscard]] const char* name() const noexcept override
        {
            return "recvfrom";
        }

        /** Datagrams longer than the buffer. Each one is a desynchronised packet. */
        [[nodiscard]] std::uint64_t truncated() const noexcept
        {
            return truncated_;
        }

        /** The port actually bound, which matters when 0 asked the kernel to pick. */
        [[nodiscard]] std::uint16_t port() const noexcept
        {
            return port_;
        }

        [[nodiscard]] int fd() const noexcept
        {
            return fd_;
        }

    private:
        int                           fd_ = -1;
        std::uint16_t                 port_ = 0;
        std::atomic<bool>             stop_{false};
        std::uint64_t                 packets_   = 0;
        std::uint64_t                 syscalls_  = 0;
        std::uint64_t                 truncated_ = 0;
        alignas(64) std::array<std::byte, kBufferSize> buffer_{};
    };
}  // namespace nts::receive
