#include "mold/mold.h"

#include "common/byte_reader.h"

namespace nts::mold
{
    namespace
    {
        void put_be16(std::byte*    out,
                      std::uint16_t v) noexcept
        {
            out[0] = static_cast<std::byte>(v >> 8);
            out[1] = static_cast<std::byte>(v & 0xFFu);
        }

        void put_be64(std::byte*    out,
                      std::uint64_t v) noexcept
        {
            for (int i = 7; i >= 0; --i)
            {
                out[i] = static_cast<std::byte>(v & 0xFFu);
                v >>= 8;
            }
        }
    }  // namespace

    void encode_header(const Header& h,
                       std::byte*    out) noexcept
    {
        for (std::size_t i = 0; i < kSessionSize; ++i)
        {
            out[i] = static_cast<std::byte>(h.session[i]);
        }
        put_be64(out + kSessionSize, h.sequence);
        put_be16(out + kSessionSize + 8, h.message_count);
    }

    Header decode_header(const std::byte* p) noexcept
    {
        Header h;
        for (std::size_t i = 0; i < kSessionSize; ++i)
        {
            h.session[i] = static_cast<char>(p[i]);
        }
        h.sequence      = be64(p + kSessionSize);
        h.message_count = be16(p + kSessionSize + 8);
        return h;
    }

    Session make_session(const char* text) noexcept
    {
        Session s;
        s.fill(' ');
        for (std::size_t i = 0; i < kSessionSize && text[i] != '\0'; ++i)
        {
            s[i] = text[i];
        }
        return s;
    }
}  // namespace nts::mold
