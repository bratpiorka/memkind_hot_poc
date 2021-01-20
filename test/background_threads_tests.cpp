// SPDX-License-Identifier: BSD-2-Clause
/* Copyright (C) 2021 Intel Corporation. */

#include <memkind.h>

#include "common.h"

#include <numa.h>

class BackgroundThreadsTests: public :: testing::Test
{
protected:
    void SetUp()
    {}

    void TearDown()
    {}

};

TEST_F(BackgroundThreadsTests, test_TC_MEMKIND_BackgroundThreadsEnable)
{
    unsigned cpus_count = numa_num_configured_cpus();
    std::set<void *> allocations;
    const size_t alloc_size = 1 * MB;

    void *ptr = memkind_malloc(MEMKIND_REGULAR, alloc_size);


    for (unsigned i = 0; i < 10 * cpus_count; i++) {
        ASSERT_NE(nullptr, ptr);
        memset(ptr, 'a', alloc_size);
        allocations.insert(ptr);
    }

    bool enable = true, read_state = false;
    bool *enable_ptr = &enable;
    bool *read_state_ptr = &read_state;
    size_t sz = sizeof(bool);
    ASSERT_TRUE(memkind_set_bg_threads(NULL, sz, enable_ptr, sz) == 0); // enable bg threads
    ASSERT_TRUE(memkind_set_bg_threads(read_state_ptr, sz, NULL, 0) == 0); // read bg threads state
    ASSERT_TRUE(read_state);
    enable = false;
    ASSERT_TRUE(memkind_set_bg_threads(NULL, sz, enable_ptr, sz) == 0); // disable bg threads
    ASSERT_TRUE(memkind_set_bg_threads(read_state_ptr, sz, NULL, 0) == 0); // read bg threads state
    ASSERT_FALSE(read_state);
    enable = true;
    ASSERT_TRUE(memkind_set_bg_threads(NULL, sz, enable_ptr, sz) == 0); // enable bg threads
    ASSERT_TRUE(memkind_set_bg_threads(read_state_ptr, sz, NULL, 0) == 0); // read bg threads state
    ASSERT_TRUE(read_state);

    for (auto const &ptr: allocations) {
        memkind_free(MEMKIND_REGULAR, ptr);
    }
}