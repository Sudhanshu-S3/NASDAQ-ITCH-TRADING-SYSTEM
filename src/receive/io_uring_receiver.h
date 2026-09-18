#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <linux/io_uring.h>

#include "receive/ireceiver.h"

namespace nts::receive
{
    struct IoUringConfig
    {
        unsigned entries        = 256;    ///< submission queue depth; completions get 4x
        bool     multishot      = false;  ///< one armed RECV completing many times
        bool     sqpoll         = false;  ///< kernel thread polls the SQ; no submit syscall
        unsigned sqpoll_idle_ms = 100;
        int      sqpoll_cpu     = -1;     ///< pin the SQPOLL thread there; -1 lets it float
        unsigned buffer_count   = 1024;   ///< provided buffers for the multishot ring
        unsigned buffer_size    = 2048;
        bool     spin           = false;  ///< poll the CQ tail instead of sleeping in enter
        unsigned depth          = 1;      ///< plain mode: RECV requests in flight; see below
    };

    class IoUringReceiver final : public IReceiver
    {
    public:
        IoUringReceiver(std::string_view    bind_host,
                        std::uint16_t       port,
                        const IoUringConfig& cfg,
                        bool                multicast       = false,
                        std::string_view    multicast_group = "");
        ~IoUringReceiver() override;

        IoUringReceiver(const IoUringReceiver&)            = delete;
        IoUringReceiver& operator=(const IoUringReceiver&) = delete;

        void run(const Sink& sink) override;
        void stop() noexcept override;

        [[nodiscard]] std::uint64_t packets_received() const noexcept override
        {
            return packets_;
        }

        [[nodiscard]] std::uint64_t syscalls() const noexcept override
        {
            return enters_;
        }

        [[nodiscard]] const char* name() const noexcept override;

        [[nodiscard]] std::uint64_t enobufs() const noexcept
        {
            return enobufs_;
        }

        [[nodiscard]] std::uint64_t submits() const noexcept
        {
            return enters_;
        }

        [[nodiscard]] std::uint64_t completions_per_enter_max() const noexcept
        {
            return max_batch_;
        }

        [[nodiscard]] std::uint16_t port() const noexcept
        {
            return port_;
        }

        [[nodiscard]] const IoUringConfig& config() const noexcept
        {
            return cfg_;
        }

    private:
        void setup_ring();
        void setup_buffer_ring();
        void teardown() noexcept;

        // Ring plumbing. All index arithmetic is masked; head and tail are shared with
        // the kernel and read and written with acquire and release semantics.
        io_uring_sqe* get_sqe() noexcept;
        void          push_recv(unsigned slot) noexcept;
        void          push_multishot() noexcept;
        int           enter(unsigned min_complete,
                            unsigned flags) noexcept;
        bool          wait_for_completion() noexcept;
        void          return_buffer(unsigned bid) noexcept;

        IoUringConfig cfg_;
        int           sock_    = -1;
        int           ring_fd_ = -1;
        std::uint16_t port_    = 0;

        // Submission ring
        void*          sq_mmap_      = nullptr;
        std::size_t    sq_mmap_size_ = 0;
        unsigned*      sq_head_      = nullptr;
        unsigned*      sq_tail_      = nullptr;
        unsigned*      sq_mask_      = nullptr;
        unsigned*      sq_flags_     = nullptr;
        unsigned*      sq_array_     = nullptr;
        io_uring_sqe*  sqes_         = nullptr;
        std::size_t    sqes_size_    = 0;
        unsigned       sq_local_tail_ = 0;
        unsigned       sq_submitted_  = 0;  ///< tail the kernel has been told about

        // Completion ring
        void*          cq_mmap_      = nullptr;
        std::size_t    cq_mmap_size_ = 0;
        unsigned*      cq_head_      = nullptr;
        unsigned*      cq_tail_      = nullptr;
        unsigned*      cq_mask_      = nullptr;
        io_uring_cqe*  cqes_         = nullptr;

        // Buffers: plain slots or a provided buffer ring.
        std::vector<std::byte> buffers_;
        io_uring_buf_ring*     buf_ring_      = nullptr;
        std::size_t            buf_ring_size_ = 0;
        unsigned               buf_ring_mask_ = 0;
        unsigned               buf_ring_tail_ = 0;

        std::atomic<bool> stop_{false};
        std::uint64_t     packets_   = 0;
        std::uint64_t     enters_    = 0;
        std::uint64_t     enobufs_   = 0;
        std::uint64_t     max_batch_ = 0;
    };
}  // namespace nts::receive
