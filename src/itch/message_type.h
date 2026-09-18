#pragma once

#include <cstddef>

namespace nts::itch
{
    /**
     * ITCH 5.0 message type bytes.
     * Every other type byte is still legal on the wire and gets skipped by its length prefix,
     * so this is not a whitelist of what may appear in the file.
     */
    enum class MessageType : char
    {
        SystemEvent            = 'S',
        StockDirectory         = 'R',
        StockTradingAction     = 'H',
        RegShoRestriction      = 'Y',
        MarketParticipantPos   = 'L',

        AddOrder               = 'A',
        AddOrderMpid           = 'F',
        OrderExecuted          = 'E',
        OrderExecutedWithPrice = 'C',
        OrderCancel            = 'X',
        OrderDelete            = 'D',
        OrderReplace           = 'U',
        TradeNonCross          = 'P',
        TradeCross             = 'Q',
        BrokenTrade            = 'B',
    };

    /** True if the byte is a type this build knows how to name. */
    [[nodiscard]] constexpr bool is_known(char type) noexcept
    {
        switch (type)
        {
            case 'S':
            case 'R':
            case 'H':
            case 'Y':
            case 'L':
            case 'A':
            case 'F':
            case 'E':
            case 'C':
            case 'X':
            case 'D':
            case 'U':
            case 'P':
            case 'Q':
            case 'B':
                return true;
            default:
                return false;
        }
    }

    /**
     * Total message length in bytes for a known type, including the type byte but
     * excluding the 2 byte length prefix. Returns 0 for a type not in the table.
     */
    [[nodiscard]] constexpr std::size_t expected_length(MessageType type) noexcept
    {
        switch (type)
        {
            case MessageType::SystemEvent:            return 12;
            case MessageType::StockDirectory:         return 39;
            case MessageType::StockTradingAction:     return 25;
            case MessageType::RegShoRestriction:      return 20;
            case MessageType::MarketParticipantPos:   return 26;
            case MessageType::AddOrder:               return 36;
            case MessageType::AddOrderMpid:           return 40;
            case MessageType::OrderExecuted:          return 31;
            case MessageType::OrderExecutedWithPrice: return 36;
            case MessageType::OrderCancel:            return 23;
            case MessageType::OrderDelete:            return 19;
            case MessageType::OrderReplace:           return 35;
            case MessageType::TradeNonCross:          return 44;
            case MessageType::TradeCross:             return 40;
            case MessageType::BrokenTrade:            return 19;
            default:                                  return 0;
        }
    }
}