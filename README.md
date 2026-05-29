Запуск с дефолтным аллокатором:

```
g++ test.cpp pool.hpp && ./a.out
Time used: 262787 usec
Memory used: 320684032 bytes
Overhead: 50.1%
```

Запуск с пулом:
```
g++ test_pool.cpp pool.hpp && ./a.out
Time used: 51238 usec
Memory used: 160190464 bytes
Overhead:  0.1%
```

Запуск с переполнением пула:
```
g++ --std=c++20 -DBAD_POOL test_pool.cpp pool.hpp && ./a.out
Pool overflow
```