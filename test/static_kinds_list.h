// SPDX-License-Identifier: BSD-2-Clause
/* Copyright (C) 2016 - 2021 Intel Corporation. */

#pragma once

#include <memkind.h>


static memkind_t static_kinds_list[] = {
    MEMKIND_DEFAULT,
    MEMKIND_HBW,
    MEMKIND_HBW_HUGETLB,
    MEMKIND_HBW_PREFERRED,
    MEMKIND_HBW_PREFERRED_HUGETLB,
    MEMKIND_HUGETLB,
    MEMKIND_HBW_GBTLB,
    MEMKIND_HBW_PREFERRED_GBTLB,
    MEMKIND_GBTLB,
    MEMKIND_HBW_INTERLEAVE,
    MEMKIND_INTERLEAVE,
    MEMKIND_DAX_KMEM,
    MEMKIND_DAX_KMEM_ALL,
    MEMKIND_DAX_KMEM_PREFERRED,
    MEMKIND_DAX_KMEM_INTERLEAVE,
    MEMKIND_HIGHEST_CAPACITY,
    MEMKIND_HIGHEST_CAPACITY_PREFERRED,
    MEMKIND_HIGHEST_CAPACITY_LOCAL,
    MEMKIND_HIGHEST_CAPACITY_LOCAL_PREFERRED
};


