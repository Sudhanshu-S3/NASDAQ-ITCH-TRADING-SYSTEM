#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "itch/framing.h"
#include "mold/mold.h"

namespace nts::mold
{
    /**
     * Packs ITCH messages into MoldUDP64 packets.
     */
    class Packer
    {
    public:
        Packer(Session       session,
               std::uint64_t first_sequence,
               std::size_t   max_payload);

        /** False means this packet is full. The caller sends, then retries the same message. */
        [[nodiscard]] bool try_add(const itch::MessageView& msg) noexcept;

        [[nodiscard]] std::span<const std::byte> finish() noexcept;

        /** A header-only packet with count kEndOfSession. Advances nothing. */
        [[nodiscard]] std::span<const std::byte> end_of_session() noexcept;

        /** Sequence number the next message added will receive. */
        [[nodiscard]] std::uint64_t next_sequence() const noexcept
        {
            return sequence_ + count_;
        }

        /** Sequence number of the first message in the packet being built. */
        [[nodiscard]] std::uint64_t packet_sequence() const noexcept
        {
            return sequence_;
        }

        [[nodiscard]] std::uint16_t count() const noexcept
        {
            return count_;
        }

        [[nodiscard]] bool empty() const noexcept
        {
            return count_ == 0;
        }

        [[nodiscard]] std::size_t body_size() const noexcept
        {
            return body_;
        }

        /** True if a message could never fit even in an empty packet. */
        [[nodiscard]] bool too_large(std::size_t message_size) const noexcept
        {
            return 2 + message_size > max_payload_;
        }

    private:
        Session                session_;
        std::uint64_t          sequence_;
        std::size_t            max_payload_;
        std::vector<std::byte> buffer_;  ///< kHeaderSize + max_payload, allocated once
        std::size_t            body_  = 0;
        std::uint16_t          count_ = 0;
    };
}  // namespace nts::mold
