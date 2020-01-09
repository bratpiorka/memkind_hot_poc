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

#include <chrono>
#include <numa.h>
#include <set>
#include <sched.h>

#include "common.h"
#include "allocator_perf_tool/Configuration.hpp"
#include "allocator_perf_tool/StressIncreaseToMaxDaxKmem.h"
#include "allocator_perf_tool/HugePageOrganizer.hpp"
#include "dax_kmem_api.h"

//static std::set<int> get_dax_kmem_nodes(void)
//{
//    struct bitmask *cpu_mask = numa_allocate_cpumask();
//    std::set<int> dax_kmem_nodes;
//    long long free_space;
//    const int MAXNODE_ID = numa_num_configured_nodes();
//    for (int id = 0; id < MAXNODE_ID; ++id) {
//        numa_node_to_cpus(id, cpu_mask);
//        // Check if numa node exists and if it is NUMA node created from persistent memory
//        if (numa_node_size64(id, &free_space) > 0 &&
//            numa_bitmask_weight(cpu_mask) == 0) {
//            dax_kmem_nodes.insert(id);
//        }
//    }
//    numa_free_cpumask(cpu_mask);
//    return dax_kmem_nodes;
//}

//static std::set<int> get_closest_dax_kmem_numa_nodes(int regular_node)
//{
//    int min_distance = INT_MAX;
//    std::set<int> closest_numa_ids;
//    std::set<int> dax_kmem_nodes = get_dax_kmem_nodes();
//    for (auto const &node: dax_kmem_nodes) {
//        int distance_to_i_node = numa_distance(regular_node, node);
//        if (distance_to_i_node < min_distance) {
//            min_distance = distance_to_i_node;
//            closest_numa_ids.clear();
//            closest_numa_ids.insert(node);
//        } else if (distance_to_i_node == min_distance) {
//            closest_numa_ids.insert(node);
//        }
//    }
//    return closest_numa_ids;
//}

//memkind stress and longevity tests using Allocatr Perf Tool.
class MemkindDaxKmemPerfTestsParam: public ::testing::Test,
    public ::testing::WithParamInterface<unsigned>
{
protected:
    unsigned kind;
    DaxKmem dax_kmem;

    void SetUp()
    {
        kind = GetParam();
    }

    void TearDown()
    {}

    //Allocates memory up to 'memory_request_limit'.
    void run(TypesConf kinds, TypesConf func_calls, unsigned operations,
             size_t size_from, size_t size_to, size_t memory_request_limit,
             bool touch_memory)
    {
        RecordProperty("memory_operations", operations);
        RecordProperty("size_from", size_from);
        RecordProperty("size_to", size_to);

        TaskConf task_conf = {
            .n = operations, //number of memory operations
            .allocation_sizes_conf = {
                operations, //number of memory operations
                size_from, //no random sizes.
                size_to
            },
            .func_calls = func_calls, //enable allocator function call
            .allocators_types = kinds, //enable allocator
            .seed = 11, //random seed
            .touch_memory = touch_memory //enable or disable touching memory
        };

        std::chrono::time_point<std::chrono::system_clock> start, end;
        start = std::chrono::system_clock::now();

        //Execute test iterations.
        std::vector<iteration_result> results =
            StressIncreaseToMaxDaxKmem::execute_test_iterations(task_conf, 120,
                                                         memory_request_limit);

        end = std::chrono::system_clock::now();

        std::chrono::duration<double> elapsed_time = end - start;

        RecordProperty("elapsed_time", elapsed_time.count());

        //Check finish status.
        EXPECT_EQ(check_allocation_errors(results, task_conf), 0);
    }

    //Check true allocation errors over all iterations.
    //Return iteration number (>0) when error occurs, or zero
    int check_allocation_errors(std::vector<iteration_result> &results,
                                const TaskConf &task_conf)
    {
        for (size_t i=0; i<results.size(); i++) {
            //Check if test ends with allocation error.
            if(results[i].is_allocation_error) {
                return i+1;
            }
        }

        return 0;
    }
};

INSTANTIATE_TEST_CASE_P(
    KindParam, MemkindDaxKmemPerfTestsParam,
    ::testing::Values(AllocatorTypes::MEMKIND_DAX_KMEM, AllocatorTypes::MEMKIND_DAX_KMEM_ALL,
                      AllocatorTypes::MEMKIND_DAX_KMEM_PREFERRED));

TEST_P(MemkindDaxKmemPerfTestsParam,
       test_TC_MEMKIND_DAX_KMEM_malloc_calloc_realloc_free_max_memory)
{
    TypesConf kinds;
    kinds.enable_type(kind);
    TypesConf func_calls;
    func_calls.enable_type(FunctionCalls::MALLOC);
    func_calls.enable_type(FunctionCalls::CALLOC);
    func_calls.enable_type(FunctionCalls::REALLOC);
    func_calls.enable_type(FunctionCalls::FREE);

    long long free_space;
    int process_cpu = sched_getcpu();
    int process_node = numa_node_of_cpu(process_cpu);
    std::set<int> closest_dax_kmem_nodes = dax_kmem.get_closest_dax_kmem_numa_nodes(process_node);
    int closest_dax_kmem_node = *closest_dax_kmem_nodes.begin();
    numa_node_size64(closest_dax_kmem_node, &free_space);
    unsigned long long min_alloc_size = 1 * KB;
    unsigned long long max_alloc_size = 1 * MB;
    unsigned long long max_allocated_memory = 0.99 * free_space;

    run(kinds, func_calls, max_allocated_memory / min_alloc_size, min_alloc_size, max_alloc_size, max_allocated_memory, true);
}

TEST_P(MemkindDaxKmemPerfTestsParam,
       test_TC_MEMKIND_DAX_KMEM_malloc_max_memory)
{
    TypesConf kinds;
    kinds.enable_type(kind);
    TypesConf func_calls;
    func_calls.enable_type(FunctionCalls::MALLOC);

    long long free_space;
    int process_cpu = sched_getcpu();
    int process_node = numa_node_of_cpu(process_cpu);
    std::set<int> closest_dax_kmem_nodes = dax_kmem.get_closest_dax_kmem_numa_nodes(process_node);
    int closest_dax_kmem_node = *closest_dax_kmem_nodes.begin();
    numa_node_size64(closest_dax_kmem_node, &free_space);
    unsigned long long min_alloc_size = 1 * KB;
    unsigned long long max_alloc_size = 1 * MB;
    unsigned long long max_allocated_memory = 0.99 * free_space;

    run(kinds, func_calls, max_allocated_memory / min_alloc_size, min_alloc_size, max_alloc_size, max_allocated_memory, true);
}
