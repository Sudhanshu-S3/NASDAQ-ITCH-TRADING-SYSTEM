#pragma once

#include <cstddef>

namespace nts::itch
{
    /**
     * One framed message, as a view into a buffer somebody else owns.
     *
     * data points at the message type byte, not at the length prefix, so offsets used
     * against it match the spec's offset tables with no constant shift to remember.
     * size excludes the prefix. Valid only while the underlying buffer is alive.
     */
    struct MessageView
    {
        const std::byte* data = nullptr;
        std::size_t      size = 0;
    };

    /**
     * Walks a length prefixed ITCH message stream.
     *
     * Framing is also where bounds checking lives. Because the walker has proved that
     * size bytes are present before handing out a view, the byte readers stay noexcept
     * and unchecked. That split is why be16 and friends take no length argument, and
     * changing it changes common/byte_reader.h too.
     *
     * The walker never trusts a length. A prefix claiming more bytes than remain means
     * a truncated or corrupt stream, and next() reports end rather than reading past
     * the buffer. Every bound is compared by subtraction, never by adding to pos_,
     * because pos_ + n wraps on size_t and a wrapped sum passes a naive check.
     */
    class Framer
    {
    public:
        Framer(const std::byte* data,
               std::size_t      size) noexcept;

        [[nodiscard]] bool next(MessageView& out) noexcept;

        /** Byte offset of the next unread prefix. Report this when a frame is bad. */
        [[nodiscard]] std::size_t offset() const noexcept;

        /**
         * True only if the walk consumed the buffer exactly, with nothing trailing.
         */
        [[nodiscard]] bool exhausted() const noexcept;

    private:
        const std::byte* data_ = nullptr;
        std::size_t      size_ = 0;
        std::size_t      pos_  = 0;
    };
}  // namespace nts::itch
