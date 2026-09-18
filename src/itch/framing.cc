#include "itch/framing.h"

#include "common/byte_reader.h"

namespace nts::itch
{
    Framer::Framer(const std::byte* data,
                   std::size_t      size) noexcept
        : data_(data)
        , size_(size)
        , pos_(0)
    {
    }

    bool Framer::next(MessageView& out) noexcept
    {
        // Subtraction, not pos_ + 2 > size_. The invariant pos_ <= size_ makes this
        // safe, whereas the addition wraps and lets a corrupt length through.
        if (size_ - pos_ < 2)
        {
            return false;
        }

        const std::uint16_t msg_len = be16(data_ + pos_);

        // A zero length is not a short message, it is a desynchronised stream: every
        // ITCH message carries at least a type byte.
        if (msg_len == 0)
        {
            return false;
        }

        if (size_ - pos_ - 2 < msg_len)
        {
            return false;
        }

        // The view starts at the type byte, so the spec's offset tables apply to it
        // unshifted. The constant shift of 2 is absorbed here and nowhere else.
        out.data = data_ + pos_ + 2;
        out.size = msg_len;

        pos_ += 2 + static_cast<std::size_t>(msg_len);
        return true;
    }

    std::size_t Framer::offset() const noexcept
    {
        return pos_;
    }

    bool Framer::exhausted() const noexcept
    {
        return pos_ == size_;
    }
}  // namespace nts::itch
