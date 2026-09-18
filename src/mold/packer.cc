#include "mold/packer.h"

#include <cstring>

namespace nts::mold
{
    Packer::Packer(Session       session,
                   std::uint64_t first_sequence,
                   std::size_t   max_payload)
        : session_(session)
        , sequence_(first_sequence)
        , max_payload_(max_payload)
        , buffer_(kHeaderSize + max_payload)
    {
    }

    bool Packer::try_add(const itch::MessageView& msg) noexcept
    {
        const std::size_t room = max_payload_ - body_;
        if (room < 2 || room - 2 < msg.size)
        {
            return false;
        }
        // 0xFFFF is the end of session marker, so a count can never reach it.
        if (count_ == kEndOfSession - 1)
        {
            return false;
        }

        std::byte* block = buffer_.data() + kHeaderSize + body_;
        block[0]         = static_cast<std::byte>(msg.size >> 8);
        block[1]         = static_cast<std::byte>(msg.size & 0xFFu);
        std::memcpy(block + 2, msg.data, msg.size);

        body_ += 2 + msg.size;
        ++count_;
        return true;
    }

    std::span<const std::byte> Packer::finish() noexcept
    {
        Header h;
        h.session       = session_;
        h.sequence      = sequence_;
        h.message_count = count_;
        encode_header(h, buffer_.data());

        const std::span<const std::byte> packet(buffer_.data(), kHeaderSize + body_);

        sequence_ += count_;
        body_  = 0;
        count_ = 0;
        return packet;
    }

    std::span<const std::byte> Packer::end_of_session() noexcept
    {
        Header h;
        h.session       = session_;
        h.sequence      = sequence_;
        h.message_count = kEndOfSession;
        encode_header(h, buffer_.data());
        return {buffer_.data(), kHeaderSize};
    }
}  // namespace nts::mold
