add_executable(feed_handler main.cc)

target_link_libraries(feed_handler PRIVATE nts_common nts_itch nts_book)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(feed_handler PRIVATE
        -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -pedantic)
endif()
