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
 * Test storage - pools are cast to static buffers of sufficient size
 * ------------------------------------------------------------------------- */

#define TEST_POOL_SIZE (POOL_MAX_SLOTS * POOL_ITEM_SIZE)

static uint8_t g_pool_storage[TEST_POOL_SIZE + 32]; /* alignment padding */

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
        return (pool_handle_t)(g_pool_storage);
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

        memset(g_pool_storage, 0xFFU, sizeof(g_pool_storage));

        pool_status_t status = pool_init(pool);

        if (status != POOL_OK) {
                return 1;
        }

        for (uint8_t i = 0U; i < POOL_MAX_SLOTS; i++) {
                uint8_t *slot_ptr = &((uint8_t *)pool)[sizeof(uint32_t) + i];
                if (*slot_ptr != 0x00U) {
                        return 1;
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

        memset(g_pool_storage, 0x55U, sizeof(g_pool_storage));

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

        memset(g_pool_storage, 0U, sizeof(g_pool_storage));

        pool_init(pool);

        pool_status_t status = pool_acquire(pool, NULL);

        return (status == POOL_ERR_NULL_PTR) ? 0 : 1;
}

/** Test 6: Verify pool is full after acquiring all slots */
static int
test_pool_acquire_full_pool(void)
{
        pool_handle_t pool = get_test_pool();

        memset(g_pool_storage, 0U, sizeof(g_pool_storage));

        pool_init(pool);

        pool_id_t ids[POOL_MAX_SLOTS];
        uint8_t acquired_count = 0U;

        for (uint8_t i = 0U; i < POOL_MAX_SLOTS; i++) {
                if (pool_acquire(pool, &ids[i]) == POOL_OK) {
                        acquired_count++;
                } else {
                        return 1;
                }
        }

        if (acquired_count != POOL_MAX_SLOTS) {
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

        memset(g_pool_storage, 0U, sizeof(g_pool_storage));

        pool_init(pool);

        pool_id_t id = (pool_id_t)5U;
        pool_acquire(pool, &id);

        pool_status_t status = pool_release(pool, id);

        if (status != POOL_OK) {
                return 1;
        }

        pool_id_t reused_id = (pool_id_t)0xFFU;
        status = pool_acquire(pool, &reused_id);

        if (status != POOL_OK || reused_id != id) {
                return 1;
        }

        return 0;
}

/** Test 8: Verify release rejects invalid/out-of-bounds ID */
static int
test_pool_release_invalid_id(void)
{
        pool_handle_t pool = get_test_pool();

        memset(g_pool_storage, 0U, sizeof(g_pool_storage));

        pool_init(pool);

        pool_status_t status;

        status = pool_release(pool, POOL_MAX_SLOTS);

        if (status != POOL_ERR_INVALID_ID) {
                return 1;
        }

        status = pool_release(pool, (POOL_MAX_SLOTS + 5U));

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

        memset(g_pool_storage, 0U, sizeof(g_pool_storage));

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

        memset(g_pool_storage, 0U, sizeof(g_pool_storage));

        pool_init(pool);

        if (pool_get_pointer(pool, POOL_MAX_SLOTS) != NULL) {
                return 1;
        }

        if (pool_get_pointer(pool, 0xFFU) != NULL) {
                return 1;
        }

        if (pool_get_pointer(NULL, 0U) != NULL) {
                return 1;
        }

        if (pool_get_pointer(pool, POOL_MAX_SLOTS + 10U) != NULL) {
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
        printf("Running %d unit tests...\n\n", 10);

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

        printf("\n%d/%d tests passed.\n", g_tests_run - g_tests_failed,
               g_tests_run);

        return (g_tests_failed == 0) ? 0 : 1;
}