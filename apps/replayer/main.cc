// The replayer.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

#include "common/file_buffer.h"
#include "itch/framing.h"
#include "mold/gap_injector.h"
#include "mold/mold.h"
#include "mold/packer.h"
#include "mold/rate_limiter.h"
#include "mold/udp_sender.h"

namespace
{
    using namespace nts;

    struct Options
    {
        std::string   file;
        std::string   host      = "127.0.0.1";
        std::uint16_t port      = 26477;
        double        rate      = 0.0;  ///< packets per second, 0 means unthrottled
        std::uint64_t drop_every = 0;   ///< 0 means no gaps
        double        drop_prob = 0.0;  ///< 0 means no gaps
        std::uint64_t seed      = 1;
        bool          multicast = false;
        std::string   interface = "0.0.0.0";
        std::string   session   = "ITCH191230";
        std::size_t   max_payload = mold::kMaxPayload;
        int           sndbuf    = 0;
        std::uint64_t max_packets = 0;  ///< 0 means the whole file
        std::string   schedule_log;
        std::string   drop_log;
    };

    void usage(const char* argv0)
    {
        std::fprintf(stderr,
                     "usage: %s <itch-file> [--host A] [--port P] [--rate PPS] [--drop-every N]\n"
                     "          [--drop-prob P --seed S] [--multicast [--interface A]]\n"
                     "          [--session ID] [--max-payload B] [--sndbuf B] [--max-packets N]\n"
                     "          [--schedule-log FILE] [--drop-log FILE]\n",
                     argv0);
    }

    Options parse_args(int    argc,
                       char** argv)
    {
        Options o;
        for (int i = 1; i < argc; ++i)
        {
            const std::string a = argv[i];
            auto              next = [&](const char* flag) -> const char* {
                if (i + 1 >= argc)
                {
                    std::fprintf(stderr, "%s needs a value\n", flag);
                    std::exit(2);
                }
                return argv[++i];
            };
            if (a == "--host")
            {
                o.host = next("--host");
            }
            else if (a == "--port")
            {
                o.port = static_cast<std::uint16_t>(std::atoi(next("--port")));
            }
            else if (a == "--rate")
            {
                o.rate = std::atof(next("--rate"));
            }
            else if (a == "--drop-every")
            {
                o.drop_every = std::strtoull(next("--drop-every"), nullptr, 10);
            }
            else if (a == "--drop-prob")
            {
                o.drop_prob = std::atof(next("--drop-prob"));
            }
            else if (a == "--seed")
            {
                o.seed = std::strtoull(next("--seed"), nullptr, 10);
            }
            else if (a == "--multicast")
            {
                o.multicast = true;
            }
            else if (a == "--interface")
            {
                o.interface = next("--interface");
            }
            else if (a == "--session")
            {
                o.session = next("--session");
            }
            else if (a == "--max-payload")
            {
                o.max_payload = std::strtoull(next("--max-payload"), nullptr, 10);
            }
            else if (a == "--sndbuf")
            {
                o.sndbuf = std::atoi(next("--sndbuf"));
            }
            else if (a == "--max-packets")
            {
                o.max_packets = std::strtoull(next("--max-packets"), nullptr, 10);
            }
            else if (a == "--schedule-log")
            {
                o.schedule_log = next("--schedule-log");
            }
            else if (a == "--drop-log")
            {
                o.drop_log = next("--drop-log");
            }
            else if (!a.empty() && a[0] == '-')
            {
                std::fprintf(stderr, "unknown option %s\n", a.c_str());
                usage(argv[0]);
                std::exit(2);
            }
            else
            {
                o.file = a;
            }
        }
        if (o.file.empty())
        {
            usage(argv[0]);
            std::exit(2);
        }
        return o;
    }

    struct ScheduleEntry
    {
        std::uint64_t sequence;
        std::uint16_t count;
        std::uint64_t scheduled_ns;
        std::uint64_t actual_ns;
        bool          dropped;
    };

    struct DropEntry
    {
        std::uint64_t first;
        std::uint64_t count;
        std::uint64_t packet_index;
    };
}  // namespace

int main(int    argc,
         char** argv)
{
    const Options o = parse_args(argc, argv);

    try
    {
        const std::vector<std::byte> file = nts::read_file(o.file);

        mold::UdpSender   sender(o.host, o.port, o.multicast, o.interface, o.sndbuf);
        mold::Packer      packer(mold::make_session(o.session.c_str()), 1, o.max_payload);
        mold::RateLimiter limiter(o.rate);
        mold::GapInjector gaps = o.drop_every != 0 ? mold::GapInjector::every_nth(o.drop_every)
                                 : o.drop_prob > 0.0
                                     ? mold::GapInjector::with_probability(o.drop_prob, o.seed)
                                     : mold::GapInjector::none();

        std::vector<ScheduleEntry> schedule;
        std::vector<DropEntry>     drops;
        if (!o.schedule_log.empty())
        {
            schedule.reserve(file.size() / 200);
        }

        itch::Framer      framer(file.data(), file.size());
        itch::MessageView mv;
        std::uint64_t     messages_read   = 0;
        std::uint64_t     messages_packed = 0;
        std::uint64_t     packets_built   = 0;
        std::uint64_t     packets_dropped = 0;
        std::uint64_t     too_large       = 0;
        bool              stopped_early   = false;

        // Sends or drops the packet the packer is holding. The sequence advances in
        // finish() either way, which is what makes a drop look like a real loss.
        auto flush = [&]() {
            if (packer.empty())
            {
                return;
            }
            const std::uint64_t seq   = packer.packet_sequence();
            const std::uint16_t count = packer.count();
            const auto          pkt   = packer.finish();
            const std::uint64_t index = packets_built++;
            const std::uint64_t due   = limiter.wait(index);
            const bool          drop  = gaps.should_drop(index);
            if (drop)
            {
                ++packets_dropped;
                drops.push_back({seq, count, index});
            }
            else
            {
                (void) sender.send(pkt);
            }
            if (!o.schedule_log.empty())
            {
                schedule.push_back({seq, count, due, limiter.now_ns(), drop});
            }
        };

        while (framer.next(mv))
        {
            ++messages_read;
            if (packer.too_large(mv.size))
            {
                // A message that can never fit is a configuration error, not a
                // condition to work around by splitting it. Counted and skipped, and
                // the summary will show messages packed below messages read.
                ++too_large;
                continue;
            }
            if (!packer.try_add(mv))
            {
                flush();
                if (o.max_packets != 0 && packets_built >= o.max_packets)
                {
                    stopped_early = true;
                    break;
                }
                const bool ok = packer.try_add(mv);
                if (!ok)
                {
                    std::fprintf(stderr, "packer refused a message that fits an empty packet\n");
                    return 1;
                }
            }
            ++messages_packed;
        }
        if (stopped_early)
        {
            // The message that triggered the flush was counted as read but not
            // packed; undo so the summary's read and packed agree on what was sent.
            --messages_read;
        }
        flush();
        (void) sender.send(packer.end_of_session());

        // Logs, after the run.
        if (!o.schedule_log.empty())
        {
            std::FILE* f = std::fopen(o.schedule_log.c_str(), "w");
            if (f)
            {
                std::fprintf(f, "# sequence count scheduled_ns actual_ns dropped\n");
                for (const ScheduleEntry& e : schedule)
                {
                    std::fprintf(f, "%llu %u %llu %llu %d\n",
                                 static_cast<unsigned long long>(e.sequence), unsigned{e.count},
                                 static_cast<unsigned long long>(e.scheduled_ns),
                                 static_cast<unsigned long long>(e.actual_ns), e.dropped ? 1 : 0);
                }
                std::fclose(f);
            }
        }
        if (!o.drop_log.empty())
        {
            std::FILE* f = std::fopen(o.drop_log.c_str(), "w");
            if (f)
            {
                std::fprintf(f, "# first_sequence count packet_index   (seed %llu)\n",
                             static_cast<unsigned long long>(o.seed));
                for (const DropEntry& d : drops)
                {
                    std::fprintf(f, "%llu %llu %llu\n", static_cast<unsigned long long>(d.first),
                                 static_cast<unsigned long long>(d.count),
                                 static_cast<unsigned long long>(d.packet_index));
                }
                std::fclose(f);
            }
        }

        const std::uint64_t leftover = file.size() - framer.offset();
        std::printf("file             %s\n", o.file.c_str());
        std::printf("destination      %s:%u%s\n", o.host.c_str(), unsigned{o.port},
                    o.multicast ? " (multicast)" : "");
        std::printf("session          %s\n", o.session.c_str());
        std::printf("rate             %s\n", o.rate > 0.0 ? "throttled" : "unthrottled");
        if (o.rate > 0.0)
        {
            std::printf("  packets/s      %.0f\n", o.rate);
        }
        std::printf("max payload      %zu\n", o.max_payload);
        std::printf("sndbuf           %d (granted)\n", sender.effective_send_buffer());
        std::printf("messages read    %llu\n", static_cast<unsigned long long>(messages_read));
        std::printf("messages packed  %llu\n", static_cast<unsigned long long>(messages_packed));
        std::printf("too large        %llu\n", static_cast<unsigned long long>(too_large));
        std::printf("packets built    %llu\n", static_cast<unsigned long long>(packets_built));
        std::printf("packets sent     %llu\n", static_cast<unsigned long long>(sender.packets_sent()));
        std::printf("packets dropped  %llu (injected)\n",
                    static_cast<unsigned long long>(packets_dropped));
        std::printf("send failures    %llu\n", static_cast<unsigned long long>(sender.send_failures()));
        std::printf("bytes sent       %llu\n", static_cast<unsigned long long>(sender.bytes_sent()));
        std::printf("final sequence   %llu\n",
                    static_cast<unsigned long long>(packer.packet_sequence()));
        std::printf("leftover bytes   %llu%s\n", static_cast<unsigned long long>(leftover),
                    (leftover != 0 && !stopped_early) ? "  (partial trailing message, not packed)" : "");
        if (!o.schedule_log.empty())
        {
            std::printf("schedule log     %s (%zu packets)\n", o.schedule_log.c_str(), schedule.size());
        }
        if (!o.drop_log.empty())
        {
            std::printf("drop log         %s (%zu ranges)\n", o.drop_log.c_str(), drops.size());
        }

        // The two invariants the phase 4 comparison leans on.
        const bool packed_all = (messages_packed + too_large == messages_read);
        const bool seq_ok     = (packer.packet_sequence() == 1 + messages_packed);
        std::printf("messages packed == read   %s\n", packed_all ? "yes" : "NO");
        std::printf("final seq == 1 + packed   %s\n", seq_ok ? "yes" : "NO");
        return (packed_all && seq_ok && sender.send_failures() == 0) ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 2;
    }
}
