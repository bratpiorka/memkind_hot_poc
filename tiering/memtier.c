// SPDX-License-Identifier: BSD-2-Clause
/* Copyright (C) 2021 Intel Corporation. */

#include "../include/memkind/internal/memkind_private.h"
#include "../include/memkind_memtier.h"

#include "ctl.h"
#include "memtier_log.h"

#include <pthread.h>

#define MEMTIER_EXPORT __attribute__((visibility("default")))
#define MEMTIER_INIT   __attribute__((constructor))
#define MEMTIER_FINI   __attribute__((destructor))

#define MEMTIER_LIKELY(x)   __builtin_expect((x), 1)
#define MEMTIER_UNLIKELY(x) __builtin_expect((x), 0)

static int initialized = 0;
static int destructed = 0;

struct memtier_kind *current_kind;
struct memtier_tier *current_tier;

MEMTIER_EXPORT void *malloc(size_t size)
{
    void *ret = NULL;

    if (MEMTIER_LIKELY(initialized)) {
        ret = memtier_kind_malloc(current_kind, size);
    } else if (destructed == 0) {
        ret = memkind_malloc(MEMKIND_DEFAULT, size);
    }

    log_debug("malloc(%zu) = %p", size, ret);
    return ret;
}

MEMTIER_EXPORT void *calloc(size_t num, size_t size)
{
    void *ret = NULL;

    if (MEMTIER_LIKELY(initialized)) {
        ret = memtier_kind_calloc(current_kind, num, size);
    } else if (destructed == 0) {
        ret = memkind_calloc(MEMKIND_DEFAULT, num, size);
    }

    log_debug("calloc(%zu, %zu) = %p", num, size, ret);
    return ret;
}

MEMTIER_EXPORT void *realloc(void *ptr, size_t size)
{
    void *ret = NULL;

    if (MEMTIER_LIKELY(initialized)) {
        ret = memtier_kind_realloc(current_kind, ptr, size);
    } else if (destructed == 0) {
        ret = memkind_realloc(MEMKIND_DEFAULT, ptr, size);
    }

    log_debug("realloc(%p, %zu) = %p", ptr, size, ret);
    return ret;
}

// TODO add memalign

MEMTIER_EXPORT void free(void *ptr)
{
    if (MEMTIER_LIKELY(initialized)) {
        memtier_free(ptr);
    } else if (destructed == 0) {
        memkind_free(MEMKIND_DEFAULT, ptr);
    }

    log_debug("free(%p)", ptr);
}

static int create_tiered_kind_from_env(char *env_var_string)
{
    char *kind_name;
    char *pmem_path;
    char *pmem_size;
    unsigned ratio_value;
    ctl_load_config(env_var_string, &kind_name, &pmem_path, &pmem_size,
                    &ratio_value);

    log_debug("kind_name: %s", kind_name);
    log_debug("pmem_path: %s", pmem_path);
    log_debug("pmem_size: %s", pmem_size);
    log_debug("ratio_value: %u", ratio_value);

    // TODO add "DRAM" -> MEMKIND_DEFAULT etc. mapping logic
    current_tier = memtier_tier_new(MEMKIND_DEFAULT);
    struct memtier_builder *builder = memtier_builder();
    memtier_builder_add_tier(builder, current_tier, ratio_value);
    memtier_builder_set_policy(builder, MEMTIER_DUMMY_VALUE);
    memtier_builder_construct_kind(builder, &current_kind);

    return 0;
}

static pthread_once_t init_once = PTHREAD_ONCE_INIT;

static MEMTIER_INIT void memtier_init(void)
{
    pthread_once(&init_once, log_init_once);

    // TODO: Handle failure when this variable (or config variable) is not
    // present
    char *env_var = utils_get_env("MEMKIND_MEM_TIERING_CONFIG");
    if (env_var) {
        log_info("Memkind memtier lib loaded!");
        create_tiered_kind_from_env(env_var);
        initialized = 1;
    }
}

static MEMTIER_FINI void memtier_fini(void)
{
    log_info("Unloading memkind memtier lib!");

    if (current_kind) {
        // TODO unify names
        memtier_tier_delete(current_tier);
        memtier_delete_kind(current_kind);
    }

    destructed = 1;
    initialized = 0;
}
