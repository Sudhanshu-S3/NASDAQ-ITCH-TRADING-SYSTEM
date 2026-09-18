#include "itch/decoder.h"

#include "common/byte_reader.h"
#include "itch/message_type.h"

namespace nts::itch
{
    namespace
    {
        // Alpha fields keep their space padding. The padding is part of the field, and
        // trimming here would mean every later comparison had to know which it had.
        void read_stock(const std::byte* p,
                        Stock&           stock) noexcept
        {
            for (std::size_t i = 0; i < stock.size(); ++i)
            {
                stock[i] = static_cast<char>(p[i]);
            }
        }
    }  // namespace

    Header decode_header(const std::byte* p) noexcept
    {
        Header h;
        h.stock_locate    = be16(p + 1);
        h.tracking_number = be16(p + 3);
        h.timestamp       = be48(p + 5);
        return h;
    }

    SystemEvent decode_system_event(const std::byte* p) noexcept
    {
        SystemEvent msg;
        msg.header     = decode_header(p);
        msg.event_code = static_cast<char>(p[11]);
        return msg;
    }

    StockDirectory decode_stock_directory(const std::byte* p) noexcept
    {
        StockDirectory msg;
        msg.header = decode_header(p);
        read_stock(p + 11, msg.stock);
        msg.market_category = static_cast<char>(p[19]);
        // Offset 20 is the financial status indicator, which the book does not use.
        msg.round_lot_size  = be32(p + 21);
        return msg;
    }

    AddOrder decode_add_order(const std::byte* p) noexcept
    {
        // 'A' and 'F' share every offset below. 'F' adds a 4 byte MPID at offset 36,
        // outside the range read here, so one decoder serves both.
        AddOrder msg;
        msg.header    = decode_header(p);
        msg.order_ref = be64(p + 11);
        msg.side      = static_cast<char>(p[19]);
        msg.shares    = be32(p + 20);
        read_stock(p + 24, msg.stock);
        msg.price = be32(p + 32);
        return msg;
    }

    OrderExecuted decode_order_executed(const std::byte* p) noexcept
    {
        OrderExecuted msg;
        msg.header          = decode_header(p);
        msg.order_ref       = be64(p + 11);
        msg.executed_shares = be32(p + 19);
        msg.match_number    = be64(p + 23);

        // 'C' extends 'E' with two more fields rather than reshaping it, so the shared
        // prefix above is read once and only the tail is conditional.
        if (static_cast<char>(p[0]) == static_cast<char>(MessageType::OrderExecutedWithPrice))
        {
            msg.printable       = (static_cast<char>(p[31]) == 'Y');
            msg.execution_price = be32(p + 32);
        }

        return msg;
    }

    OrderCancel decode_order_cancel(const std::byte* p) noexcept
    {
        OrderCancel msg;
        msg.header           = decode_header(p);
        msg.order_ref        = be64(p + 11);
        msg.cancelled_shares = be32(p + 19);
        return msg;
    }

    OrderDelete decode_order_delete(const std::byte* p) noexcept
    {
        OrderDelete msg;
        msg.header    = decode_header(p);
        msg.order_ref = be64(p + 11);
        return msg;
    }

    OrderReplace decode_order_replace(const std::byte* p) noexcept
    {
        OrderReplace msg;
        msg.header             = decode_header(p);
        msg.original_order_ref = be64(p + 11);
        msg.new_order_ref      = be64(p + 19);
        msg.shares             = be32(p + 27);
        msg.price              = be32(p + 31);
        return msg;
    }

    Trade decode_trade(const std::byte* p) noexcept
    {
        Trade msg;
        msg.header = decode_header(p);

        if (static_cast<char>(p[0]) == static_cast<char>(MessageType::TradeNonCross))
        {
            msg.has_order = true;
            msg.order_ref = be64(p + 11);
            msg.side      = static_cast<char>(p[19]);
            msg.shares    = be32(p + 20);
            read_stock(p + 24, msg.stock);
            msg.price        = be32(p + 32);
            msg.match_number = be64(p + 36);
        }
        else
        {
            // 'Q'. A cross has no resting order and no side, and its share count is 8
            // bytes rather than 4, because the opening and closing auctions print the
            // largest quantities of the day.
            msg.has_order = false;
            msg.side      = ' ';
            msg.shares    = be64(p + 11);
            read_stock(p + 19, msg.stock);
            msg.price        = be32(p + 27);
            msg.match_number = be64(p + 31);
            // Offset 39 is the cross type. The book does not use it.
        }

        return msg;
    }
}  // namespace nts::itch
