// mold_capture: an unprivileged packet capture for the validation step. p3-s6.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "mold/mold.h"

namespace
{
    void put32(std::FILE*    f,
               std::uint32_t v)
    {
        std::fwrite(&v, 4, 1, f);  // pcap headers are host order by convention
    }

    void put16(std::FILE*    f,
               std::uint16_t v)
    {
        std::fwrite(&v, 2, 1, f);
    }

    std::uint16_t ip_checksum(const unsigned char* p,
                              std::size_t          n)
    {
        std::uint32_t sum = 0;
        for (std::size_t i = 0; i + 1 < n; i += 2)
        {
            sum += static_cast<std::uint32_t>(p[i] << 8 | p[i + 1]);
        }
        while (sum >> 16)
        {
            sum = (sum & 0xFFFFu) + (sum >> 16);
        }
        return static_cast<std::uint16_t>(~sum);
    }
}  // namespace

int main(int    argc,
         char** argv)
{
    std::uint16_t port  = 26477;
    std::string   group;
    std::string   out   = "capture.pcap";
    int           count = 5;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--port" && i + 1 < argc)
        {
            port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        }
        else if (a == "--group" && i + 1 < argc)
        {
            group = argv[++i];
        }
        else if (a == "--out" && i + 1 < argc)
        {
            out = argv[++i];
        }
        else if (a == "--count" && i + 1 < argc)
        {
            count = std::atoi(argv[++i]);
        }
        else
        {
            std::fprintf(stderr, "usage: %s [--port P] [--group G] [--count N] [--out FILE]\n",
                         argv[0]);
            return 2;
        }
    }

    const int fd  = ::socket(AF_INET, SOCK_DGRAM, 0);
    int       one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in local{};
    local.sin_family      = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port        = htons(port);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof local) < 0)
    {
        std::perror("bind");
        return 1;
    }
    if (!group.empty())
    {
        ip_mreq mreq{};
        ::inet_pton(AF_INET, group.c_str(), &mreq.imr_multiaddr);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if (::setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof mreq) < 0)
        {
            std::perror("IP_ADD_MEMBERSHIP");
            return 1;
        }
    }

    std::FILE* f = std::fopen(out.c_str(), "wb");
    if (!f)
    {
        std::perror("fopen");
        return 1;
    }
    // pcap global header: magic, 2.4, zone 0, sigfigs 0, snaplen, LINKTYPE_IPV4.
    put32(f, 0xa1b2c3d4u);
    put16(f, 2);
    put16(f, 4);
    put32(f, 0);
    put32(f, 0);
    put32(f, 65535);
    put32(f, 228);

    std::vector<unsigned char> buf(65536);
    for (int n = 0; n < count; ++n)
    {
        sockaddr_in   from{};
        socklen_t     fl = sizeof from;
        const ssize_t got = ::recvfrom(fd, buf.data(), buf.size(), 0,
                                       reinterpret_cast<sockaddr*>(&from), &fl);
        if (got < 0)
        {
            std::perror("recvfrom");
            break;
        }
        timeval tv{};
        ::gettimeofday(&tv, nullptr);

        const auto    payload = static_cast<std::uint16_t>(got);
        unsigned char hdr[28] = {};
        // IPv4: version 4, IHL 5, total length, TTL 64, protocol 17 (UDP).
        const std::uint16_t total = static_cast<std::uint16_t>(20 + 8 + payload);
        hdr[0]                    = 0x45;
        hdr[2]                    = static_cast<unsigned char>(total >> 8);
        hdr[3]                    = static_cast<unsigned char>(total & 0xFF);
        hdr[8]                    = 64;
        hdr[9]                    = 17;
        std::memcpy(hdr + 12, &from.sin_addr, 4);
        in_addr dst = group.empty() ? from.sin_addr : in_addr{};
        if (!group.empty())
        {
            ::inet_pton(AF_INET, group.c_str(), &dst);
        }
        std::memcpy(hdr + 16, &dst, 4);
        const std::uint16_t csum = ip_checksum(hdr, 20);
        hdr[10]                  = static_cast<unsigned char>(csum >> 8);
        hdr[11]                  = static_cast<unsigned char>(csum & 0xFF);
        // UDP: src port, dst port, length, checksum 0 (allowed for IPv4).
        std::memcpy(hdr + 20, &from.sin_port, 2);
        const std::uint16_t dport = htons(port);
        std::memcpy(hdr + 22, &dport, 2);
        const std::uint16_t ulen = static_cast<std::uint16_t>(8 + payload);
        hdr[24]                  = static_cast<unsigned char>(ulen >> 8);
        hdr[25]                  = static_cast<unsigned char>(ulen & 0xFF);

        put32(f, static_cast<std::uint32_t>(tv.tv_sec));
        put32(f, static_cast<std::uint32_t>(tv.tv_usec));
        put32(f, static_cast<std::uint32_t>(28 + payload));
        put32(f, static_cast<std::uint32_t>(28 + payload));
        std::fwrite(hdr, 1, 28, f);
        std::fwrite(buf.data(), 1, payload, f);

        if (got >= static_cast<ssize_t>(nts::mold::kHeaderSize))
        {
            const nts::mold::Header h =
                nts::mold::decode_header(reinterpret_cast<const std::byte*>(buf.data()));
            std::printf("packet %d: %zd bytes, session '%.10s', sequence %llu, count %u\n", n, got,
                        h.session.data(), static_cast<unsigned long long>(h.sequence),
                        unsigned{h.message_count});
        }
    }
    std::fclose(f);
    ::close(fd);
    std::printf("wrote %s\n", out.c_str());
    return 0;
}
