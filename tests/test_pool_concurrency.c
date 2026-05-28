/**
 * @file test_pool_concurrency.c
 * @brief Single-writer / many-readers stress test for the pool allocator.
 *
 * The documented threading contract (see pool.h) is:
 *   - SINGLE WRITER: one context calls pool_init / pool_acquire /
 *     pool_release.
 *   - MANY READERS: any number of contexts may call pool_get_pointer /
 *     pool_get_pointer_checked.
 *
 * This test spawns one writer thread that cycles acquire/release on the
 * full pool and several reader threads that hammer the pointer accessors
 * against random IDs. The accessors are expected to return either a
 * valid in-storage pointer (when the slot is currently USED) or NULL
 * (when it is FREE or out of range) - both are valid observations under
 * the contract.
 *
 * Under POOL_ATOMIC_MODE_C11 (the default on hosted targets), the
 * per-slot status flags are _Atomic uint_least8_t. ThreadSanitizer
 * should report zero data races. Build with `-Dtsan=true` to enable.
 *
 * Under POOL_ATOMIC_MODE_VOLATILE this test will still pass functionally
 * on a single-core / single-socket host (where naturally-aligned word
 * accesses are atomic), but ThreadSanitizer will flag races - that is
 * expected behaviour for the volatile path and not a bug.
 */

/* rand_r is POSIX; needed for re-entrant per-thread RNG state. */
#define _POSIX_C_SOURCE 200809L

#include "pool.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define WRITER_ITERATIONS   200000
#define READER_ITERATIONS   200000
#define READER_THREAD_COUNT 4

static struct pool_t g_pool;

typedef struct {
        pool_handle_t pool;
        atomic_bool *stop;
        unsigned int seed;
        atomic_uint observations;
} reader_args_t;

typedef struct {
        pool_handle_t pool;
        atomic_bool *stop;
} writer_args_t;

static void *
writer_thread_func(void *arg)
{
        writer_args_t *args = (writer_args_t *)arg;
        pool_id_t owned[POOL_MAX_SLOTS];
        size_t owned_count = 0U;

        for (size_t i = 0U; i < (size_t)WRITER_ITERATIONS; i++) {
                /*
                 * Mix acquires and releases so the pool oscillates near
                 * full and near empty, maximising the chance of a
                 * reader observing a slot mid-transition.
                 */
                if ((owned_count == 0U)
                    || ((owned_count < POOL_MAX_SLOTS) && ((i & 1U) == 0U))) {
                        pool_id_t id;
                        if (pool_acquire(args->pool, &id) == POOL_OK) {
                                owned[owned_count++] = id;
                        }
                } else if (owned_count > 0U) {
                        const pool_id_t id = owned[--owned_count];
                        (void)pool_release(args->pool, id);
                }
        }

        /* Drain any remaining owned slots so the pool ends clean. */
        while (owned_count > 0U) {
                (void)pool_release(args->pool, owned[--owned_count]);
        }

        atomic_store(args->stop, true);
        return NULL;
}

static void *
reader_thread_func(void *arg)
{
        reader_args_t *args = (reader_args_t *)arg;
        unsigned int seed = args->seed;
        unsigned int local_obs = 0U;

        for (size_t i = 0U;
             (i < (size_t)READER_ITERATIONS) && !atomic_load(args->stop); i++) {
                const pool_id_t id =
                    (pool_id_t)(rand_r(&seed) % (int)POOL_MAX_SLOTS);

                /*
                 * Either accessor may legitimately return NULL (FREE
                 * slot) or a valid pointer (USED slot). Both outcomes
                 * are correct; we only assert that valid pointers fall
                 * within the storage block.
                 */
                void *p = pool_get_pointer(args->pool, id);
                (void)p;

                void *p2 = (void *)0x1;
                pool_status_t st =
                    pool_get_pointer_checked(args->pool, id, &p2);
                if (st == POOL_OK) {
                        if (p2 == NULL) {
                                fprintf(stderr,
                                        "reader: checked accessor returned "
                                        "POOL_OK with NULL pointer\n");
                                return (void *)1;
                        }
                } else if (st == POOL_ERR_INVALID_ID) {
                        if (p2 != NULL) {
                                fprintf(stderr,
                                        "reader: checked accessor returned "
                                        "INVALID_ID with non-NULL pointer\n");
                                return (void *)1;
                        }
                } else {
                        fprintf(stderr,
                                "reader: unexpected checked-accessor "
                                "status %d\n",
                                (int)st);
                        return (void *)1;
                }

                local_obs++;
        }

        atomic_fetch_add(&args->observations, local_obs);
        return NULL;
}

int
main(void)
{
        pthread_t writer;
        pthread_t readers[READER_THREAD_COUNT];
        atomic_bool stop = ATOMIC_VAR_INIT(false);
        reader_args_t reader_args[READER_THREAD_COUNT];
        writer_args_t writer_args = {.pool = &g_pool, .stop = &stop};

        if (pool_init(&g_pool) != POOL_OK) {
                fprintf(stderr, "pool_init failed\n");
                return 1;
        }

        printf("pool concurrency stress: 1 writer x %d iters, %d readers "
               "x %d iters each\n",
               WRITER_ITERATIONS, READER_THREAD_COUNT, READER_ITERATIONS);

        for (size_t i = 0U; i < (size_t)READER_THREAD_COUNT; i++) {
                reader_args[i].pool = &g_pool;
                reader_args[i].stop = &stop;
                reader_args[i].seed = (unsigned int)(0xC0FFEEU + i);
                atomic_init(&reader_args[i].observations, 0U);
                if (pthread_create(&readers[i], NULL, reader_thread_func,
                                   &reader_args[i])
                    != 0) {
                        fprintf(stderr, "pthread_create reader %zu failed\n",
                                i);
                        return 1;
                }
        }

        if (pthread_create(&writer, NULL, writer_thread_func, &writer_args)
            != 0) {
                fprintf(stderr, "pthread_create writer failed\n");
                return 1;
        }

        void *writer_ret = NULL;
        pthread_join(writer, &writer_ret);

        unsigned int total_obs = 0U;
        for (size_t i = 0U; i < (size_t)READER_THREAD_COUNT; i++) {
                void *r = NULL;
                pthread_join(readers[i], &r);
                if (r != NULL) {
                        fprintf(stderr, "reader %zu reported failure\n", i);
                        return 1;
                }
                total_obs += atomic_load(&reader_args[i].observations);
        }

        printf("pool concurrency stress: passed (%u reader observations)\n",
               total_obs);
        return 0;
}
