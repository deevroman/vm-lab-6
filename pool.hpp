#ifndef POOL_HPP
#define POOL_HPP

#include <cstdlib>
#include <iomanip>
#include <sys/mman.h>
#include <unistd.h>

template <class T>
class Pool
{
    char* start_addr;
    char* last_addr;
    size_t pool_size;

public:
    Pool(size_t capacity)
    {
        size_t page_size = sysconf(_SC_PAGESIZE);
        size_t guard_size = (sizeof(T) + page_size - 1) / page_size * page_size;
        size_t real_capacity = (capacity + page_size - 1) / page_size * page_size;
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
