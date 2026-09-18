add_library(nts_itch STATIC
    framing.cc
    decoder.cc)

# PUBLIC: consumers of nts_itch include itch/messages.h, which needs the same
# include root, and the byte readers come through this edge too.
target_link_libraries(nts_itch PUBLIC nts_common)
