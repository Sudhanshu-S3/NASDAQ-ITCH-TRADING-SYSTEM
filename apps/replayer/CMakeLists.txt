add_executable(replayer main.cc)
target_link_libraries(replayer PRIVATE nts_common nts_itch nts_mold)

# Unprivileged capture to a pcap file, for the validation step. Test equipment.
add_executable(mold_capture capture.cc)
target_link_libraries(mold_capture PRIVATE nts_common nts_mold)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(replayer PRIVATE
        -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -pedantic)
    target_compile_options(mold_capture PRIVATE
        -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -pedantic)
endif()
