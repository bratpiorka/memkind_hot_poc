// SPDX-License-Identifier: BSD-3-Clause
/* Copyright (C) 2021 Intel Corporation. */

#include "memkind_memtier.h"
#include <assert.h>
#include <cstring>
#include <pthread.h>
#include <stdint.h>
#include <thread>

#include <vector>

#define SIZE         512
#define ITERATION    10000000
#define THREAD_COUNT 8

int memkind_bench_thread(size_t ntimes, size_t nworks)
{
    std::vector<std::thread> vthread(nworks);
    for (size_t k = 0; k < nworks; ++k) {
        vthread[k] = std::thread([&]() {
            std::vector<void *> v;
            v.reserve(ntimes);
            for (size_t i = 0; i < ntimes; i++) {
                v.push_back(memkind_malloc(MEMKIND_DEFAULT, SIZE));
            }
            for (size_t i = 0; i < ntimes; i++) {
                memkind_free(MEMKIND_DEFAULT, v[i]);
            }
            v.clear();
        });
    }
    for (auto &t : vthread) {
        t.join();
    }
    return 0;
}

int memkind_bench_single(size_t ntimes)
{
    std::vector<void *> v;
    v.reserve(ntimes);
    for (size_t i = 0; i < ntimes; i++) {
        v.push_back(memkind_malloc(MEMKIND_DEFAULT, SIZE));
    }
    for (size_t i = 0; i < ntimes; i++) {
        memkind_free(MEMKIND_DEFAULT, v[i]);
    }
    v.clear();
    return 0;
}

int memtier_bench_single(size_t ntimes)
{
    std::vector<void *> v;
    v.reserve(ntimes);
    for (size_t i = 0; i < ntimes; i++) {
        v.push_back(memtier_kind_malloc(MEMKIND_DEFAULT, SIZE));
    }
    for (size_t i = 0; i < ntimes; i++) {
        memtier_kind_realloc(MEMKIND_DEFAULT, v[i], 0);
    }
    v.clear();
    return 0;
}

int memtier_bench_thread(size_t ntimes, size_t nworks)
{
    std::vector<std::thread> vthread(nworks);
    for (size_t k = 0; k < nworks; ++k) {
        vthread[k] = std::thread([&]() {
            std::vector<void *> v;
            v.reserve(ntimes);
            for (size_t i = 0; i < ntimes; i++) {
                v.push_back(memtier_kind_malloc(MEMKIND_DEFAULT, SIZE));
            }
            for (size_t i = 0; i < ntimes; i++) {
                memtier_kind_realloc(MEMKIND_DEFAULT, v[i], 0);
            }
            v.clear();
        });
    }
    for (auto &t : vthread) {
        t.join();
    }
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        exit(1);
    }

    if (strcmp(argv[1], "memkind_thread") == 0) {
        return memkind_bench_thread(ITERATION, THREAD_COUNT);
    } else if (strcmp(argv[1], "memtier_thread") == 0) {
        return memtier_bench_thread(ITERATION, THREAD_COUNT);
    } else if (strcmp(argv[1], "memkind_single") == 0) {
        return memkind_bench_single(ITERATION);
    } else if (strcmp(argv[1], "memtier_single") == 0) {
        return memtier_bench_single(ITERATION);
    } else {
        exit(1);
    }
}