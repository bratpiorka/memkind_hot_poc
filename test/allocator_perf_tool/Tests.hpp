/*
* Copyright (C) 2015 - 2018 Intel Corporation.
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

/*
This file contain self-tests for Allocator Perf Tool.
*/
#pragma once

#include <unistd.h>

#include "Configuration.hpp"
#include "AllocatorFactory.hpp"
#include "AllocationSizes.hpp"
#include "VectorIterator.hpp"
#include "StandardAllocatorWithTimer.hpp"
#include "ScenarioWorkload.h"
#include "TaskFactory.hpp"
#include "Task.hpp"

//Basic test for allocators. Allocate 32 bytes by malloc, then deallocate it by free.
void test_allocator(Allocator& allocator, const char* allocator_name)
{
    printf("\n============= Allocator (%s) test ============= \n",allocator_name);

    memory_operation data = allocator.wrapped_malloc(32);
    printf("malloc: %p \n",data.ptr);

    assert(data.ptr!=NULL);

    allocator.wrapped_free(data.ptr);
    printf("free: %p \n",data.ptr);

    printf("\n==================================================== \n");
}

//Test for the workload classes. This test count and validate the number or runs.
void test_workload(Workload& workload, const int N, const char* workload_name)
{
    printf("\n============= Work load (%s) test ============= \n",workload_name);

    assert(workload.run()); // Do "single" memory operation (allocate or deallocate).

    int i;
    for (i=1; workload.run(); i++); // Do the rest of operations.
    assert(i == N);

    printf("\n==================================================== \n");
}

//Simulate time consuming memory operation.
memory_operation time_consuming_memory_operation(unsigned int seconds)
{
    size_t size;

    START_TEST(0,0)
    sleep(seconds);
    END_TEST
}

//This test check if timer has measured time.
void test_timer()
{
    float total_time = time_consuming_memory_operation(2).total_time;

    bool test_res = (total_time >=  2.0) && (total_time < 2.2);
    if(!test_res)
        printf("test_timer(): unexpected timing %f", total_time);
    assert(test_res);
}

//Test for iterators. Check the number of iterations, such as:
//N == number of iterations.
template<class T>
void test_iterator(Iterator<T>& it, const int N)
{
    printf("\n================= Iteartor test ==================== \n");

    assert(it.size() == N);
    int i;
    for (i=0; it.has_next(); i++) {
        it.next();
    }
    printf("iterations=%d/%d\n",i,N);
    assert(i == N);

    printf("\n==================================================== \n");
}

//This test validate range of values generated by random generators.
template<class T, class C>
void test_iterator_values(VectorIterator<T>& it, const C from, const C to)
{
    for (int i=0; it.has_next(); i++) {
        T val = it.next();

        if(val < from) std::cout << "ivalid value: actual=" << val << ", expected= >="
                                     << from << std::endl;

        if(val > to) std::cout << "ivalid value: actual=" << val << ", expected= <=" <<
                                   to << std::endl;
    }
}

//Test time counting instrumentation in StandardAllocatorWithTimer.
//The instrumentation is made with START_TEST and END_TEST macros from WrapperMacros.h.
void test_allocator_with_timer(int N, int seed)
{
    printf("\n============= Allocator with timer test ============= \n");

    StandardAllocatorWithTimer allocator;
    VectorIterator<size_t> allocation_sizes =
        AllocationSizes::generate_random_sizes(N, 32, 2048, seed);
    memory_operation data;

    double elaspsed_time = 0;
    for (int i=0; i<N; i++) {
        data = allocator.wrapped_malloc(allocation_sizes.next());
        elaspsed_time += data.total_time;
        allocator.wrapped_free(data.ptr);
    }

    printf("%d allocations and frees done in time: %f \n", N, elaspsed_time);

    printf("\n==================================================== \n");
}

//Test behavior of TypesConfiguration class, by enabling and disabling types.
void test_types_conf()
{
    TypesConf types;

    for (unsigned i=0; i<FunctionCalls::NUM_OF_FUNCTIONS; i++) {
        types.enable_type(i);
        assert(types.is_enabled(i));
        types.disable_type(i);
        assert(!types.is_enabled(i));
    }
}

void execute_self_tests()
{
    const int N = 10000;
    const size_t size_from = 32, size_to = 2048;
    const unsigned seed = 11;

    test_types_conf();

    {
        VectorIterator<size_t> it = AllocationSizes::generate_random_sizes(N, size_from,
                                                                           size_to,seed);
        test_iterator_values(it, size_from, size_to);
    }

    {
        TypesConf enable_func_calls;
        enable_func_calls.enable_type(FunctionCalls::MALLOC);

        VectorIterator<int> it = FunctionCalls::generate_random_allocator_func_calls(N,
                                                                                     seed, enable_func_calls);
        test_iterator(it, N);
    }

//Timer implementation __cplusplus < 201100L is based on CPU clocks counting and it will fail on this test.
#if __cplusplus > 201100L
    test_timer();
#endif

    printf("Test completed! (press ENTER to continue)\n");
}

