// The offline driver for phase 1. Reads an ITCH 5.0 daily file, walks it by length
// prefix, decodes every message, folds the book-moving ones into a per-symbol book,
// and prints a summary.
//
// This is the only place in phase 1 allowed to print, and it prints once, after the
// walk. A write syscall per message would dominate the whole parser and make every
// later measurement meaningless.
//
// The wire mode that reads from a socket arrives in phase 4, once the replayer from
// phase 3 has something to send to.

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <map>
#include <vector>

#include "book/book_set.h"
#include "common/file_buffer.h"
#include "itch/decoder.h"
#include "itch/framing.h"
#include "itch/message_type.h"

namespace
{
    using namespace nts;

    book::Side to_side(char c) noexcept
    {
        return c == 'S' ? book::Side::Sell : book::Side::Buy;
    }

    // check_invariants rebuilds both sides from the order index, so it is linear in
    // live orders and can only be sampled. check_invariants_fast is constant time and
    // runs after every message, which is what p1-s7's "continuous" requires.
    constexpr std::size_t kInvariantInterval = 250'000;

    /**
     * What the message stream said, tallied independently of the book.
     *
     * The point of every counter here is that the book's own state can be checked
     * against it. A decode bug that shifts order references produces an empty book and
     * a huge unknown_ref count; a bug in a share field breaks conservation. Either way
     * something says so, which is the whole difference between validation and running
     * without crashing.
     */
    struct Audit
    {
        std::size_t applied        = 0;
        std::size_t unknown_ref    = 0;
        std::size_t duplicate_ref  = 0;
        std::size_t clamped_reduce = 0;

        std::uint64_t shares_added     = 0;
        std::uint64_t shares_executed  = 0;
        std::uint64_t shares_cancelled = 0;
        std::uint64_t shares_deleted   = 0;

        void tally(book::ApplyResult r) noexcept
        {
            switch (r)
            {
                case book::ApplyResult::Applied:
                    ++applied;
                    break;
                case book::ApplyResult::UnknownRef:
                    ++unknown_ref;
                    break;
                case book::ApplyResult::DuplicateRef:
                    ++duplicate_ref;
                    break;
                case book::ApplyResult::ClampedReduce:
                    ++clamped_reduce;
                    break;
            }
        }

        [[nodiscard]] std::uint64_t expected_resting() const noexcept
        {
            return shares_added - shares_executed - shares_cancelled - shares_deleted;
        }
    };
}  // namespace

int main(int    argc,
         char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s <itch-file>\n", argv[0]);
        return 2;
    }

    try
    {
        const std::vector<std::byte> file = nts::read_file(argv[1]);

        itch::Framer      framer(file.data(), file.size());
        itch::MessageView mv;
        book::BookSet     books;

        std::map<char, std::size_t> counts;
        Audit                       audit;
        std::size_t                 messages        = 0;
        std::size_t                 unknown_type    = 0;
        std::size_t                 length_mismatch = 0;
        std::size_t                 unknown_locate  = 0;
        bool                        invariants_ok   = true;

        while (framer.next(mv))
        {
            ++messages;
            const char type = static_cast<char>(mv.data[0]);
            ++counts[type];

            if (!itch::is_known(type))
            {
                ++unknown_type;
                continue;
            }
            if (itch::expected_length(static_cast<itch::MessageType>(type)) != mv.size)
            {
                ++length_mismatch;
            }

            // Directory messages must be applied before anything routes by locate, so
            // this switch handles 'R' first and every order path checks known().
            if (type == 'R')
            {
                books.on_stock_directory(itch::decode_stock_directory(mv.data));
                continue;
            }

            const std::uint16_t locate = itch::decode_header(mv.data).stock_locate;

            switch (type)
            {
                case 'A':
                case 'F':
                {
                    const itch::AddOrder m = itch::decode_add_order(mv.data);
                    if (!books.known(locate))
                    {
                        ++unknown_locate;
                        break;
                    }
                    const auto r =
                        books.book(locate).add(m.order_ref, to_side(m.side), m.shares, m.price);
                    audit.tally(r);
                    if (r == book::ApplyResult::Applied)
                    {
                        audit.shares_added += m.shares;
                    }
                    break;
                }
                case 'E':
                case 'C':
                {
                    const itch::OrderExecuted m = itch::decode_order_executed(mv.data);
                    if (!books.known(locate))
                    {
                        ++unknown_locate;
                        break;
                    }
                    // Measure the quantity that actually left, not what the message
                    // asked for. They differ when a reduction is clamped, and using the
                    // message's number would break conservation for the right reason in
                    // the wrong place.
                    book::OrderBook&    b      = books.book(locate);
                    const std::uint32_t before = b.shares_of(m.order_ref);
                    audit.tally(b.reduce(m.order_ref, m.executed_shares));
                    audit.shares_executed += before - b.shares_of(m.order_ref);
                    break;
                }
                case 'X':
                {
                    const itch::OrderCancel m = itch::decode_order_cancel(mv.data);
                    if (!books.known(locate))
                    {
                        ++unknown_locate;
                        break;
                    }
                    book::OrderBook&    b      = books.book(locate);
                    const std::uint32_t before = b.shares_of(m.order_ref);
                    audit.tally(b.reduce(m.order_ref, m.cancelled_shares));
                    audit.shares_cancelled += before - b.shares_of(m.order_ref);
                    break;
                }
                case 'D':
                {
                    const itch::OrderDelete m = itch::decode_order_delete(mv.data);
                    if (!books.known(locate))
                    {
                        ++unknown_locate;
                        break;
                    }
                    book::OrderBook& b    = books.book(locate);
                    audit.shares_deleted += b.shares_of(m.order_ref);
                    audit.tally(b.remove(m.order_ref));
                    break;
                }
                case 'U':
                {
                    const itch::OrderReplace m = itch::decode_order_replace(mv.data);
                    if (!books.known(locate))
                    {
                        ++unknown_locate;
                        break;
                    }
                    book::OrderBook& b = books.book(locate);
                    // A replace is a delete and an add, so it contributes to both
                    // sides of the conservation sum, not to a category of its own.
                    const std::uint32_t killed = b.shares_of(m.original_order_ref);
                    const auto          r =
                        b.replace(m.original_order_ref, m.new_order_ref, m.shares, m.price);
                    audit.tally(r);
                    if (r == book::ApplyResult::Applied)
                    {
                        audit.shares_deleted += killed;
                        audit.shares_added   += m.shares;
                    }
                    break;
                }

                // 'P' and 'Q' report to the tape and do not touch the displayed book.
                // A non cross trade executes against hidden liquidity that was never
                // in the book, so applying it would double count the visible orders.
                default:
                    break;
            }

            // Only the book this message touched. Sweeping all 9,000 books per message
            // would be 20 billion checks over the file, which is how a check meant to
            // be constant time turns the run quadratic. A message can only break the
            // book it was applied to, so checking that one is both cheap and complete.
            if (books.known(locate) && !books.book(locate).check_invariants_fast())
            {
                invariants_ok = false;
                std::fprintf(stderr, "fast invariant broke at message %zu\n", messages);
                break;
            }

#ifndef NDEBUG
            if (messages % kInvariantInterval == 0 && !books.check_invariants())
            {
                invariants_ok = false;
                std::fprintf(stderr, "invariants broke by message %zu\n", messages);
                break;
            }
#endif
        }

        if (!books.check_invariants())
        {
            invariants_ok = false;
        }

        std::printf("file            %s\n", argv[1]);
        std::printf("bytes           %zu\n", file.size());
        std::printf("consumed        %zu\n", framer.offset());
        std::printf("leftover        %zu\n", file.size() - framer.offset());
        std::printf("framing exact   %s\n", framer.exhausted() ? "yes" : "no");
        std::printf("messages        %zu\n", messages);
        std::printf("unknown types   %zu\n", unknown_type);
        std::printf("bad lengths     %zu\n", length_mismatch);
        std::printf("unknown locates %zu\n", unknown_locate);
        std::printf("symbols         %zu\n", books.size());
        std::printf("invariants      %s\n", invariants_ok ? "hold" : "BROKEN");

        std::printf("\nmessage application\n");
        std::printf("  applied        %zu\n", audit.applied);
        std::printf("  unknown ref    %zu\n", audit.unknown_ref);
        std::printf("  duplicate ref  %zu\n", audit.duplicate_ref);
        std::printf("  clamped reduce %zu\n", audit.clamped_reduce);

        // Conservation. Every share that entered the book through an add must have
        // left through an execute, a cancel or a delete, or still be resting. This ties
        // the messages consumed to the state produced across the whole file, and no
        // share field can be misdecoded without breaking it.
        const std::uint64_t resting   = books.resting_shares();
        const std::uint64_t expected  = audit.expected_resting();
        const bool          conserved = (resting == expected);

        std::printf("\nshare conservation\n");
        std::printf("  added          %llu\n", static_cast<unsigned long long>(audit.shares_added));
        std::printf("  executed       %llu\n",
                    static_cast<unsigned long long>(audit.shares_executed));
        std::printf("  cancelled      %llu\n",
                    static_cast<unsigned long long>(audit.shares_cancelled));
        std::printf("  deleted        %llu\n",
                    static_cast<unsigned long long>(audit.shares_deleted));
        std::printf("  expected rest  %llu\n", static_cast<unsigned long long>(expected));
        std::printf("  actual resting %llu\n", static_cast<unsigned long long>(resting));
        std::printf("  conserved      %s\n", conserved ? "yes" : "NO");

        std::printf("\nmessages by type\n");
        for (const auto& [type, n] : counts)
        {
            std::printf("  %c %10zu\n", type, n);
        }

        std::printf("\ntop of book, first ten symbols with a two sided market\n");
        std::size_t shown = 0;
        for (std::size_t locate = 1; locate <= 0xFFFF && shown < 10; ++locate)
        {
            const auto l16 = static_cast<std::uint16_t>(locate);
            if (!books.known(l16))
            {
                continue;
            }
            const auto bid = books.book(l16).best_bid();
            const auto ask = books.book(l16).best_ask();
            if (!bid || !ask)
            {
                continue;
            }
            std::printf("  %.8s  %8u x %-8llu | %8u x %-8llu\n",
                        books.symbol(l16).data(),
                        bid->price,
                        static_cast<unsigned long long>(bid->shares),
                        ask->price,
                        static_cast<unsigned long long>(ask->shares));
            ++shown;
        }

        return (invariants_ok && conserved && unknown_type == 0 && length_mismatch == 0 &&
                unknown_locate == 0 && audit.unknown_ref == 0 && audit.duplicate_ref == 0 &&
                audit.clamped_reduce == 0)
                   ? 0
                   : 1;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 2;
    }
}
