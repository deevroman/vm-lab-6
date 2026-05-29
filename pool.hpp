#ifndef POOL_HPP
#define POOL_HPP

#include <cassert>
#include <cstdlib>
#include <iomanip>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

inline void overflow_signal_handler(int, siginfo_t*, void*)
{
    constexpr char msg[] = "Pool overflow\n";
    write(STDERR_FILENO, msg, sizeof(msg) - 1);
    _exit(EXIT_FAILURE);
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
        assert(segv_rc == 0 && "Cannot install SIGSEGV handler");
        assert(bus_rc == 0 && "Cannot install SIGBUS handler");
    }
};

inline SigsegvInstaller sigsegv_installer;

inline size_t align_to_page_size(const size_t x, size_t page_size) { return (x + page_size - 1) & ~(page_size - 1); }

template <class T>
class Pool
{
    char* start_addr;
    char* last_addr;
    size_t pool_size;

public:
    Pool(size_t capacity)
    {
#ifdef BAD_POOL
        capacity /= 1.5;
#endif
        size_t page_size = sysconf(_SC_PAGESIZE);
        size_t guard_size = align_to_page_size(sizeof(T), page_size);
        size_t real_capacity = align_to_page_size(capacity, page_size);
        pool_size = real_capacity + guard_size;

        start_addr =
            static_cast<char*>(mmap(nullptr, pool_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0));
        if (start_addr == MAP_FAILED)
        {
            perror("mmap failed");
            exit(EXIT_FAILURE);
        }
        if (mprotect(start_addr, guard_size, PROT_NONE) == -1)
        {
            perror("mprotect failed");
            exit(EXIT_FAILURE);
        }

        last_addr = start_addr + pool_size;
    }
    ~Pool() { munmap(start_addr, pool_size); }

    void* allocate(size_t size) { return last_addr -= size; }
};
#endif // POOL_HPP
