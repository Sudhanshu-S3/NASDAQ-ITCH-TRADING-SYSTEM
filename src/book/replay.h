#pragma once

#include <cstddef>
#include <cstdint>

#include "book/book_set.h"
#include "itch/framing.h"

namespace nts::book
{
    /**
     * Counters the replay keeps beside the book.
     *
     * Every counter is an integer increment, so tallying them costs nothing
     * measurable and nothing here prints or allocates. They exist so that a timed
     * run can still prove it applied the stream it was given: a decode bug that
     * shifts every order reference produces an empty book and a flood of
     * unknown_ref, rather than a faster run that means nothing.
     */
    struct ReplayStats
    {
        std::uint64_t messages        = 0;
        std::uint64_t unknown_type    = 0;
        std::uint64_t length_mismatch = 0;
        std::uint64_t unknown_locate  = 0;
        std::uint64_t applied         = 0;
        std::uint64_t unknown_ref     = 0;
        std::uint64_t duplicate_ref   = 0;
        std::uint64_t clamped_reduce  = 0;

        /**
         * unknown_type is deliberately not a dirtiness condition. A type byte this
         * build does not name is still legal on the wire and is skipped safely by
         * its length prefix, which is what message_type.h says. The full day file
         * carries about a million of them (I, J, K, V), and none moves a book.
         *
         * length_mismatch is a different matter and does count: it means the length
         * table disagrees with the file for a type this build claims to know, and
         * that desynchronises everything after it.
         */
        [[nodiscard]] bool clean() const noexcept
        {
            return length_mismatch == 0 && unknown_locate == 0 && unknown_ref == 0 &&
                   duplicate_ref == 0 && clamped_reduce == 0;
        }
    };

    /**
     * Decodes one framed message and folds it into the book it names.
     *
     * This is the dispatch, and it is deliberately the only one: the offline walk
     * below and the wire path in bench/bench_wire.cc both call it, so a book built
     * from a file and a book built from a socket cannot drift apart through two
     * copies of a switch statement.
     *
     * Nothing here prints, logs, allocates or throws.
     */
    void apply_message(BookSet&                 books,
                       const itch::MessageView& msg,
                       ReplayStats&             stats);

    /**
     * Walks a whole buffer: frame, decode, apply to the book it names.
     *
     * This is the function the benchmark times, so it is the definition of "the
     * pipeline" for every number this project reports about the offline path. It
     * does the minimum: no conservation tracking, no invariant checks, no printing.
     *
     * apps/feed_handler keeps its own loop rather than calling this, because the
     * conservation audit needs a shares_of() lookup before and after every reduce,
     * which is real work a benchmark must not pay for.
     */
    [[nodiscard]] ReplayStats replay(BookSet&         books,
                                     const std::byte* data,
                                     std::size_t      size);
}  // namespace nts::book
