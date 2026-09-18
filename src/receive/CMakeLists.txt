add_library(nts_receive STATIC
    socket_setup.cc
    recvfrom_receiver.cc
    io_uring_receiver.cc)

target_link_libraries(nts_receive PUBLIC nts_common)
