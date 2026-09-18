
add_library(nts_book STATIC
    order_book.cc
    price_levels.cc
    book_set.cc
    replay.cc)

# PUBLIC: book_set.h includes itch/messages.h, so consumers need nts_itch too.
target_link_libraries(nts_book PUBLIC nts_common nts_itch)
