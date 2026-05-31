#ifndef POOL_HPP
#define POOL_HPP

#include <cstdlib>
#include <iomanip>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

#define runtime_assert(x, msg)                                                                                         \
    if (!(x))                                                                                                          \
    {                                                                                                                  \
        fprintf(stderr, msg);                                                                                          \
        exit(EXIT_FAILURE);                                                                                            \
    }

constexpr int kMaxPools = 16;

struct PoolEntry
{
    std::atomic<char*> guard_start{nullptr};
    std::atomic<char*> guard_end{nullptr};
};

class PoolRegistry
{
    PoolEntry entries[kMaxPools];

public:
    int register_pool(char* guard_start, char* guard_end)
    {
        for (int i = 0; i < kMaxPools; ++i)
        {
            char* expected = nullptr;
            if (entries[i].guard_end.compare_exchange_strong(expected, guard_end, std::memory_order_acq_rel,
                                                             std::memory_order_relaxed))
            {
                entries[i].guard_start.store(guard_start, std::memory_order_release);
                return i;
            }
        }
        runtime_assert(false, "Too many pools registered");
    }

    void unregister_pool(int id)
    {
        entries[id].guard_start.store(nullptr, std::memory_order_release);
        entries[id].guard_end.store(nullptr, std::memory_order_release);
    }

    int find_pool_id(char* fault_addr) const
    {
        for (int i = 0; i < kMaxPools; ++i)
        {
            char* start = entries[i].guard_start.load(std::memory_order_acquire);
            if (start == nullptr)
            {
                continue;
            }

            char* end = entries[i].guard_end.load(std::memory_order_acquire);
            if (fault_addr >= start && fault_addr < end)
            {
                return i;
            }
        }
        return -1;
    }
};

inline PoolRegistry registry;
inline struct sigaction prev_sigsegv_action = {};
inline struct sigaction prev_sigbus_action = {};

inline void forward_signal(int sig, siginfo_t* info, void* ucontext)
{
    struct sigaction& prev_action = sig == SIGBUS ? prev_sigbus_action : prev_sigsegv_action;

    if (prev_action.sa_handler == SIG_DFL)
    {
        sigaction(sig, &prev_action, nullptr);
        raise(sig);
        return;
    }

    if (prev_action.sa_handler == SIG_IGN)
    {
        return;
    }

    if ((prev_action.sa_flags & SA_SIGINFO) != 0)
    {
        prev_action.sa_sigaction(sig, info, ucontext);
    }
    else
    {
        prev_action.sa_handler(sig);
    }
}

inline void overflow_signal_handler(int sig, siginfo_t* info, void* ucontext)
{
    char* fault_addr = static_cast<char*>(info->si_addr);
    int pool_id = registry.find_pool_id(fault_addr);

    if (pool_id != -1)
    {
        char msg[64];
        char* out = msg;
        constexpr char prefix[] = "Pool overflow detected in pool ";
        for (std::size_t i = 0; i < sizeof(prefix) - 1; ++i)
        {
            *out++ = prefix[i];
        }

        char digits[10];
        unsigned count = 0;
        unsigned value = static_cast<unsigned>(pool_id);
        do
        {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        }
        while (value != 0);

        while (count > 0)
        {
            *out++ = digits[--count];
        }

        *out++ = '\n';
        write(STDERR_FILENO, msg, out - msg);
        _exit(EXIT_FAILURE);
    }

    forward_signal(sig, info, ucontext);
}


struct SigsegvInstaller
{
    SigsegvInstaller()
    {
        struct sigaction action{};
        action.sa_sigaction = overflow_signal_handler;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_SIGINFO;

        int segv_rc = sigaction(SIGSEGV, &action, nullptr);
        int bus_rc = sigaction(SIGBUS, &action, nullptr);
        runtime_assert(segv_rc == 0, "Cannot install SIGSEGV handler");
        runtime_assert(bus_rc == 0, "Cannot install SIGBUS handler");
    }
};

inline SigsegvInstaller sigsegv_installer;

const size_t page_size = sysconf(_SC_PAGESIZE);
inline size_t align_to_page_size(const size_t x) { return (x + page_size - 1) & ~(page_size - 1); }

template <class T>
class Pool
{
protected:
    const size_t pool_size;
    char* const start_addr;
    char* last_addr;
    const int pool_id;

    static char* alloc_pool_memory(const size_t pool_size)
    {
        const auto addr =
            static_cast<char*>(mmap(nullptr, pool_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (addr == MAP_FAILED)
        {
            perror("mmap failed");
            exit(EXIT_FAILURE);
        }
        if (mprotect(addr, align_to_page_size(sizeof(T)), PROT_NONE) == -1)
        {
            perror("mprotect failed");
            exit(EXIT_FAILURE);
        }
        return addr;
    }

public:
    Pool(size_t capacity) :
        pool_size(align_to_page_size(capacity) + align_to_page_size(sizeof(T))),
        start_addr(alloc_pool_memory(pool_size)), last_addr(start_addr + pool_size),
        pool_id(registry.register_pool(start_addr, last_addr))
    {
    }
    ~Pool()
    {
        registry.unregister_pool(pool_id);
        munmap(start_addr, pool_size);
    }

    virtual void* allocate(size_t size) { return last_addr -= size; }
};
#endif // POOL_HPP
