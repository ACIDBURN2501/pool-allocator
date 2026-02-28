/**
 * @file test_pool.c
 * @brief Unit tests for the static object pool allocator library.
 *
 * Each test function returns 0 on pass, 1 on failure.
 * main() returns 0 if all tests pass, 1 otherwise.
 */

#include "pool.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Test storage
 * ------------------------------------------------------------------------- */

static struct pool_t g_pool;

/* -------------------------------------------------------------------------
 * Minimal test runner (no external dependencies)
 * ------------------------------------------------------------------------- */

static int g_tests_run = 0;
static int g_tests_failed = 0;

#define RUN_TEST(fn)                                                           \
        do {                                                                   \
                g_tests_run++;                                                 \
                if ((fn)() != 0) {                                             \
                        printf("FAIL  %s\n", #fn);                             \
                        g_tests_failed++;                                      \
                } else {                                                       \
                        printf("pass  %s\n", #fn);                             \
                }                                                              \
        } while (0)

/* =========================================================================
 * Test Case Helpers
 * ========================================================================= */

static pool_handle_t
get_test_pool(void)
{
        return &g_pool;
}

/* =========================================================================
 * Test Cases - 10 comprehensive tests covering all aspects of operation
 * and safety
 * ========================================================================= */

/** Test 1: Verify initialization clears state and marks slots free */
static int
test_pool_init(void)
{
        pool_handle_t pool = get_test_pool();

        memset(&g_pool, 0xFFU, sizeof(g_pool));

        pool_status_t status = pool_init(pool);

        if (status != POOL_OK) {
                return 1;
        }

        {
                bool seen[POOL_MAX_SLOTS];
                memset(seen, 0, sizeof(seen));

                for (pool_id_t i = 0U; i < (pool_id_t)POOL_MAX_SLOTS; i++) {
                        pool_id_t id = (pool_id_t)0xFFFFU;
                        if (pool_acquire(pool, &id) != POOL_OK) {
                                return 1;
                        }
                        if (id >= (pool_id_t)POOL_MAX_SLOTS) {
                                return 1;
                        }
                        if (seen[id]) {
                                return 1;
                        }
                        seen[id] = true;

                        uint8_t *ptr = (uint8_t *)pool_get_pointer(pool, id);
                        if (ptr == NULL) {
                                return 1;
                        }
                        if (ptr[0] != 0x00U) {
                                return 1;
                        }
                }

                for (pool_id_t id = 0U; id < (pool_id_t)POOL_MAX_SLOTS; id++) {
                        if (pool_release(pool, id) != POOL_OK) {
                                return 1;
                        }
                }
        }

        return 0;
}

/** Test 2: Verify pool_init rejects NULL pointer */
static int
test_pool_init_null_ptr(void)
{
        pool_status_t status = pool_init(NULL);

        return (status == POOL_ERR_NULL_PTR) ? 0 : 1;
}

/** Test 3: Verify successful acquisition returns valid ID and pointer */
static int
test_pool_acquire_success(void)
{
        pool_handle_t pool = get_test_pool();

        memset(&g_pool, 0x55U, sizeof(g_pool));

        pool_init(pool);

        pool_id_t id = (pool_id_t)0xFFU;
        pool_status_t status = pool_acquire(pool, &id);

        if (status != POOL_OK || id >= POOL_MAX_SLOTS) {
                return 1;
        }

        void *ptr = pool_get_pointer(pool, id);

        if (ptr == NULL) {
                return 1;
        }

        memset(ptr, 0xAAU, POOL_ITEM_SIZE);

        if (((uint8_t *)ptr)[0] != 0xAAU) {
                return 1;
        }

        return 0;
}

/** Test 4: Verify pool_acquire rejects NULL pool handle */
static int
test_pool_acquire_null_pool(void)
{
        pool_id_t id = (pool_id_t)0xFFU;
        pool_status_t status = pool_acquire(NULL, &id);

        return (status == POOL_ERR_NULL_PTR) ? 0 : 1;
}

/** Test 5: Verify pool_acquire rejects NULL output pointer */
static int
test_pool_acquire_null_id(void)
{
        pool_handle_t pool = get_test_pool();

        memset(&g_pool, 0U, sizeof(g_pool));

        pool_init(pool);

        pool_status_t status = pool_acquire(pool, NULL);

        return (status == POOL_ERR_NULL_PTR) ? 0 : 1;
}

/** Test 6: Verify pool is full after acquiring all slots */
static int
test_pool_acquire_full_pool(void)
{
        pool_handle_t pool = get_test_pool();

        memset(&g_pool, 0U, sizeof(g_pool));

        pool_init(pool);

        pool_id_t ids[POOL_MAX_SLOTS];
        pool_id_t acquired_count = 0U;

        for (pool_id_t i = 0U; i < (pool_id_t)POOL_MAX_SLOTS; i++) {
                if (pool_acquire(pool, &ids[i]) == POOL_OK) {
                        acquired_count++;
                } else {
                        return 1;
                }
        }

        if (acquired_count != (pool_id_t)POOL_MAX_SLOTS) {
                return 1;
        }

        pool_id_t extra_id = (pool_id_t)0xFFU;
        pool_status_t status = pool_acquire(pool, &extra_id);

        if (status != POOL_ERR_FULL || extra_id != (pool_id_t)0xFFU) {
                return 1;
        }

        return 0;
}

/** Test 7: Verify successful release frees slot for reuse */
static int
test_pool_release_success(void)
{
        pool_handle_t pool = get_test_pool();

        memset(&g_pool, 0U, sizeof(g_pool));

        pool_init(pool);

        pool_id_t ids[POOL_MAX_SLOTS];

        for (pool_id_t i = 0U; i < (pool_id_t)POOL_MAX_SLOTS; i++) {
                if (pool_acquire(pool, &ids[i]) != POOL_OK) {
                        return 1;
                }
        }

        /* Pool is full; freeing one slot means the next acquire must return it
         */
        pool_id_t id = ids[(pool_id_t)(POOL_MAX_SLOTS / 2U)];

        pool_status_t status = pool_release(pool, id);

        if (status != POOL_OK) {
                return 1;
        }

        pool_id_t reused_id = (pool_id_t)0xFFFFU;
        status = pool_acquire(pool, &reused_id);

        if (status != POOL_OK) {
                return 1;
        }

        if (reused_id != id) {
                return 1;
        }

        return 0;
}

/** Test 8: Verify release rejects invalid/out-of-bounds ID */
static int
test_pool_release_invalid_id(void)
{
        pool_handle_t pool = get_test_pool();

        memset(&g_pool, 0U, sizeof(g_pool));

        pool_init(pool);

        pool_status_t status;

        status = pool_release(pool, (pool_id_t)POOL_MAX_SLOTS);

        if (status != POOL_ERR_INVALID_ID) {
                return 1;
        }

        status =
            pool_release(pool, (pool_id_t)((pool_id_t)POOL_MAX_SLOTS + 5U));

        if (status != POOL_ERR_INVALID_ID) {
                return 1;
        }

        return 0;
}

/** Test 9: Verify double-free is detected and rejected */
static int
test_pool_double_free(void)
{
        pool_handle_t pool = get_test_pool();

        memset(&g_pool, 0U, sizeof(g_pool));

        pool_init(pool);

        pool_id_t id = (pool_id_t)3U;

        pool_acquire(pool, &id);
        pool_release(pool, id);

        pool_status_t status = pool_release(pool, id);

        return (status == POOL_ERR_INVALID_ID) ? 0 : 1;
}

/** Test 10: Verify get_pointer rejects invalid/bounds IDs and NULL pool */
static int
test_pool_get_pointer_invalid_id(void)
{
        pool_handle_t pool = get_test_pool();

        memset(&g_pool, 0U, sizeof(g_pool));

        pool_init(pool);

        if (pool_get_pointer(pool, (pool_id_t)POOL_MAX_SLOTS) != NULL) {
                return 1;
        }

        if (pool_get_pointer(pool, (pool_id_t)((pool_id_t)POOL_MAX_SLOTS + 1U))
            != NULL) {
                return 1;
        }

        if (pool_get_pointer(NULL, 0U) != NULL) {
                return 1;
        }

        if (pool_get_pointer(pool, (pool_id_t)((pool_id_t)POOL_MAX_SLOTS + 10U))
            != NULL) {
                return 1;
        }

        return 0;
}

/** Test 11: Verify checked pointer accessor enforces allocation state */
static int
test_pool_get_pointer_checked(void)
{
        pool_handle_t pool = get_test_pool();

        memset(&g_pool, 0U, sizeof(g_pool));
        pool_init(pool);

        void *ptr = (void *)0x1;
        if (pool_get_pointer_checked(pool, 0U, &ptr) != POOL_ERR_INVALID_ID) {
                return 1;
        }
        if (ptr != NULL) {
                return 1;
        }

        pool_id_t id = 0U;
        if (pool_acquire(pool, &id) != POOL_OK) {
                return 1;
        }

        if (pool_get_pointer_checked(pool, id, &ptr) != POOL_OK) {
                return 1;
        }
        if (ptr == NULL) {
                return 1;
        }

        if (pool_release(pool, id) != POOL_OK) {
                return 1;
        }

        ptr = (void *)0x1;
        if (pool_get_pointer_checked(pool, id, &ptr) != POOL_ERR_INVALID_ID) {
                return 1;
        }
        if (ptr != NULL) {
                return 1;
        }

        return 0;
}

/* =========================================================================
 * Entry point
 * ========================================================================= */

int
main(void)
{
        printf("Running %d unit tests...\n\n", 11);

        RUN_TEST(test_pool_init);
        RUN_TEST(test_pool_init_null_ptr);
        RUN_TEST(test_pool_acquire_success);
        RUN_TEST(test_pool_acquire_null_pool);
        RUN_TEST(test_pool_acquire_null_id);
        RUN_TEST(test_pool_acquire_full_pool);
        RUN_TEST(test_pool_release_success);
        RUN_TEST(test_pool_release_invalid_id);
        RUN_TEST(test_pool_double_free);
        RUN_TEST(test_pool_get_pointer_invalid_id);
        RUN_TEST(test_pool_get_pointer_checked);

        printf("\n%d/%d tests passed.\n", g_tests_run - g_tests_failed,
               g_tests_run);

        return (g_tests_failed == 0) ? 0 : 1;
}
