#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "itch/framing.h"
#include "mold/mold.h"
#include "receive/packet_view.h"

namespace nts::mold
{
    struct Gap
    {
        std::uint64_t first_missing = 0;
        std::uint64_t count         = 0;
    };

    /**
     * Strips the MoldUDP64 header, tracks the sequence, walks the message blocks.
     * p4-s3.
     *
     * Three sequence cases and all three are real. Equal to expected is the normal
     * path. Greater is a gap of exactly the difference, in messages; after it,
     * expected jumps to the incoming sequence plus its count (advancing by the count
     * of the packet that did arrive would leave the decoder permanently off by the
     * size of the hole and every later packet would report a spurious gap). Less is a
     * duplicate or a retransmission, ignored and counted separately, because it says
     * something different about the network.
     *
     * A heartbeat (count 0) and an end of session marker (0xFFFF) carry no blocks and
     * advance nothing, except that the end of session sequence is checked against
     * expected so a hole just before the end of the stream is still seen.
     *
     * A malformed packet (shorter than the header, a block running past the buffer,
     * or a stated count that disagrees with the blocks present) is rejected whole
     * before any message is delivered. Applying the messages found before the fault
     * would be worse than dropping the packet, because a partial application cannot be
     * undone. The walk is done twice for that reason: once to validate, once to
     * deliver. Both use the phase 1 Framer, whose subtraction bounds check matters far
     * more here than it did on a file, because these lengths arrived off a socket.
     *
     * The MessageView handed to the sink points into the receiver's buffer and is
     * dead when on_packet returns.
     *
     * gaps() is bounded at kMaxGaps entries. Past that, gaps are still counted in
     * gap_messages() and gaps_total() but not listed, so a pathological run cannot
     * allocate in the hot loop forever.
     */
    class Decoder
    {
    public:
        using MessageSink =
            std::function<void(const itch::MessageView&, std::uint64_t rx_tsc)>;

        static constexpr std::size_t kMaxGaps = 4096;

        Decoder();

        void on_packet(const receive::PacketView& pkt,
                       const MessageSink&         sink);

        [[nodiscard]] std::uint64_t expected_sequence() const noexcept
        {
            return expected_;
        }

        /** Sequence of the last packet delivered (or the marker's, at end of session). */
        [[nodiscard]] std::uint64_t last_sequence() const noexcept
        {
            return last_sequence_;
        }

        [[nodiscard]] std::uint64_t packets_applied() const noexcept
        {
            return applied_;
        }

        [[nodiscard]] std::uint64_t messages_delivered() const noexcept
        {
            return messages_;
        }

        [[nodiscard]] std::uint64_t duplicates() const noexcept
        {
            return duplicates_;
        }

        [[nodiscard]] std::uint64_t malformed() const noexcept
        {
            return malformed_;
        }

        [[nodiscard]] std::uint64_t heartbeats() const noexcept
        {
            return heartbeats_;
        }

        [[nodiscard]] bool end_of_session_seen() const noexcept
        {
            return eos_;
        }

        [[nodiscard]] const std::vector<Gap>& gaps() const noexcept
        {
            return gaps_;
        }

        [[nodiscard]] std::uint64_t gaps_total() const noexcept
        {
            return gaps_total_;
        }

        [[nodiscard]] std::uint64_t gap_messages() const noexcept
        {
            return gap_messages_;
        }

        [[nodiscard]] const Session& session() const noexcept
        {
            return session_;
        }

        [[nodiscard]] std::uint64_t session_changes() const noexcept
        {
            return session_changes_;
        }

    private:
        void note_gap(std::uint64_t first,
                      std::uint64_t count);

        std::uint64_t    expected_      = 1;
        std::uint64_t    last_sequence_ = 0;
        bool             started_  = false;
        bool             eos_      = false;
        Session          session_{};
        std::uint64_t    session_changes_ = 0;
        std::uint64_t    applied_         = 0;
        std::uint64_t    messages_        = 0;
        std::uint64_t    duplicates_      = 0;
        std::uint64_t    malformed_       = 0;
        std::uint64_t    heartbeats_      = 0;
        std::uint64_t    gaps_total_      = 0;
        std::uint64_t    gap_messages_    = 0;
        std::vector<Gap> gaps_;
    };
}  // namespace nts::mold
