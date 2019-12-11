/*
 * Copyright (C) 2019 Intel Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice(s),
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice(s),
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO
 * EVENT SHALL THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <memkind.h>
#include <vector>
#include <numaif.h>
#include <numa.h>
#include "common.h"
#include "allocator_perf_tool/TimerSysTime.hpp"
#include "allocator_perf_tool/GTestAdapter.hpp"
#include "allocator_perf_tool/VectorIterator.hpp"
#include "allocator_perf_tool/AllocationSizes.hpp"

class DaxKmemAllocPerformanceTests: public :: testing::Test,
    public ::testing::WithParamInterface<memkind_t>
{
protected:
    memkind_t kind;
    void SetUp()
    {
        kind = GetParam();
    }

    void TearDown()
    {}

};

INSTANTIATE_TEST_CASE_P(
    KindParam, DaxKmemAllocPerformanceTests,
    ::testing::Values(MEMKIND_DEFAULT, MEMKIND_DAX_KMEM, MEMKIND_DAX_KMEM_ALL,
                      MEMKIND_DAX_KMEM_PREFERRED));

//Test dax kmem allocation performance.
TEST_P(DaxKmemAllocPerformanceTests, test_TC_MEMKIND_MEMKIND_DAX_KMEM_malloc)
{
    void *ptr;
    //const size_t alloc_size = 1 * MB;
    int seed = time(nullptr);
    VectorIterator<size_t> allocation_sizes =
        AllocationSizes::generate_random_sizes(10, 32, 1 * MB, seed);
    std::vector<void *> pointers_vec;
    //const unsigned allocations = 10000;
    TimerSysTime timer;
    std::vector<double> times_vec;
    //size_t numa_size;
    //int numa_id = -1;
    size_t alloc_size;

    ptr = memkind_malloc(kind, alloc_size);
    ASSERT_NE(nullptr, ptr);
    memset(ptr, 'a', alloc_size);

    //get_mempolicy(&numa_id, nullptr, 0, ptr, MPOL_F_NODE | MPOL_F_ADDR);
    //numa_size = numa_node_size64(numa_id, nullptr);

    timer.start();
    alloc_size = allocation_sizes. rand() % allocation_sizes.size();
    //while (0.98 * numa_size > alloc_size * pointers_vec.size()) {
    while (9 * GB > alloc_size * pointers_vec.size()) {
        alloc_size =
        ptr = memkind_malloc(kind, alloc_size);
        ASSERT_NE(nullptr, ptr);
        memset(ptr, 'a', alloc_size);
        double elapsed_time = timer.getElapsedTime();
        times_vec.push_back(elapsed_time);
        pointers_vec.push_back(ptr);
    }

    double total_elapsed_time = 0;
    for (auto const &time: times_vec) {
        total_elapsed_time += time;
    }
    double mean_elapsed_time = total_elapsed_time / times_vec.size();

    GTestAdapter::RecordProperty("total_time_spent_on_alloc", total_elapsed_time);
    GTestAdapter::RecordProperty("mean_time_spent_on_alloc", mean_elapsed_time);

    for (auto const &ptr: pointers_vec) {
        memkind_free(nullptr, ptr);
    }
}
