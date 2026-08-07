# NASDAQ ITCH 5.0 Kernel-Bypass Trading System

A C++20 tick-to-trade trading system built against real NASDAQ TotalView-ITCH 5.0 market data.

A recorded exchange session is replayed onto the wire as MoldUDP64 over UDP, received through
progressively lower-latency paths, decoded, and folded into a per-symbol limit order book. The
loop then closes on the send side: a strategy signal passes an inline pre-trade risk gate and
leaves as an OUCH 4.2 order over SoupBinTCP, against an exchange simulator that matches it on
real price-time priority.

**Status: in progress** building at my own pace.

## Layout

```
src/common/      shared primitives: big-endian field reads, timing, ring buffer
src/itch/        ITCH 5.0 message decoding
src/mold/        MoldUDP64 framing, sequence numbers, gap detection
src/book/        the order book engine: symbol index, order index, price levels
src/receive/     the receive-path seam: recvfrom, io_uring, AF_XDP
src/strategy/    the top-of-book-change callback
src/risk/        pre-trade risk gate and live positions
src/ouch/        OUCH 4.2 order entry encode and decode
src/soup/        SoupBinTCP session layer

apps/replayer/       file to MoldUDP64 on the wire
apps/feed_handler/   receive, book, strategy, risk, gateway
apps/exchange_sim/   market data publisher plus matching engine plus Soup server

tests/    correctness and invariant checks
bench/    latency and throughput measurement
cmake/    build modules
scripts/  helper scripts
data/     ITCH sample files, not committed
```

Headers live beside their sources inside each module, with `src/` as the include root, so
includes read `<itch/decode.hpp>` rather than a relative path. There is no separate `include/`
tree, because nothing links against this repository from outside.

Directories beyond the current phase exist as placeholders so the architecture is visible.
They fill in as the phases land.

## Why this exists

Building for Fun ;-)
