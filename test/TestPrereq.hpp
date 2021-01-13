// SPDX-License-Identifier: BSD-2-Clause
/* Copyright (C) 2021 Intel Corporation. */
#pragma once

#include <climits>
#include <unordered_set>
#include <stdexcept>
#include "common.h"
#include "numa.h"
#include "numaif.h"
#include "config.h"
#ifdef MEMKIND_HWLOC
#include "hwloc.h"
#endif
class TestPrereq
{
private:

#ifdef MEMKIND_HWLOC
    hwloc_topology_t topology;
#endif


#define CPUID_MODEL_SHIFT       (4)
#define CPUID_MODEL_MASK        (0xf)
#define CPUID_EXT_MODEL_MASK    (0xf)
#define CPUID_EXT_MODEL_SHIFT   (16)
#define CPUID_FAMILY_MASK       (0xf)
#define CPUID_FAMILY_SHIFT      (8)
#define CPU_MODEL_KNL           (0x57)
#define CPU_MODEL_KNM           (0x85)
#define CPU_MODEL_CLX           (0x55)
#define CPU_FAMILY_INTEL        (0x06)

    typedef struct {
        uint32_t model;
        uint32_t family;
    } cpu_model_data_t;

    typedef struct registers_t {
        uint32_t eax;
        uint32_t ebx;
        uint32_t ecx;
        uint32_t edx;
    } registers_t;

    inline static void cpuid_asm(int leaf, int subleaf, registers_t *registers)
    {
#ifdef __x86_64__
        asm volatile("cpuid":"=a"(registers->eax),
                     "=b"(registers->ebx),
                     "=c"(registers->ecx),
                     "=d"(registers->edx):"0"(leaf), "2"(subleaf));
#else
        registers->eax = 0;
#endif
    }

    static cpu_model_data_t get_cpu_model_data()
    {
        registers_t registers;
        cpuid_asm(1, 0, &registers);
        uint32_t model = (registers.eax >> CPUID_MODEL_SHIFT) & CPUID_MODEL_MASK;
        uint32_t model_ext = (registers.eax >> CPUID_EXT_MODEL_SHIFT) &
                             CPUID_EXT_MODEL_MASK;

        cpu_model_data_t data;
        data.model = model | (model_ext << 4);
        data.family = (registers.eax >> CPUID_FAMILY_SHIFT) & CPUID_FAMILY_MASK;
        return data;
    }
public:
    TestPrereq()
    {
#ifdef MEMKIND_HWLOC
        if (hwloc_topology_init(&topology))
            throw std::runtime_error("Cannot create initialize hwloc topology.");
        if (hwloc_topology_load(topology))
            throw std::runtime_error("Cannot load hwloc topology.");
#endif
    }

    ~TestPrereq()
    {
#ifdef MEMKIND_HWLOC
        hwloc_topology_destroy(topology);
#endif
    }
    enum memory_var {HBM, PMEM};

    bool check_cpu(memory_var variant)
    {
        cpu_model_data_t cpu = get_cpu_model_data();
        switch(variant) {
            case HBM:
                return cpu.family == CPU_FAMILY_INTEL &&
                       (cpu.model == CPU_MODEL_KNL || cpu.model == CPU_MODEL_KNM);
            case PMEM:
                return cpu.family == CPU_FAMILY_INTEL && (cpu.model == CPU_MODEL_CLX);
            default:
                return false;
        }
    }

    std::unordered_set<int> get_closest_numa_nodes(int first_node,
                                                   std::unordered_set<int> nodes)
    {
        int min_distance = INT_MAX;
        std::unordered_set<int> closest_numa_ids;

        for (auto const &node: nodes) {
            int distance_to_i_node = numa_distance(first_node, node);

            if (distance_to_i_node < min_distance) {
                min_distance = distance_to_i_node;
                closest_numa_ids.clear();
                closest_numa_ids.insert(node);
            } else if (distance_to_i_node == min_distance) {
                closest_numa_ids.insert(node);
            }
        }
        return closest_numa_ids;
    }

    bool is_libhwloc_supported(void)
    {
#ifdef MEMKIND_HWLOC
        return true;
#else
        return false;
#endif
    }

    bool is_libdaxctl_supported(void)
    {
#ifdef MEMKIND_DAXCTL_KMEM
        return true;
#else
        return false;
#endif
    }

    std::unordered_set<int> get_memory_only_numa_nodes(void)
    {
        struct bitmask *cpu_mask = numa_allocate_cpumask();
        std::unordered_set<int> mem_only_nodes;

        const int MAXNODE_ID = numa_max_node();
        for (int id = 0; id <= MAXNODE_ID; ++id) {
            int res = numa_node_to_cpus(id, cpu_mask);
            if (res == -1) {
                continue;
            }

            if (numa_node_size64(id, nullptr) > 0 &&
                numa_bitmask_weight(cpu_mask) == 0) {
                mem_only_nodes.insert(id);
            }
        }
        numa_free_cpumask(cpu_mask);

        return mem_only_nodes;
    }

    std::unordered_set<int> get_regular_numa_nodes(void)
    {
        struct bitmask *cpu_mask = numa_allocate_cpumask();
        std::unordered_set<int> regular_nodes;

        const int MAXNODE_ID = numa_max_node();
        for (int id = 0; id <= MAXNODE_ID; ++id) {
            int res = numa_node_to_cpus(id, cpu_mask);
            if (res == -1) {
                continue;
            }
            if (numa_bitmask_weight(cpu_mask) != 0) {
                regular_nodes.insert(id);
            }
        }
        numa_free_cpumask(cpu_mask);

        return regular_nodes;
    }

    size_t get_free_space(std::unordered_set<int> nodes)
    {
        size_t sum_of_free_space = 0;
        long long free_space;

        for(auto const &node: nodes) {
            int result = numa_node_size64(node, &free_space);
            if (result == -1)
                continue;
            sum_of_free_space += free_space;
        }

        return sum_of_free_space;
    }

    bool is_kind_prefferred(memkind_t kind)
    {
        return (kind == MEMKIND_HBW_PREFERRED ||
                kind == MEMKIND_HBW_PREFERRED_HUGETLB ||
                kind == MEMKIND_HBW_PREFERRED_GBTLB ||
                kind == MEMKIND_DAX_KMEM_PREFERRED);
    }

    bool is_preferred_supported()
    {
        auto regular_nodes = get_regular_numa_nodes();
        auto mem_only_nodes = get_memory_only_numa_nodes();
        for (auto const &node: regular_nodes) {
            auto closest_mem_only_nodes = get_closest_numa_nodes(node, mem_only_nodes);
            if (closest_mem_only_nodes.size() > 1) {
                std::cout << "More than one NUMA Node are in the same distance to: " << node <<
                          std::endl;
                return false;
            }
        }
        return true;
    }

    bool is_MCDRAM_supported()
    {
        if (getenv("MEMKIND_HBW_NODES") != NULL)
            return true;
        if (!check_cpu(HBM))
            return false;
#ifdef MEMKIND_HWLOC
        hwloc_obj_t node = nullptr;
        while ((node = hwloc_get_next_obj_by_type(topology, HWLOC_OBJ_NUMANODE,
                                                  node)) != nullptr) {
            if (node->subtype && !strcmp(node->subtype, "MCDRAM")) {
                return true;
            }
        }
        return false;
#else
        auto mem_only_nodes = get_memory_only_numa_nodes();
        return mem_only_nodes.size() != 0;
#endif
    }

    bool is_DAX_KMEM_supported(void)
    {
        if (getenv("MEMKIND_DAX_KMEM_NODES") != nullptr)
            return true;
        if (!check_cpu(PMEM))
            return false;
#ifdef MEMKIND_HWLOC
        hwloc_obj_t node = nullptr;
        while ((node = hwloc_get_next_obj_by_type(topology, HWLOC_OBJ_NUMANODE,
                                                  node)) != nullptr) {
            if (hwloc_obj_get_info_by_name(node, "DAXDevice") != nullptr) {
                return true;
            }
        }
        return false;
#else
        auto mem_only_nodes = get_memory_only_numa_nodes();
        return mem_only_nodes.size() != 0;
#endif
    }
};
