// SPDX-License-Identifier: BSD-2-Clause
/* Copyright (C) 2021 Intel Corporation. */

#include <hbwmalloc.h>
#include <hwloc.h>
#include <memkind.h>
#include <memory>
#include <numa.h>
#include <numaif.h>
#include <omp.h>
#include <sys/sysinfo.h>

#include "common.h"
#include "memkind/internal/memkind_hbw.h"
#include "memory_topology.h"
#include "TestPrereq.hpp"

using BitmaskPtr =
    std::unique_ptr<struct bitmask, decltype(&numa_free_nodemask)>;

/* This test is run with overridden MEMKIND_HBW_THRESHOLD environment variable
 * and tries to perform allocation from all NUMA nodes that can be innitiators.
 * NOTE: comparison of MEMKIND_HBW_THRESHOLD with actual bandwidth between a
 * pair of nodes requires support of HMAT attributes - they are set only in
 * *HMAT and *HBW topologies.
 */
int main(int argc, char *argv[])
{
    const int retcode_success = 0;
    const int retcode_error = 1;

    if (argc == 1) {
        std::cout << "Error: missing kind parameter" << std::endl;
        return retcode_error;
    }

    memkind_t memory_kind;
    std::string kind_str;
    if (argc == 2) {
        kind_str = std::string(argv[1]);

        if (kind_str == "MEMKIND_HBW") {
            memory_kind = MEMKIND_HBW;
        } else if (kind_str == "MEMKIND_HBW_ALL") {
            memory_kind = MEMKIND_HBW_ALL;
        } else {
            std::cout << "Error: not supported kind: " << kind_str << std::endl;
            return retcode_error;
        }
    }

    const char *memory_tpg = std::getenv("MEMKIND_TEST_TOPOLOGY");
    if (memory_tpg == NULL) {
        std::cout << "Error: MEMKIND_TEST_TOPOLOGY env variable not set" << std::endl;
        return retcode_error;
    }

    const char *threshold_str = std::getenv("MEMKIND_HBW_THRESHOLD");
    if (threshold_str == NULL) {
        std::cout << "Error: MEMKIND_HBW_THRESHOLD env variable not set" << std::endl;
        return retcode_error;
    }

    int threshold_int = std::stoi(std::string(threshold_str));

    std::cout << "MEMKIND_TEST_TOPOLOGY is: " << memory_tpg << std::endl;
    std::cout << "MEMKIND_HBW_THRESHOLD is: " << threshold_str << std::endl;
    std::cout << "kind is: " << kind_str << std::endl;

    auto topology = TopologyFactory(memory_tpg);
    TestPrereq tp;
    bool any_fail = false;
    int alloc_num = 0;
    hwloc_topology_t hwloc_topology;

    if (tp.is_libhwloc_supported() == false) {
        std::cout << "Error: libhwloc is required." << std::endl;
        return retcode_error;
    }

    int status = hwloc_topology_init(&hwloc_topology);
    if (status) {
        std::cout << "Error: hwloc initialization failed" << std::endl;
        return retcode_error;
    }

    status = hwloc_topology_load(hwloc_topology);
    if (status) {
        std::cout << "Error: hwloc topology load failed" << std::endl;
        hwloc_topology_destroy(hwloc_topology);
        return retcode_error;
    }

    status = numa_available();
    if (status != 0) {
        std::cout << "Error: numa_available() failed" << std::endl;
        return retcode_error;
    }

    // use big size to ensure that we call jemalloc extent
    const size_t size = 11*MB-5;
    int threads_num = get_nprocs();

    #pragma omp parallel for num_threads(threads_num)
    for (int thread_id = 0; thread_id < threads_num; ++thread_id) {
        cpu_set_t cpu_set;
        CPU_ZERO(&cpu_set);
        CPU_SET(thread_id, &cpu_set);

        status = sched_setaffinity(0, sizeof(cpu_set_t), &cpu_set);
        int cpu = sched_getcpu();
        if (thread_id != cpu) {
            any_fail = true;
            std::cout << "Error: thread_id != cpu" << std::endl;
            continue;
        }

        int init_id = numa_node_of_cpu(cpu);
        void *ptr = memkind_malloc(memory_kind, size);

        // MEMKIND_HBW_THRESHOLD env var doesn't affect KN* family processors
        // so allocation should always succeed
        if (tp.is_KN_family_supported() && ptr != NULL) {
            memkind_free(memory_kind, ptr);
            continue;
        }

        // check if our topology supports given kind and contains HMAT
        // attributes - if not, allocation should fail
        if ((std::string(memory_tpg).find("HMAT") == std::string::npos) &&
            (std::string(memory_tpg).find("HBW") == std::string::npos) &&
            (topology->is_kind_supported(memory_kind) == false)) {
            if (ptr != nullptr) {
                std::cout << "Error: kind not supported but allocation succeed "
                          "for node: " << init_id << std::endl;
                any_fail = true;
                memkind_free(memory_kind, ptr);
            }
            // allocation failed so nothing more to do here
            continue;
        }

        memset(ptr, 0, size);

        // TODO CHECK ALLOCATION MASK HERE

        /*
        BitmaskPtr ptr_nodemask = BitmaskPtr(numa_allocate_nodemask(),
                                             numa_free_nodemask);
        BitmaskPtr target_nodemask = BitmaskPtr(numa_allocate_nodemask(),
                                                numa_free_nodemask);

        // check node from allocation
        int target_id;
        status = get_mempolicy(&target_id, nullptr, 0, ptr,
                               MPOL_F_NODE | MPOL_F_ADDR);
        if (status) {
            std::cout << "Error: get_mempolicy failed" << std::endl;
            any_fail = true;
            memkind_free(memory_kind, ptr);
            continue;
        }

        // check mbind mask from allocation
        int policy;
        status = get_mempolicy(&policy, ptr_nodemask->maskp, ptr_nodemask->size,
                               ptr, MPOL_F_ADDR);
        if (status) {
            std::cout << "Error: get_mempolicy failed" << std::endl;
            any_fail = true;
            memkind_free(memory_kind, ptr);
            continue;
        }

        // node bandwidth validation
        hwloc_obj_t init_node = hwloc_get_numanode_obj_by_os_index(hwloc_topology,
                                                                   init_id);
        hwloc_obj_t target_node = hwloc_get_numanode_obj_by_os_index(hwloc_topology,
                                                                     target_id);

        hwloc_uint64_t attr_val;
        hwloc_location initiator;
        initiator.type =
            hwloc_location::hwloc_location_type_e::HWLOC_LOCATION_TYPE_CPUSET;
        initiator.location.cpuset = init_node->cpuset;
        status = hwloc_memattr_get_value(hwloc_topology, HWLOC_MEMATTR_ID_BANDWIDTH,
                                         target_node, &initiator, 0, &attr_val);
        if (status) {
            std::cout << "Error: hwloc_memattr_get_value failed for nodes " <<
                      init_id << " - " << target_id << std::endl;
            any_fail = true;
            memkind_free(memory_kind, ptr);
            continue;
        }

        // check if bandwith between innitiator node that requested alloation
        // and tharget node where this alloaction was made is higher than
        // threshold - if not, allocation should fail
        if (threshold_int > (int)attr_val) {
            std::cout << "Error: allocation was done from node with bandwidth "
                      "lower than threshold for nodes " << init_id << " - "
                      << target_id << std::endl;
            any_fail = true;
            memkind_free(memory_kind, ptr);
            continue;
        }
        */

        // all checks passed
        memkind_free(memory_kind, ptr);
        alloc_num ++;
    }

    // in case of any fail return error code
    if (any_fail) {
        return retcode_error;
    }

    // TODO WHEN WE EXPECT THAT THERE SHOULD BE NO/LESS SUCCESSFUL ALLOCATIONS
    // THAN NUM OF THREADS HANDLE THIS HERE AND RETURN SUCCESS

    // allocations from all threads should succeed
    return (alloc_num == threads_num) ? retcode_success : retcode_error;
}
