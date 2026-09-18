# The baseline receiver: one blocking recvfrom per datagram, deliberately
# untuned. SO_RCVBUF is left at the kernel default and nothing is batched.
# This is the control group every later receiver is measured against, so
# making it faster would destroy the comparison it exists to provide.
#
# The io_uring and AF_XDP receivers are parked in Notes/parked/receive/ and
# come back in phase 5, behind the same IReceiver seam.
#
# This target must not link nts_book or nts_itch: a receiver knows nothing
# about ITCH, and the compiler is the cheapest place to enforce that.
add_library(nts_receive STATIC
    socket_setup.cc
    recvfrom_receiver.cc)

target_link_libraries(nts_receive PUBLIC nts_common)
