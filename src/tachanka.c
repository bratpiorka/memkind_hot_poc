
#include <memkind/internal/memkind_memtier.h>
#include <memkind/internal/memkind_log.h>
#include <memkind/internal/bthash.h>
#include <memkind/internal/critnib.h>
#include <memkind/internal/tachanka.h>
#include <memkind/internal/ranking.h>
#include <memkind/internal/lockless_srmw_queue.h>
#include <memkind/internal/slab_allocator.h>

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <stdatomic.h>
#include <assert.h>

// DEBUG
#ifndef MEMKIND_EXPORT
#define MEMKIND_EXPORT __attribute__((visibility("default")))
#endif

static slab_alloc_t g_tblockAlloc;
static slab_alloc_t g_ttypeAlloc;

// TODO (possibly) move elsewhere - make sure multiple rankings are supported!
static ranking_t *ranking;
static lq_buffer_t ranking_event_buff;
static _Atomic double g_dramToTotalMemRatio=1.0;
/*static*/ critnib *hash_to_type, *addr_to_block;
#define ADD(var,x) __sync_fetch_and_add(&(var), (x))
#define SUB(var,x) __sync_fetch_and_sub(&(var), (x))

#if CHECK_ADDED_SIZE
static size_t g_total_critnib_size=0u;
static size_t g_total_ranking_size=0u;
#endif

void register_block(uint64_t hash, void *addr, size_t size)
{
    struct ttype *t;
#if CHECK_ADDED_SIZE
    assert(g_total_ranking_size == g_total_critnib_size);
#endif

    //printf("hash: %lu\n", hash);

    int nt = critnib_get(hash_to_type, hash);
    if (nt == -1) {
        t = slab_alloc_malloc(&g_ttypeAlloc);
        t->hash = hash;
        t->total_size = 0; // will be incremented later
        t->timestamp_state = TIMESTAMP_NOT_SET;
        if (critnib_insert(hash_to_type, nt) == EEXIST) {
            // TODO FREE nt !!!
            nt = critnib_get(hash_to_type, hash); // raced with another thread
            if (nt == -1) {
                log_fatal("Alloc type disappeared?!?");
                exit(-1);
            }
            t = &ttypes[nt]; // TODO
        }
        t->n1=0u;
        t->n2=0u;
        t->touchCb = NULL;
        t->touchCbArg = NULL;
#if PRINT_POLICY_LOG_STATISTICS_INFO
        static atomic_uint_fast64_t counter=0;
        counter++;
        log_info("new type created, total types: %lu", counter);
#endif
    } else {
        t = &ttypes[nt]; // TODO
    }

    t->num_allocs++;
    t->total_size+= size;

    struct tblock *bl = slab_alloc_malloc(&g_tblockAlloc);

    bl->addr = addr;
    bl->size = size;
    bl->type = nt;

#if PRINT_CRITNIB_NEW_BLOCK_REGISTERED_INFO
    log_info("New block %p registered: addr %p size %lu type %d", bl, (void*)addr, size, nt);
#endif

    critnib_insert(addr_to_block, fb); // TODO
#if CHECK_ADDED_SIZE
    g_total_critnib_size += size;
#endif
}

void realloc_block(void *addr, void *new_addr, size_t size)
{
    int bln = critnib_remove(addr_to_block, (intptr_t)addr);

    if (bln == -1)
    {
#if PRINT_CRITNIB_NOT_FOUND_ON_REALLOC_WARNING
        log_info("WARNING: Tried realloc a non-allocated block at %p", addr);
#endif
#if CRASH_ON_BLOCK_NOT_FOUND
        assert(false && "dealloc non-allocated block!"); // TODO remove!
#endif
        return;
    }
    struct tblock *bl = &tblocks[bln]; // TODO
#if CHECK_ADDED_SIZE
    assert(g_total_critnib_size >= bl->size);
    g_total_critnib_size -= bl->size;
    assert(g_total_ranking_size == g_total_critnib_size);
#endif

#if PRINT_CRITNIB_REALLOC_INFO
    log_info("realloc %p -> %p (block %d, type %d)", addr, new_addr, bln, bl->type);
#endif

    bl->addr = new_addr;
    struct ttype *t = bl->type; // TODO
    SUB(t->total_size, bl->size);
    ADD(t->total_size, size);
    // TODO the new block might have completely different hash/type ...
    critnib_insert(addr_to_block, bln);
#if CHECK_ADDED_SIZE
    g_total_critnib_size += size;
#endif
}

void register_block_in_ranking(void * addr, size_t size)
{
    int bln = critnib_find_le(addr_to_block, (uint64_t)addr);
    if (bln == -1) {
#if PRINT_CRITNIB_NOT_FOUND_ON_UNREGISTER_BLOCK_WARNING
        log_info("WARNING: Tried deallocating a non-allocated block at %p", addr);
#endif
#if CRASH_ON_BLOCK_NOT_FOUND
        assert(false && "only existing blocks can be unregistered!");
#endif
        return;
    }
    int type = tblocks[bln].type; // TODO
    assert(type != -1);
#if CHECK_ADDED_SIZE
    volatile size_t pre_real_ranking_size = ranking_calculate_total_size(ranking);
#endif
    ranking_add(ranking, ttypes[type].f, size);
#if CHECK_ADDED_SIZE
    g_total_ranking_size += size;
    assert(g_total_ranking_size == g_total_critnib_size);
    volatile size_t real_ranking_size = ranking_calculate_total_size(ranking);
    assert(g_total_ranking_size == real_ranking_size);
    assert(pre_real_ranking_size + size == real_ranking_size);
//     wre_destroy(temp_cpy);
#endif
}

void unregister_block_from_ranking(void * addr)
{
#if CHECK_ADDED_SIZE
    assert(g_total_ranking_size == g_total_critnib_size);
#endif
    int bln = critnib_find_le(addr_to_block, (uint64_t)addr);
    if (bln == -1) {
        assert(false && "only existing blocks can be unregistered!");
        return;
    }
    int type = tblocks[bln].type;
    assert(type != -1);
    ranking_remove(ranking, ttypes[type].f, tblocks[bln].size);
#if CHECK_ADDED_SIZE
    assert(g_total_ranking_size >= tblocks[bln].size);
    g_total_ranking_size -= tblocks[bln].size;
#endif
}

void unregister_block(void *addr)
{
    int bln = critnib_remove(addr_to_block, (intptr_t)addr);
    if (bln == -1)
    {
#if PRINT_CRITNIB_NOT_FOUND_ON_UNREGISTER_BLOCK_WARNING
        log_info("WARNING: Tried deallocating a non-allocated block at %p", addr);
#endif
#if CRASH_ON_BLOCK_NOT_FOUND
        assert(false && "dealloc non-allocated block!"); // TODO remove!
#endif
        return;
    }
    struct tblock *bl = &tblocks[bln]; // TODO
    struct ttype *t = bl->type;
    SUB(t->num_allocs, 1);
    SUB(t->total_size, bl->size);
#if CHECK_ADDED_SIZE
    assert(g_total_critnib_size >= bl->size);
    g_total_critnib_size -= bl->size;
    assert(g_total_ranking_size == g_total_critnib_size);
    assert(g_total_ranking_size == ranking_calculate_total_size(ranking));
#endif
    bl->addr = 0;
    bl->size = 0;
    slab_alloc_free(bl);
}

MEMKIND_EXPORT Hotness_e tachanka_get_hotness_type(const void *addr)
{
    int bln = critnib_find_le(addr_to_block, (uintptr_t)addr);

    // TODO
    if (bln < 0 || addr >= tblocks[bln].addr + tblocks[bln].size)
        return HOTNESS_NOT_FOUND;
    struct ttype *t = &ttypes[tblocks[bln].type];

    //printf("get_hotness block %d, type %d hot %g\n", bln, tblocks[bln].type, ttypes[tblocks[bln].type].f);

    if (ranking_is_hot(ranking, t))
        return HOTNESS_HOT;
    return HOTNESS_COLD;
}

MEMKIND_EXPORT double tachanka_get_hot_thresh(void)
{
    return ranking_get_hot_threshold(ranking);
}

MEMKIND_EXPORT Hotness_e tachanka_get_hotness_type_hash(uint64_t hash)
{
    Hotness_e ret = HOTNESS_NOT_FOUND;
    int nt = critnib_get(hash_to_type, hash);
    if (nt != -1) {
        struct ttype *t = &ttypes[nt]; // TODO
        if (ranking_is_hot(ranking, t))
            ret = HOTNESS_HOT;
        else
            ret = HOTNESS_COLD;
    }

    return ret;
}

/// @warning NOT THREAD SAFE
/// This function operates on block that should not be freed/modifed
/// in the meantime
void touch(void *addr, __u64 timestamp, int from_malloc)
{
#if CHECK_ADDED_SIZE
    assert(g_total_ranking_size == ranking_calculate_total_size(ranking));
#endif
    int bln = critnib_find_le(addr_to_block, (uint64_t)addr);
    if (bln == -1) {
#if PRINT_CRITNIB_NOT_FOUND_ON_TOUCH_WARNING
        log_info("WARNING: Addr %p not in known tachanka range  %p - %p", (char*)addr,
            (char*)bl->addr, (char*)(bl->addr + bl->size));
#endif
        assert(from_malloc == 0);
        return;
    }
    struct tblock *bl = &tblocks[bln]; // TODO
    if ((char*)addr >= (char*)(bl->addr + bl->size)) {
#if PRINT_CRITNIB_NOT_FOUND_ON_TOUCH_WARNING
        log_info("WARNING: Addr %p not in known tachanka range  %p - %p", (char*)addr,
            (char*)bl->addr, (char*)(bl->addr + bl->size));
#endif
        assert(from_malloc == 0);
        return;
    }

    //printf("bln: %d", bln);
    //printf("bl->type: %d\n", bl->type);

//     else
//     {
//         printf("tachanka touch for known area!\n");
//     }
    assert(bl->type >= 0);
//     assert(bl->type > 0); TODO check if this is the case in our scenario
    struct ttype *t = bl->type;
    // TODO - is this thread safeness needed? or best effort will be enough?
    //__sync_fetch_and_add(&t->accesses, 1);

//     int hotness =1 ;
    size_t total_size = t->total_size;
    if (total_size>0) {
        if (from_malloc) {
            assert(from_malloc == 1); // other case should not occur
            // TODO clean this up, this is (should be?) dead code
            assert(false); // interface changed, this should never be called
//             ranking_add(ranking, bl); // first of all, add
    //         hotness=INIT_MALLOC_HOTNESS; TODO this does not work, for now
        } else {
            double hotness = 1e16/total_size ;
            ranking_touch(ranking, t, timestamp, hotness);
        }
    }

#if PRINT_CRITNIB_TOUCH_INFO
    static atomic_uint_fast16_t counter=0;
    const uint64_t interval=1000;
    if (++counter > interval) {
        struct timespec t;
        int ret = clock_gettime(CLOCK_MONOTONIC, &t);
        if (ret != 0) {
            log_fatal("ASSERT TOUCH COUNTER FAILURE!\n");
            exit(-1);
        }
        log_info("touch counter %lu hit, [seconds, nanoseconds]: [%ld, %ld]",
            interval, t.tv_sec, t.tv_nsec);
        counter=0u;
    }
#endif

    // TODO make decisions regarding thread-safeness
    // thread-safeness:
    //  - can we actually touch a removed structure?
    //  - is it a problem?
    // current solution: assert(FALSE)
    // future solution: ignore?
//     printf("touches tachanka, timestamp: [%llu]\n", timestamp);
#if CHECK_ADDED_SIZE
    assert(g_total_ranking_size == ranking_calculate_total_size(ranking));
#endif
}

static bool initialized=false;
void tachanka_init(double old_window_hotness_weight, size_t event_queue_size)
{
#if CHECK_ADDED_SIZE
    // re-initalize global variables
    g_total_critnib_size=0u;
    g_total_ranking_size=0u;
#endif

    slab_alloc_init(&g_tblockAlloc, sizeof(struct tblock), 0);
    slab_alloc_init(&g_ttypeAlloc, sizeof(struct ttype), 0);

    read_maps();

    // TODO
    addr_to_block = critnib_new((uint64_t*)tblocks, sizeof(tblocks[0]) / sizeof(uint64_t));
    hash_to_type = critnib_new((uint64_t*)ttypes, sizeof(ttypes[0]) / sizeof(uint64_t));

    ranking_create(&ranking, old_window_hotness_weight);
    ranking_event_init(&ranking_event_buff, event_queue_size);

    initialized = true;
}

MEMKIND_EXPORT void tachanka_set_dram_total_ratio(double ratio)
{
    if (ratio < 0 || ratio > 1) {
        log_fatal("Incorrect ratio [%f], exiting", ratio);
        exit(-1);
    }

    g_dramToTotalMemRatio = ratio;
}

void tachanka_update_threshold(void)
{
    ranking_calculate_hot_threshold_dram_total(ranking, g_dramToTotalMemRatio);
}

void tachanka_destroy(void)
{
    slab_alloc_destroy(&g_tblockAlloc);
    slab_alloc_destroy(&g_typeAlloc);
    ranking_destroy(ranking);
    ranking_event_destroy(&ranking_event_buff);
}

static int _size;
static double _hotness;

static int size_hotness(int nt)
{
    if (ttypes[nt].total_size == _size) {
        _hotness = ttypes[nt].f;
        return 1;
    }
    return 0;
}

MEMKIND_EXPORT double tachanka_get_obj_hotness(int size)
{
    _size = size;
    _hotness = -1;
    critnib_iter(hash_to_type, size_hotness);
    return _hotness;
}

MEMKIND_EXPORT double tachanka_get_addr_hotness(void *addr)
{
    double ret = -1;
    int bln = critnib_find_le(addr_to_block, (uintptr_t)addr);
    if (bln >= 0) {
        struct ttype *t = &ttypes[tblocks[bln].type];
        ret = t->f;
    }
    return ret;
}

// MEMKIND_EXPORT double tachanka_set_touch_callback(void *addr, const char *name)
MEMKIND_EXPORT int tachanka_set_touch_callback(void *addr, tachanka_touch_callback cb, void* arg)
{
    int ret = -1;
    int bln = critnib_find_le(addr_to_block, (uintptr_t)addr);
    if (bln>=0) {
        struct ttype *t = &ttypes[tblocks[bln].type];

        ranking_set_touch_callback(ranking, cb, arg, t);
        ret=0;
    }
    return ret;
}

MEMKIND_EXPORT bool tachanka_ranking_event_push(EventEntry_t *event)
{
    if (initialized == false) {
        log_fatal("push onto non-initialized queue");
        exit(-1);
    }
    return ranking_event_push(&ranking_event_buff, event);
}

MEMKIND_EXPORT bool tachanka_ranking_event_pop(EventEntry_t *event)
{
    if (initialized == false) {
        log_fatal("pop from a non-initialized queue");
        exit(-1);
    }
    return ranking_event_pop(&ranking_event_buff, event);
}
