#include "receive/recvfrom_receiver.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common/timing.h"
#include "receive/socket_setup.h"

namespace nts::receive
{
    RecvfromReceiver::RecvfromReceiver(std::string_view bind_host,
                                       std::uint16_t    port,
                                       bool             multicast,
                                       std::string_view multicast_group)
    {
        fd_ = open_udp_socket(bind_host, port, multicast, multicast_group, &port_);
    }

    RecvfromReceiver::~RecvfromReceiver()
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
        }
    }

    void RecvfromReceiver::stop() noexcept
    {
        stop_.store(true, std::memory_order_relaxed);
    }

    void RecvfromReceiver::run(const Sink& sink)
    {
        PacketView view;
        view.data = buffer_.data();

        while (!stop_.load(std::memory_order_relaxed))
        {
            // MSG_TRUNC makes the return value the datagram's real length even when it
            // did not fit, so a too-long datagram is detected rather than silently cut
            // and walked as if it were whole.
            const ssize_t n = ::recvfrom(fd_, buffer_.data(), buffer_.size(), MSG_TRUNC, nullptr,
                                         nullptr);

            // t0. The very first statement after the call returns. Nothing, not the
            // error check and not the counter, goes above this line.
            const std::uint64_t rx_tsc = rdtscp_now();

            ++syscalls_;
            if (n < 0)
            {
                if (errno == EINTR)
                {
                    continue;  // a stray signal is not the end of the stream
                }
                break;  // anything else is a real socket failure
            }
            // 0 is a legitimate empty datagram on UDP, not end of stream. It is handed
            // on like any other; the Mold decoder rejects it as too short.
            if (static_cast<std::size_t>(n) > buffer_.size())
            {
                ++truncated_;
                continue;
            }

            ++packets_;
            view.len    = static_cast<std::size_t>(n);
            view.rx_tsc = rx_tsc;
            sink(view);
        }
    }
}  // namespace nts::receive
