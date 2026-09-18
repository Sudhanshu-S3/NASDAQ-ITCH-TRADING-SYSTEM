#pragma once

#include <array>
#include <cstdint>

namespace nts::itch
{
    /**
     * Decoded forms of the messages order book read and acts on.
     */

    using Stock = std::array<char, 8>;

    /** Fields at the front of every message. Decoded once, then the type is switched on. */
    struct Header
    {
        std::uint16_t stock_locate    = 0;
        std::uint16_t tracking_number = 0;
        std::uint64_t timestamp       = 0;  ///< nanoseconds since midnight, 6 bytes on the wire
    };

    struct SystemEvent
    {
        Header header;
        char   event_code = 0;
    };

    /** Populates the symbol table in p1-s6. Carries no order and never moves a book. */
    struct StockDirectory
    {
        Header        header;
        Stock         stock{};
        char          market_category = 0;
        std::uint32_t round_lot_size  = 0;
    };

    /** 'H'. Relays changes in trading status for an individual security. */
    struct StockTradingAction
    {
        Header              header;
        Stock               stock{};
        char                trading_state = 0;
        char                reserved      = 0;
        std::array<char, 4> reason{};
    };

    /** 'Y'. Denotes the Reg SHO Short Sale Price Test Restriction status. */
    struct RegShoRestriction
    {
        Header header;
        Stock  stock{};
        char   reg_sho_action = 0;
    };

    /** 'L'. Provides the Market Maker mode and participant state for a firm in an issue. */
    struct MarketParticipantPosition
    {
        Header              header;
        std::array<char, 4> mpid{};
        Stock               stock{};
        char                primary_market_maker     = 0;
        char                market_maker_mode        = 0;
        char                market_participant_state = 0;
    };

    /** 'A' and 'F'. F carries a 4 byte MPID the book does not use. */
    struct AddOrder
    {
        Header        header;
        std::uint64_t order_ref = 0;
        char          side      = 0;  ///< 'B' or 'S'
        std::uint32_t shares    = 0;
        Stock         stock{};
        std::uint32_t price = 0;
    };

    /**
     * 'E' and 'C'. C additionally carries an execution price and a printable flag.
     *
     * Neither names a stock or a side: the order reference is the only key, and
     * everything else is looked up from the order the book already holds. That is why
     * the order index has to be right before any of these can be applied.
     */
    struct OrderExecuted
    {
        Header        header;
        std::uint64_t order_ref       = 0;
        std::uint32_t executed_shares = 0;
        std::uint64_t match_number    = 0;
        std::uint32_t execution_price = 0;     ///< 'C' only
        bool          printable       = true;  ///< 'C' only
    };

    /** 'X'. A partial reduction, not a removal. */
    struct OrderCancel
    {
        Header        header;
        std::uint64_t order_ref        = 0;
        std::uint32_t cancelled_shares = 0;
    };

    /** 'D'. Removes the whole remaining quantity. */
    struct OrderDelete
    {
        Header        header;
        std::uint64_t order_ref = 0;
    };

    /**
     * 'U'. Carries a new reference number, so the old one dies here.
     *
     * Side and stock are not on the wire; they carry over from the original order.
     * This is checkpoint question 1 for the phase, so understand before implementing
     * why a replace cannot be treated as an in place edit of price and shares.
     */
    struct OrderReplace
    {
        Header        header;
        std::uint64_t original_order_ref = 0;
        std::uint64_t new_order_ref      = 0;
        std::uint32_t shares             = 0;
        std::uint32_t price              = 0;
    };

    /**
     * 'P' and 'Q'. Reported for the tape only.
     *
     * These do not touch the displayed book. A non cross trade reports an execution
     * against hidden liquidity that was never in the book, so applying it would double
     * count against the visible orders. This is checkpoint question 2.
     */
    struct Trade
    {
        Header        header;
        std::uint64_t order_ref = 0;
        char          side      = 0;

        // 64 bit, not 32, because 'Q' carries an 8 byte share count while 'P' carries
        // 4. Narrowing the cross to 32 bits would silently truncate the opening and
        // closing auction prints, which are the largest of the day.
        std::uint64_t shares = 0;

        Stock         stock{};
        std::uint32_t price        = 0;
        std::uint64_t match_number = 0;

        // 'P' only. A cross has no resting order and no side.
        bool has_order = false;
    };

    /** 'B'. Sent whenever an execution on Nasdaq is broken. */
    struct BrokenTrade
    {
        Header        header;
        std::uint64_t match_number = 0;
    };
}  // namespace nts::itch
