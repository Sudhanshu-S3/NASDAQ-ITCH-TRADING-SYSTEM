#include "receive/io_uring_receiver.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "common/timing.h"
#include "receive/socket_setup.h"

namespace nts::receive
{
    namespace
    {
        [[noreturn]] void fail(const char* what,
                               int         err)
        {
            throw std::runtime_error(std::string("io_uring ") + what + ": " + std::strerror(err));
        }

        int sys_io_uring_setup(unsigned         entries,
                               io_uring_params* p) noexcept
        {
            return static_cast<int>(::syscall(__NR_io_uring_setup, entries, p));
        }

        int sys_io_uring_enter(int      fd,
                               unsigned to_submit,
                               unsigned min_complete,
                               unsigned flags) noexcept
        {
            return static_cast<int>(
                ::syscall(__NR_io_uring_enter, fd, to_submit, min_complete, flags, nullptr, 0));
        }

        int sys_io_uring_register(int      fd,
                                  unsigned opcode,
                                  void*    arg,
                                  unsigned nr_args) noexcept
        {
            return static_cast<int>(::syscall(__NR_io_uring_register, fd, opcode, arg, nr_args));
        }

        unsigned load_acquire(const unsigned* p) noexcept
        {
            return __atomic_load_n(p, __ATOMIC_ACQUIRE);
        }

        void store_release(unsigned* p,
                           unsigned  v) noexcept
        {
            __atomic_store_n(p, v, __ATOMIC_RELEASE);
        }
    }  // namespace

    IoUringReceiver::IoUringReceiver(std::string_view     bind_host,
                                     std::uint16_t        port,
                                     const IoUringConfig& cfg,
                                     bool                 multicast,
                                     std::string_view     multicast_group)
        : cfg_(cfg)
    {
        // Exactly the same socket as the baseline, so the socket is not a variable.
        sock_ = open_udp_socket(bind_host, port, multicast, multicast_group, &port_);
        try
        {
            setup_ring();
            buffers_.resize(static_cast<std::size_t>(cfg_.buffer_count) * cfg_.buffer_size);
            if (cfg_.multishot)
            {
                setup_buffer_ring();
            }
        }
        catch (...)
        {
            teardown();
            throw;
        }
    }

    IoUringReceiver::~IoUringReceiver()
    {
        teardown();
    }

    void IoUringReceiver::teardown() noexcept
    {
        if (buf_ring_ != nullptr)
        {
            ::munmap(buf_ring_, buf_ring_size_);
            buf_ring_ = nullptr;
        }
        if (sqes_ != nullptr)
        {
            ::munmap(sqes_, sqes_size_);
            sqes_ = nullptr;
        }
        if (cq_mmap_ != nullptr && cq_mmap_ != sq_mmap_)
        {
            ::munmap(cq_mmap_, cq_mmap_size_);
        }
        cq_mmap_ = nullptr;
        if (sq_mmap_ != nullptr)
        {
            ::munmap(sq_mmap_, sq_mmap_size_);
            sq_mmap_ = nullptr;
        }
        if (ring_fd_ >= 0)
        {
            ::close(ring_fd_);
            ring_fd_ = -1;
        }
        if (sock_ >= 0)
        {
            ::close(sock_);
            sock_ = -1;
        }
    }

    void IoUringReceiver::setup_ring()
    {
        io_uring_params p{};
        p.flags = IORING_SETUP_CQSIZE;
        p.cq_entries = cfg_.entries * 4;  // completions outnumber submissions under multishot
        if (cfg_.sqpoll)
        {
            p.flags |= IORING_SETUP_SQPOLL;
            p.sq_thread_idle = cfg_.sqpoll_idle_ms;
            if (cfg_.sqpoll_cpu >= 0)
            {
                // Without this the kernel thread inherits our affinity mask, and a
                // pinned receiver would share its one core with the thread that is
                // supposed to be spinning on its behalf.
                p.flags |= IORING_SETUP_SQ_AFF;
                p.sq_thread_cpu = static_cast<std::uint32_t>(cfg_.sqpoll_cpu);
            }
        }
        ring_fd_ = sys_io_uring_setup(cfg_.entries, &p);
        if (ring_fd_ < 0)
        {
            fail("setup", errno);
        }

        // One mapping covers both rings when the kernel reports SINGLE_MMAP (every
        // kernel since 5.4). The offsets in params say where each field lives.
        const std::size_t sq_size = p.sq_off.array + p.sq_entries * sizeof(unsigned);
        const std::size_t cq_size = p.cq_off.cqes + p.cq_entries * sizeof(io_uring_cqe);
        const bool        single  = (p.features & IORING_FEAT_SINGLE_MMAP) != 0;
        sq_mmap_size_             = single ? (sq_size > cq_size ? sq_size : cq_size) : sq_size;

        sq_mmap_ = ::mmap(nullptr, sq_mmap_size_, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                          ring_fd_, IORING_OFF_SQ_RING);
        if (sq_mmap_ == MAP_FAILED)
        {
            sq_mmap_ = nullptr;
            fail("mmap sq ring", errno);
        }
        if (single)
        {
            cq_mmap_      = sq_mmap_;
            cq_mmap_size_ = sq_mmap_size_;
        }
        else
        {
            cq_mmap_size_ = cq_size;
            cq_mmap_      = ::mmap(nullptr, cq_mmap_size_, PROT_READ | PROT_WRITE,
                                   MAP_SHARED | MAP_POPULATE, ring_fd_, IORING_OFF_CQ_RING);
            if (cq_mmap_ == MAP_FAILED)
            {
                cq_mmap_ = nullptr;
                fail("mmap cq ring", errno);
            }
        }

        auto* sq = static_cast<unsigned char*>(sq_mmap_);
        sq_head_  = reinterpret_cast<unsigned*>(sq + p.sq_off.head);
        sq_tail_  = reinterpret_cast<unsigned*>(sq + p.sq_off.tail);
        sq_mask_  = reinterpret_cast<unsigned*>(sq + p.sq_off.ring_mask);
        sq_flags_ = reinterpret_cast<unsigned*>(sq + p.sq_off.flags);
        sq_array_ = reinterpret_cast<unsigned*>(sq + p.sq_off.array);

        auto* cq = static_cast<unsigned char*>(cq_mmap_);
        cq_head_ = reinterpret_cast<unsigned*>(cq + p.cq_off.head);
        cq_tail_ = reinterpret_cast<unsigned*>(cq + p.cq_off.tail);
        cq_mask_ = reinterpret_cast<unsigned*>(cq + p.cq_off.ring_mask);
        cqes_    = reinterpret_cast<io_uring_cqe*>(cq + p.cq_off.cqes);

        sqes_size_ = p.sq_entries * sizeof(io_uring_sqe);
        void* sqes = ::mmap(nullptr, sqes_size_, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                            ring_fd_, IORING_OFF_SQES);
        if (sqes == MAP_FAILED)
        {
            fail("mmap sqes", errno);
        }
        sqes_          = static_cast<io_uring_sqe*>(sqes);
        sq_local_tail_ = *sq_tail_;

        // The socket is the only fd every request names; registering it lets the
        // kernel skip the fd table lookup per request.
        int fds[1] = {sock_};
        if (sys_io_uring_register(ring_fd_, IORING_REGISTER_FILES, fds, 1) < 0)
        {
            fail("register files", errno);
        }
    }

    void IoUringReceiver::setup_buffer_ring()
    {
        // The buffer ring itself is a page aligned array of io_uring_buf, with the
        // tail living in the reserved field of the first entry (the union in the
        // UAPI header). ring_entries must be a power of two.
        unsigned entries = 1;
        while (entries < cfg_.buffer_count)
        {
            entries <<= 1;
        }
        cfg_.buffer_count = entries;
        buffers_.resize(static_cast<std::size_t>(entries) * cfg_.buffer_size);
        buf_ring_mask_ = entries - 1;
        buf_ring_size_ = entries * sizeof(io_uring_buf);
        buf_ring_size_ = (buf_ring_size_ + 4095) & ~std::size_t{4095};
        void* mem = ::mmap(nullptr, buf_ring_size_, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
        if (mem == MAP_FAILED)
        {
            fail("mmap buffer ring", errno);
        }
        buf_ring_ = static_cast<io_uring_buf_ring*>(mem);

        io_uring_buf_reg reg{};
        reg.ring_addr    = reinterpret_cast<std::uint64_t>(buf_ring_);
        reg.ring_entries = entries;
        reg.bgid         = 0;
        if (sys_io_uring_register(ring_fd_, IORING_REGISTER_PBUF_RING, &reg, 1) < 0)
        {
            fail("register pbuf ring", errno);
        }

        buf_ring_tail_ = 0;
        for (unsigned i = 0; i < entries; ++i)
        {
            return_buffer(i);
        }
    }

    void IoUringReceiver::return_buffer(unsigned bid) noexcept
    {
        io_uring_buf& b = buf_ring_->bufs[buf_ring_tail_ & buf_ring_mask_];
        b.addr = reinterpret_cast<std::uint64_t>(buffers_.data() +
                                                 static_cast<std::size_t>(bid) * cfg_.buffer_size);
        b.len  = cfg_.buffer_size;
        b.bid  = static_cast<std::uint16_t>(bid);
        ++buf_ring_tail_;
        // The tail is shared with the kernel: release so the entry is visible first.
        __atomic_store_n(&buf_ring_->tail, static_cast<std::uint16_t>(buf_ring_tail_),
                         __ATOMIC_RELEASE);
    }

    io_uring_sqe* IoUringReceiver::get_sqe() noexcept
    {
        const unsigned head = load_acquire(sq_head_);
        if (sq_local_tail_ - head >= *sq_mask_ + 1)
        {
            return nullptr;  // full
        }
        const unsigned idx = sq_local_tail_ & *sq_mask_;
        io_uring_sqe*  sqe = &sqes_[idx];
        std::memset(sqe, 0, sizeof *sqe);
        sq_array_[idx] = idx;
        ++sq_local_tail_;
        return sqe;
    }

    void IoUringReceiver::push_recv(unsigned slot) noexcept
    {
        io_uring_sqe* sqe = get_sqe();
        if (sqe == nullptr)
        {
            return;
        }
        sqe->opcode    = IORING_OP_RECV;
        sqe->fd        = 0;  // index into the registered file table
        sqe->flags     = IOSQE_FIXED_FILE;
        sqe->addr      = reinterpret_cast<std::uint64_t>(
            buffers_.data() + static_cast<std::size_t>(slot) * cfg_.buffer_size);
        sqe->len       = cfg_.buffer_size;
        sqe->msg_flags = MSG_TRUNC;
        sqe->user_data = slot;
    }

    void IoUringReceiver::push_multishot() noexcept
    {
        io_uring_sqe* sqe = get_sqe();
        if (sqe == nullptr)
        {
            return;
        }
        sqe->opcode    = IORING_OP_RECV;
        sqe->fd        = 0;
        sqe->flags     = IOSQE_FIXED_FILE | IOSQE_BUFFER_SELECT;
        sqe->ioprio    = IORING_RECV_MULTISHOT;
        sqe->buf_group = 0;
        sqe->msg_flags = MSG_TRUNC;
        sqe->user_data = 0xFFFFFFFFu;
    }

    int IoUringReceiver::enter(unsigned min_complete,
                               unsigned flags) noexcept
    {
        // Publish the tail first: everything written to the SQEs must be visible to
        // the kernel before it sees the new tail. Whatever was pushed since the last
        // call is submitted by this one, so a refill and a wait share one syscall.
        store_release(sq_tail_, sq_local_tail_);
        unsigned to_submit = sq_local_tail_ - sq_submitted_;
        sq_submitted_      = sq_local_tail_;
        if (cfg_.sqpoll)
        {
            // The kernel thread picks up the tail itself. A call is needed only to
            // wake it after it went idle, or to wait for completions.
            if ((load_acquire(sq_flags_) & IORING_SQ_NEED_WAKEUP) != 0)
            {
                flags |= IORING_ENTER_SQ_WAKEUP;
            }
            else if (min_complete == 0)
            {
                return 0;  // nothing to wait for: no syscall
            }
            to_submit = 0;
        }
        else if (to_submit == 0 && min_complete == 0)
        {
            return 0;
        }
        ++enters_;
        for (;;)
        {
            const int r = sys_io_uring_enter(ring_fd_, to_submit, min_complete, flags);
            if (r < 0 && errno == EINTR)
            {
                continue;
            }
            return r;
        }
    }

    bool IoUringReceiver::wait_for_completion() noexcept
    {
        if (cfg_.spin)
        {
            // Poll the shared tail. With SQPOLL this loop makes no syscall at all;
            // without it, pending submissions still need one enter, made here without
            // waiting.
            (void) enter(0, 0);
            while (load_acquire(cq_tail_) == *cq_head_)
            {
                if (stop_.load(std::memory_order_relaxed))
                {
                    return false;
                }
            }
            return true;
        }
        const int r = enter(1, IORING_ENTER_GETEVENTS);
        return r >= 0 || errno == EBUSY || errno == ETIME;
    }

    void IoUringReceiver::stop() noexcept
    {
        stop_.store(true, std::memory_order_relaxed);
    }

    const char* IoUringReceiver::name() const noexcept
    {
        if (cfg_.multishot && cfg_.sqpoll)
        {
            return cfg_.spin ? "io_uring+multishot+sqpoll+spin" : "io_uring+multishot+sqpoll";
        }
        if (cfg_.multishot)
        {
            return cfg_.spin ? "io_uring+multishot+spin" : "io_uring+multishot";
        }
        if (cfg_.sqpoll)
        {
            return cfg_.spin ? "io_uring+sqpoll+spin" : "io_uring+sqpoll";
        }
        return cfg_.spin ? "io_uring+spin" : "io_uring";
    }

    void IoUringReceiver::run(const Sink& sink)
    {
        PacketView view;

        // Arm. Plain: depth RECVs, one per slot. Multishot: one RECV over the ring.
        // Nothing is submitted here; the first wait submits it.
        if (cfg_.multishot)
        {
            push_multishot();
        }
        else
        {
            unsigned slots = cfg_.depth < cfg_.entries ? cfg_.depth : cfg_.entries;
            if (slots > cfg_.buffer_count)
            {
                slots = cfg_.buffer_count;
            }
            for (unsigned s = 0; s < slots; ++s)
            {
                push_recv(s);
            }
        }

        while (!stop_.load(std::memory_order_relaxed))
        {
            if (!wait_for_completion())
            {
                if (stop_.load(std::memory_order_relaxed))
                {
                    break;
                }
                continue;
            }

            // Drain every completion that is there. Each gets its own stamp at the
            // moment it is observed: later ones in the batch are later, honestly.
            unsigned       head     = *cq_head_;
            const unsigned tail     = load_acquire(cq_tail_);
            std::uint64_t  batch    = 0;
            bool           rearm_ms = false;
            while (head != tail)
            {
                const io_uring_cqe& cqe = cqes_[head & *cq_mask_];

                // t0. First statement after the completion is seen.
                const std::uint64_t rx_tsc = rdtscp_now();

                ++batch;
                const int res = cqe.res;
                if (cfg_.multishot)
                {
                    const bool has_buf = (cqe.flags & IORING_CQE_F_BUFFER) != 0;
                    const unsigned bid = cqe.flags >> IORING_CQE_BUFFER_SHIFT;
                    if (res == -ENOBUFS)
                    {
                        ++enobufs_;
                    }
                    else if (res >= 0 && has_buf)
                    {
                        if (static_cast<unsigned>(res) <= cfg_.buffer_size)
                        {
                            ++packets_;
                            view.data   = buffers_.data() + static_cast<std::size_t>(bid) * cfg_.buffer_size;
                            view.len    = static_cast<std::size_t>(res);
                            view.rx_tsc = rx_tsc;
                            sink(view);
                        }
                        return_buffer(bid);
                    }
                    if ((cqe.flags & IORING_CQE_F_MORE) == 0)
                    {
                        rearm_ms = true;  // the kernel disarmed it: re-issue the request
                    }
                }
                else
                {
                    const auto slot = static_cast<unsigned>(cqe.user_data);
                    if (res >= 0 && static_cast<unsigned>(res) <= cfg_.buffer_size)
                    {
                        ++packets_;
                        view.data   = buffers_.data() + static_cast<std::size_t>(slot) * cfg_.buffer_size;
                        view.len    = static_cast<std::size_t>(res);
                        view.rx_tsc = rx_tsc;
                        sink(view);
                    }
                    push_recv(slot);  // the slot is free again the moment the sink returns
                }
                ++head;
                // Mark the entry consumed as we go, or the kernel sees a full ring.
                store_release(cq_head_, head);
            }
            if (batch > max_batch_)
            {
                max_batch_ = batch;
            }
            if (rearm_ms)
            {
                push_multishot();
            }
            // Refills pushed above ride along with the next wait's enter call.
        }
    }
}  // namespace nts::receive
