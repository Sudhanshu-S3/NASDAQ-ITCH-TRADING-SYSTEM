#include "book/replay.h"

#include "itch/decoder.h"
#include "itch/framing.h"
#include "itch/message_type.h"

namespace nts::book
{
    namespace
    {
        Side to_side(char c) noexcept
        {
            return c == 'S' ? Side::Sell : Side::Buy;
        }

        void tally(ReplayStats& stats,
                   ApplyResult  r) noexcept
        {
            switch (r)
            {
                case ApplyResult::Applied:
                    ++stats.applied;
                    break;
                case ApplyResult::UnknownRef:
                    ++stats.unknown_ref;
                    break;
                case ApplyResult::DuplicateRef:
                    ++stats.duplicate_ref;
                    break;
                case ApplyResult::ClampedReduce:
                    ++stats.clamped_reduce;
                    break;
            }
        }
    }  // namespace

    void apply_message(BookSet&                 books,
                       const itch::MessageView& mv,
                       ReplayStats&             stats)
    {
        {
            ++stats.messages;
            const char type = static_cast<char>(mv.data[0]);

            if (!itch::is_known(type))
            {
                ++stats.unknown_type;
                return;
            }
            if (itch::expected_length(static_cast<itch::MessageType>(type)) != mv.size)
            {
                ++stats.length_mismatch;
            }

            // 'R' must land before anything routes by locate, and it is the one type
            // whose locate is not yet known, so it is handled before the known check.
            if (type == static_cast<char>(itch::MessageType::StockDirectory))
            {
                books.on_stock_directory(itch::decode_stock_directory(mv.data));
                return;
            }

            // Only the book-moving types go further. Reading the locate for 'H', 'Y',
            // 'L' and the trades would be wasted work, and they are a third of the file.
            switch (type)
            {
                case 'A':
                case 'F':
                case 'E':
                case 'C':
                case 'X':
                case 'D':
                case 'U':
                    break;
                default:
                    return;
            }

            const std::uint16_t locate = itch::decode_header(mv.data).stock_locate;
            if (!books.known(locate))
            {
                ++stats.unknown_locate;
                return;
            }
            OrderBook& b = books.book(locate);

            switch (type)
            {
                case 'A':
                case 'F':
                {
                    const itch::AddOrder m = itch::decode_add_order(mv.data);
                    tally(stats, b.add(m.order_ref, to_side(m.side), m.shares, m.price));
                    break;
                }
                case 'E':
                case 'C':
                {
                    const itch::OrderExecuted m = itch::decode_order_executed(mv.data);
                    tally(stats, b.reduce(m.order_ref, m.executed_shares));
                    break;
                }
                case 'X':
                {
                    const itch::OrderCancel m = itch::decode_order_cancel(mv.data);
                    tally(stats, b.reduce(m.order_ref, m.cancelled_shares));
                    break;
                }
                case 'D':
                {
                    const itch::OrderDelete m = itch::decode_order_delete(mv.data);
                    tally(stats, b.remove(m.order_ref));
                    break;
                }
                case 'U':
                {
                    const itch::OrderReplace m = itch::decode_order_replace(mv.data);
                    tally(stats,
                          b.replace(m.original_order_ref, m.new_order_ref, m.shares, m.price));
                    break;
                }
                default:
                    break;
            }
        }
    }

    ReplayStats replay(BookSet&         books,
                       const std::byte* data,
                       std::size_t      size)
    {
        ReplayStats       stats;
        itch::Framer      framer(data, size);
        itch::MessageView mv;
        while (framer.next(mv))
        {
            apply_message(books, mv, stats);
        }
        return stats;
    }
}  // namespace nts::book
