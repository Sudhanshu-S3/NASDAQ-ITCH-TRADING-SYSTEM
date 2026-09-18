#include "mold/udp_sender.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

namespace nts::mold
{
    namespace
    {
        [[noreturn]] void fail(const char* what)
        {
            throw std::runtime_error(std::string(what) + ": " + std::strerror(errno));
        }
    }  // namespace

    UdpSender::UdpSender(std::string_view host,
                         std::uint16_t    port,
                         bool             multicast,
                         std::string_view interface,
                         int              send_buffer)
    {
        fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd_ < 0)
        {
            fail("socket");
        }

        // inet_pton wants a terminated string and a string_view carries no terminator.
        const std::string host_str(host);
        dest_.sin_family = AF_INET;
        dest_.sin_port   = htons(port);
        if (::inet_pton(AF_INET, host_str.c_str(), &dest_.sin_addr) != 1)
        {
            ::close(fd_);
            throw std::runtime_error("bad IPv4 address: " + host_str);
        }

        if (multicast)
        {
            const unsigned char ttl  = 1;
            const unsigned char loop = 1;
            in_addr             ifaddr{};
            const std::string   if_str(interface);
            if (::inet_pton(AF_INET, if_str.c_str(), &ifaddr) != 1)
            {
                ::close(fd_);
                throw std::runtime_error("bad interface address: " + if_str);
            }
            if (::setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl) < 0 ||
                ::setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof loop) < 0 ||
                ::setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_IF, &ifaddr, sizeof ifaddr) < 0)
            {
                ::close(fd_);
                fail("setsockopt multicast");
            }
        }

        if (send_buffer > 0)
        {
            if (::setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof send_buffer) < 0)
            {
                ::close(fd_);
                fail("setsockopt SO_SNDBUF");
            }
        }
        socklen_t len = sizeof effective_sndbuf_;
        ::getsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &effective_sndbuf_, &len);
    }

    UdpSender::~UdpSender()
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
        }
    }

    bool UdpSender::send(std::span<const std::byte> packet) noexcept
    {
        const ssize_t n = ::sendto(fd_, packet.data(), packet.size(), 0,
                                   reinterpret_cast<const sockaddr*>(&dest_), sizeof dest_);
        if (n < 0 || static_cast<std::size_t>(n) != packet.size())
        {
            ++failures_;
            return false;
        }
        ++packets_;
        bytes_ += packet.size();
        return true;
    }
}  // namespace nts::mold
