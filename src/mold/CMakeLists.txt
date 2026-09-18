# MoldUDP64 framing and the UDP sender. rate_limiter.h and gap_injector.h are
# header only: both are a few lines and both are called once per packet by a
# program that is test equipment, so there is nothing to gain from a separate
# translation unit.
add_library(nts_mold STATIC
    mold.cc
    packer.cc
    udp_sender.cc
    decoder.cc)

# decoder.h takes a receive::PacketView, a header only struct under src/receive,
# reached through the common include root. No link to nts_receive is needed and
# none is wanted: the decoder does not know which receiver produced the packet.
target_link_libraries(nts_mold PUBLIC nts_common nts_itch)
