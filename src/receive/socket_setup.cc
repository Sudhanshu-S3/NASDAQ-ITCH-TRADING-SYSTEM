#include "receive/socket_setup.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace nts::receive
{
    int open_udp_socket(std::string_view bind_host,
                        std::uint16_t    port,
                        bool             multicast,
                        std::string_view multicast_group,
                        std::uint16_t*   bound_port)
    {
        const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0)
        {
            throw std::runtime_error(std::string("socket: ") + std::strerror(errno));
        }

        auto fail = [fd](const char* what) {
            const int e = errno;
            ::close(fd);
            throw std::runtime_error(std::string(what) + ": " + std::strerror(e));
        };

        const int one = 1;
        if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) < 0)
        {
            fail("SO_REUSEADDR");
        }

        const std::string host(bind_host);
        sockaddr_in       addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(port);
        if (multicast)
        {
            // Bind the wildcard so datagrams addressed to the group are accepted; the
            // interface choice lives in the membership below, not in the bind.
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
        }
        else if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1)
        {
            fail("bad bind address");
        }
        if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) < 0)
        {
            fail("bind");
        }

        if (multicast)
        {
            const std::string group(multicast_group);
            ip_mreq           mreq{};
            if (::inet_pton(AF_INET, group.c_str(), &mreq.imr_multiaddr) != 1)
            {
                fail("bad multicast group");
            }
            if (::inet_pton(AF_INET, host.c_str(), &mreq.imr_interface) != 1)
            {
                mreq.imr_interface.s_addr = htonl(INADDR_ANY);
            }
            if (::setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof mreq) < 0)
            {
                fail("IP_ADD_MEMBERSHIP");
            }
        }

        if (bound_port)
        {
            sockaddr_in got{};
            socklen_t   len = sizeof got;
            if (::getsockname(fd, reinterpret_cast<sockaddr*>(&got), &len) < 0)
            {
                fail("getsockname");
            }
            *bound_port = ntohs(got.sin_port);
        }
        return fd;
    }
}  // namespace nts::receive
