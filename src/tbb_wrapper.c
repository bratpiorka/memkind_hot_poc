/*
 * Copyright (C) 2017 - 2019 Intel Corporation.
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

#include <memkind/internal/memkind_default.h>
#include <memkind/internal/memkind_log.h>
#include <memkind/internal/memkind_private.h>
#include <memkind/internal/memkind_pmem.h>
#include <memkind/internal/tbb_wrapper.h>
#include <memkind/internal/tbb_mem_pool_policy.h>
#include <limits.h>

#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <unistd.h>
void *(*pool_malloc)(void *, size_t);
void *(*pool_realloc)(void *, void *, size_t);
void *(*pool_aligned_malloc)(void *, size_t, size_t);
bool (*pool_free)(void *, void *);
int (*pool_create_v1)(intptr_t, const struct MemPoolPolicy *, void **);
bool (*pool_destroy)(void *);
void *(*pool_identify)(void *object);

void *(*pool_manager_malloc)(size_t);
void *(*pool_manager_calloc)(size_t, size_t);
void  (*pool_manager_free)(void *);

static void *tbb_handle = NULL;
static bool TBBInitDone = false;

void load_tbb_symbols(void)
{
    const char so_name[]="libtbbmalloc.so.2";
    tbb_handle = dlopen(so_name, RTLD_LAZY);
    if(!tbb_handle) {
        log_fatal("%s not found.", so_name);
        abort();
    }

    pool_malloc = dlsym(tbb_handle, "_ZN3rml11pool_mallocEPNS_10MemoryPoolEm");
    pool_realloc = dlsym(tbb_handle, "_ZN3rml12pool_reallocEPNS_10MemoryPoolEPvm");
    pool_aligned_malloc = dlsym(tbb_handle,
                                "_ZN3rml19pool_aligned_mallocEPNS_10MemoryPoolEmm");
    pool_free = dlsym(tbb_handle, "_ZN3rml9pool_freeEPNS_10MemoryPoolEPv");
    pool_create_v1 = dlsym(tbb_handle,
                           "_ZN3rml14pool_create_v1ElPKNS_13MemPoolPolicyEPPNS_10MemoryPoolE");
    pool_destroy = dlsym(tbb_handle, "_ZN3rml12pool_destroyEPNS_10MemoryPoolE");
    pool_identify = dlsym(tbb_handle, "_ZN3rml13pool_identifyEPv");
    pool_manager_malloc = dlsym(tbb_handle, "scalable_malloc");
    pool_manager_calloc  = dlsym(tbb_handle, "scalable_calloc");
    pool_manager_free = dlsym(tbb_handle, "scalable_free");

    if(!pool_malloc ||
       !pool_realloc ||
       !pool_aligned_malloc ||
       !pool_free ||
       !pool_create_v1 ||
       !pool_destroy ||
       !pool_identify ||
       !pool_manager_malloc ||
       !pool_manager_calloc ||
       !pool_manager_free )

    {
        log_fatal("Could not find symbols in %s.", so_name);
        dlclose(tbb_handle);
        abort();
    }

    TBBInitDone = true;
}

//Granularity of raw_alloc allocations
#define GRANULARITY 2*1024*1024
static void *raw_alloc(intptr_t pool_id, size_t *bytes/*=n*GRANULARITY*/)
{
    void *ptr = kind_mmap((struct memkind *)pool_id, NULL, *bytes);
    return (ptr==MAP_FAILED) ? NULL : ptr;
}

static int raw_free(intptr_t pool_id, void *raw_ptr, size_t raw_bytes)
{
    return munmap(raw_ptr, raw_bytes);
}

static void *raw_pmem_alloc(intptr_t pool_id, size_t *bytes/*=n*GRANULARITY*/)
{
    void *ptr = memkind_pmem_mmap((struct memkind *)pool_id, NULL, *bytes);
    return (ptr==MAP_FAILED) ? NULL : ptr;
}

static int raw_pmem_free(intptr_t pool_id, void *raw_ptr, size_t raw_bytes)
{
return madvise(raw_ptr, raw_bytes, MADV_REMOVE);
}

static void *tbb_pool_malloc(struct memkind *kind, size_t size)
{
    if(size_out_of_bounds(size)) return NULL;
    void *result = pool_malloc(kind->pool_area, size);
    if (!result)
        errno = ENOMEM;
    return result;
}

static void *tbb_pool_calloc(struct memkind *kind, size_t num, size_t size)
{
    if (size_out_of_bounds(num) || size_out_of_bounds(size)) return NULL;

    const size_t array_size = num*size;
    if (array_size/num != size) {
        errno = ENOMEM;
        return NULL;
    }
    void *result = pool_malloc(kind->pool_area, array_size);
    if (result) {
        memset(result, 0, array_size);
    } else {
        errno = ENOMEM;
    }
    return result;
}

static void  *tbb_pool_common(void *pool, void *ptr, size_t size)
{
    if(size_out_of_bounds(size))
    {
        pool_free(pool, ptr);
        return NULL;
    }
    void *result = pool_realloc(pool, ptr, size);
    if (!result && size)
        errno = ENOMEM;
    return result;
}

static void *tbb_pool_realloc(struct memkind *kind, void *ptr, size_t size)
{
    return tbb_pool_common(kind->pool_area, ptr, size);
}

void *tbb_pool_realloc_with_kind_detect(void *ptr, size_t size)
{
    if (!ptr) {
        errno = EINVAL;
        return NULL;
    }
    return tbb_pool_common(pool_identify(ptr), ptr, size);
}

static int tbb_pool_posix_memalign(struct memkind *kind, void **memptr,
                                   size_t alignment, size_t size)
{
    //Check if alignment is "at least as large as sizeof(void *)".
    if(!alignment && (0 != (alignment & (alignment-sizeof(void *))))) return EINVAL;
    //Check if alignment is "a power of 2".
    if(alignment & (alignment-1)) return EINVAL;
    if(size_out_of_bounds(size)) return ENOMEM;
    void *result = pool_aligned_malloc(kind->pool_area, size, alignment);
    if (!result) {
        return ENOMEM;
    }
    *memptr = result;
    return 0;
}

void tbb_pool_free_with_kind_detect(void *ptr)
{
    if (ptr) {
        pool_free(pool_identify(ptr), ptr);
    }
}

void tbb_pool_free(struct memkind *kind, void *ptr)
{
    pool_free(kind->pool_area, ptr);
}

static size_t tbb_pool_usable_size(struct memkind *kind, void *ptr)
{
    log_err("memkind_malloc_usable_size() is not supported by TBB.");
    return 0;
}

static int tbb_destroy(struct memkind *kind)
{
    bool pool_destroy_ret = pool_destroy(kind->pool_area);

    if(!pool_destroy_ret) {
        log_err("TBB pool destroy failure.");
        return MEMKIND_ERROR_OPERATION_FAILED;
    }
    return MEMKIND_SUCCESS;
}

static int tbb_pmem_destroy(struct memkind *kind)
{
    struct memkind_pmem *priv = kind->priv;

    pthread_mutex_destroy(&priv->pmem_lock);

    (void) close(priv->fd);

    return tbb_destroy(kind);
}

static int tbb_finalize(struct memkind *kind)
{
    kind->ops->destroy(kind);
    if(TBBInitDone) {
        dlclose(tbb_handle);
        TBBInitDone = false;
    }
    return MEMKIND_SUCCESS;
}

MEMKIND_EXPORT int tbb_pmem_create(struct memkind *kind,
                              struct memkind_ops *ops, const char *name)
{
    struct memkind_pmem *priv = NULL;
    int err;
    priv = (struct memkind_pmem *)pool_manager_malloc(sizeof (struct memkind_pmem));
    if (!priv) {
        log_err("pool_scalable_malloc() failed.");
        return MEMKIND_ERROR_MALLOC;
    }

    if (pthread_mutex_init(&priv->pmem_lock, NULL) != 0) {
        err = MEMKIND_ERROR_RUNTIME;
        goto exit;
    }

    err = memkind_default_create(kind, ops, name);

    kind->priv = priv;
    return 0;

exit:
    /* err is set, please don't overwrite it with result of pthread_mutex_destroy */
    pthread_mutex_destroy(&priv->pmem_lock);
    pool_manager_free(priv);
    return err;
}

MEMKIND_EXPORT struct memkind_ops MEMKIND_TBB_OPS = {
    .create = tbb_pmem_create,
    .destroy = tbb_destroy,
    .malloc = tbb_pool_malloc,
    .calloc = tbb_pool_calloc,
    .posix_memalign = tbb_pool_posix_memalign,
    .realloc = tbb_pool_realloc,
    .free = tbb_pool_free,
    .finalize = tbb_finalize,
    .malloc_usable_size = tbb_pool_usable_size,
};

void tbb_initialize(struct memkind *kind, int is_dynamic)
{
    if(!TBBInitDone)
    {
        log_fatal("Failed to initialize TBB.");
        abort();
    }
    if(!kind && !is_dynamic) {
        log_fatal("Dynamic failed");
        abort();
    }

    struct MemPoolPolicy policy = {
        .pAlloc = raw_alloc,
        .pFree = raw_free,
        .granularity = GRANULARITY,
        .version = 1,
        .fixedPool = false,
        .keepAllMemory = false,
        .reserved = 0
    };

    kind->ops = &MEMKIND_TBB_OPS;


    if(is_dynamic)
    {
        policy.pAlloc = raw_pmem_alloc,
        policy.pFree= raw_pmem_free;
        kind->ops->destroy = tbb_pmem_destroy;
    }

    int test = pool_create_v1((intptr_t)kind, &policy, &kind->pool_area);
    if (test!=0)
    {
        log_fatal("Unable to create TBB memory pool. error %d",test);
        abort();
    }
    if (!kind->pool_area) {
        log_fatal("Unable to create TBB memory pool.");
        abort();
    }
}

void tbb_pool_manager_free(void *ptr)
{
    pool_manager_free(ptr);
}

void* tbb_pool_manager_calloc(size_t num, size_t size)
{
    return pool_manager_calloc(num,size);
}

void* tbb_pool_manager_malloc(size_t size)
{
    return pool_manager_malloc(size);
}
