#include "mold/decoder.h"

namespace nts::mold
{
    Decoder::Decoder()
    {
        gaps_.reserve(kMaxGaps);
    }

    void Decoder::note_gap(std::uint64_t first,
                           std::uint64_t count)
    {
        ++gaps_total_;
        gap_messages_ += count;
        if (gaps_.size() < kMaxGaps)
        {
            gaps_.push_back({first, count});
        }
    }

    void Decoder::on_packet(const receive::PacketView& pkt,
                            const MessageSink&         sink)
    {
        if (pkt.len < kHeaderSize)
        {
            ++malformed_;
            return;
        }
        const Header h = decode_header(pkt.data);

        // The session separates "this stream lost messages" from "this is a different
        // stream". The first packet defines it; a change is counted and the sequence
        // restarts from the packet's own number rather than reporting a huge gap.
        if (!started_)
        {
            session_  = h.session;
            expected_ = h.sequence;
            started_  = true;
        }
        else if (h.session != session_)
        {
            ++session_changes_;
            session_  = h.session;
            expected_ = h.sequence;
        }

        if (h.message_count == kEndOfSession)
        {
            // A hole just before the end of the stream is visible only here.
            if (h.sequence > expected_)
            {
                note_gap(expected_, h.sequence - expected_);
                expected_ = h.sequence;
            }
            eos_           = true;
            last_sequence_ = h.sequence;
            return;
        }
        if (h.message_count == kHeartbeat)
        {
            ++heartbeats_;
            return;
        }

        if (h.sequence < expected_)
        {
            ++duplicates_;
            return;
        }

        // Validate every block before delivering any. The framer reports end at the
        // first bad prefix, so a walk that does not consume the packet exactly, or
        // that finds a different number of blocks than the header claims, is a
        // malformed packet and is rejected whole.
        {
            itch::Framer      check(pkt.data + kHeaderSize, pkt.len - kHeaderSize);
            itch::MessageView mv;
            std::uint32_t     blocks = 0;
            while (check.next(mv))
            {
                ++blocks;
            }
            if (!check.exhausted() || blocks != h.message_count)
            {
                ++malformed_;
                return;
            }
        }

        if (h.sequence > expected_)
        {
            note_gap(expected_, h.sequence - expected_);
        }

        last_sequence_ = h.sequence;
        itch::Framer      framer(pkt.data + kHeaderSize, pkt.len - kHeaderSize);
        itch::MessageView mv;
        while (framer.next(mv))
        {
            sink(mv, pkt.rx_tsc);
        }
        messages_ += h.message_count;
        ++applied_;
        expected_ = h.sequence + h.message_count;
    }
}  // namespace nts::mold
