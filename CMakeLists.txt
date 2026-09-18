cmake_minimum_required(VERSION 3.20)
project(ITCH_Trading_System CXX)

enable_testing()

# Phase 1: read the file, frame it, decode it, build the book.
add_subdirectory(src/common)
add_subdirectory(src/itch)
add_subdirectory(src/book)
add_subdirectory(apps/feed_handler)

# The offline benchmark: file to book. This is the phase 2 baseline, and it is
# only half the flow. The socket to book number needs the phase 4 receiver.
add_subdirectory(bench)

# Phase 3: the replayer. It never touches the book, so it builds and runs on its
# own; see Notes/Plan/phase3/plan.md, "A note on where this sits".
add_subdirectory(src/mold)
add_subdirectory(apps/replayer)

# Phase 4: the receivers. recvfrom is the untuned control group; io_uring is
# built when the kernel UAPI header is present.
add_subdirectory(src/receive)

# Added back one phase at a time as each is done:
#   phase 2  src/book/price_levels.cc and the flat structures in src/common
#   phase 6  src/strategy, src/book/book_engine.cc, bench/tick_to_signal.cc
#
# add_subdirectory(src/strategy)

add_subdirectory(tests)
