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
 * Test Cases - cover all aspects of operation and safety
 * ========================================================================= */

/** Test 1: Verify initialization clears state and marks slots free */
static int
test_pool_init(void)
{
        pool_handle_t pool = get_test_pool();

        memset(&g_pool, 0xFF, sizeof(g_pool));

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

                        pool_byte_t *ptr =
                            (pool_byte_t *)pool_get_pointer(pool, id);
                        if (ptr == NULL) {
                                return 1;
                        }
                        if (ptr[0] != (pool_byte_t)0x00) {
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

        memset(&g_pool, 0x55, sizeof(g_pool));

        pool_init(pool);

        pool_id_t id = (pool_id_t)0xFFU;
        pool_status_t status = pool_acquire(pool, &id);

        if (status != POOL_OK || id >= POOL_MAX_SLOTS) {
                return 1;
        }

        pool_byte_t *ptr = (pool_byte_t *)pool_get_pointer(pool, id);

        if (ptr == NULL) {
                return 1;
        }

        /*
         * Per-element fill keeps the test source identical across 8-bit
         * and 16-bit MAU targets. A memset(...,POOL_ITEM_SIZE) would
         * write byte-units regardless of the underlying pool_byte_t
         * width and produce different per-element values on the two
         * paths.
         */
        for (size_t i = 0U; i < (size_t)POOL_ITEM_SIZE; i++) {
                ptr[i] = (pool_byte_t)0xAA;
        }

        if (ptr[0] != (pool_byte_t)0xAA) {
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

        memset(&g_pool, 0, sizeof(g_pool));

        pool_init(pool);

        pool_status_t status = pool_acquire(pool, NULL);

        return (status == POOL_ERR_NULL_PTR) ? 0 : 1;
}

/** Test 6: Verify pool is full after acquiring all slots */
static int
test_pool_acquire_full_pool(void)
{
        pool_handle_t pool = get_test_pool();

        memset(&g_pool, 0, sizeof(g_pool));

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

        memset(&g_pool, 0, sizeof(g_pool));

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

        memset(&g_pool, 0, sizeof(g_pool));

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

        memset(&g_pool, 0, sizeof(g_pool));

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

        memset(&g_pool, 0, sizeof(g_pool));

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

        memset(&g_pool, 0, sizeof(g_pool));
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

/** Test 12: Verify operations fail on uninitialized pool with garbage state */
static int
test_pool_uninitialized_garbage(void)
{
        pool_handle_t pool = get_test_pool();

        /* Fill with garbage (not 0 or 1) */
        memset(&g_pool, 0xAA, sizeof(g_pool));

        /* pool_release should fail */
        if (pool_release(pool, 0U) != POOL_ERR_INVALID_ID) {
                return 1;
        }

        /* pool_get_pointer should fail */
        if (pool_get_pointer(pool, 0U) != NULL) {
                return 1;
        }

        /* pool_get_pointer_checked should fail */
        void *ptr = (void *)0x1;
        if (pool_get_pointer_checked(pool, 0U, &ptr) != POOL_ERR_INVALID_ID) {
                return 1;
        }
        if (ptr != NULL) {
                return 1;
        }

        return 0;
}

/** Test 13: Verify pointer alignment for all slots */
static int
test_pool_alignment(void)
{
        pool_handle_t pool = get_test_pool();
        pool_init(pool);

        for (pool_id_t i = 0U; i < (pool_id_t)POOL_MAX_SLOTS; i++) {
                pool_id_t id;
                if (pool_acquire(pool, &id) != POOL_OK) {
                        return 1;
                }

                void *ptr = pool_get_pointer(pool, id);
                if (((uintptr_t)ptr % (uintptr_t)_Alignof(pool_align_t))
                    != 0U) {
                        return 1;
                }
        }

        return 0;
}

/** Test 14: Verify pool_release clears the entire item memory */
static int
test_pool_release_clears_memory(void)
{
        pool_handle_t pool = get_test_pool();
        pool_init(pool);

        pool_id_t id;
        pool_acquire(pool, &id);
        pool_byte_t *ptr = (pool_byte_t *)pool_get_pointer(pool, id);

        /*
         * Fill with non-zero pool_byte_t units. Per-element assignment
         * works correctly on both 8-bit and 16-bit MAU targets; a
         * memset would write byte-units and only half-fill the slot in
         * the 16-bit case.
         */
        for (size_t i = 0U; i < (size_t)POOL_ITEM_SIZE; i++) {
                ptr[i] = (pool_byte_t)0xA5;
        }

        if (pool_release(pool, id) != POOL_OK) {
                return 1;
        }

        /* Accessing the pointer after release is technically UB if the pool
         * were dynamic, but here we are checking the static storage directly
         * via the internal knowledge of where that ID mapped. */
        for (size_t i = 0U; i < (size_t)POOL_ITEM_SIZE; i++) {
                if (ptr[i] != (pool_byte_t)0x00) {
                        return 1;
                }
        }

        return 0;
}

/** Test 15: Verify no overlap between adjacent slots */
static int
test_pool_no_overlap(void)
{
        pool_handle_t pool = get_test_pool();
        pool_init(pool);

        pool_byte_t *ptrs[POOL_MAX_SLOTS];

        for (pool_id_t i = 0U; i < (pool_id_t)POOL_MAX_SLOTS; i++) {
                pool_id_t id;
                pool_acquire(pool, &id);
                ptrs[i] = (pool_byte_t *)pool_get_pointer(pool, id);
                /*
                 * Per-element fill: a memset(...,POOL_ITEM_SIZE) would
                 * be byte-oriented and fill differently on 8-bit vs
                 * 16-bit MAU targets.
                 */
                for (size_t j = 0U; j < (size_t)POOL_ITEM_SIZE; j++) {
                        ptrs[i][j] = (pool_byte_t)(i + 1U);
                }
        }

        /* Verify no slot was corrupted by another's initialization */
        for (pool_id_t i = 0U; i < (pool_id_t)POOL_MAX_SLOTS; i++) {
                for (size_t j = 0U; j < (size_t)POOL_ITEM_SIZE; j++) {
                        if (ptrs[i][j] != (pool_byte_t)(i + 1U)) {
                                return 1;
                        }
                }
        }

        /* Verify pointer distance */
        for (pool_id_t i = 0U; i < (pool_id_t)(POOL_MAX_SLOTS - 1U); i++) {
                for (pool_id_t k = (pool_id_t)(i + 1U);
                     k < (pool_id_t)POOL_MAX_SLOTS; k++) {
                        uintptr_t p1 = (uintptr_t)ptrs[i];
                        uintptr_t p2 = (uintptr_t)ptrs[k];
                        uintptr_t diff = (p1 > p2) ? (p1 - p2) : (p2 - p1);

                        if (diff < (uintptr_t)POOL_ITEM_SIZE) {
                                return 1;
                        }
                }
        }

        return 0;
}

/** Test 16: Verify the static footprint descriptor is self-consistent */
static int
test_pool_footprint(void)
{
        pool_footprint_t fp = pool_footprint();

        /* Compile-time properties must be reported verbatim. */
        if (fp.capacity != (pool_id_t)POOL_MAX_SLOTS) {
                return 1;
        }
        if (fp.item_size != (uint16_t)POOL_ITEM_SIZE) {
                return 1;
        }
        if (fp.addr_unit_bits != (uint8_t)POOL_ADDR_UNIT_BITS) {
                return 1;
        }

        /* Instance size must match sizeof and the payload macro. */
        if (fp.instance_size_units != sizeof(struct pool_t)) {
                return 1;
        }
        if (fp.payload_units != POOL_PAYLOAD_UNITS) {
                return 1;
        }

        /* Payload fits inside the instance; overhead is the remainder. */
        if (fp.payload_units > fp.instance_size_units) {
                return 1;
        }
        if (fp.overhead_units != (fp.instance_size_units - fp.payload_units)) {
                return 1;
        }

        /* Octet normalisation: identity on 8-bit, x2 on 16-bit MAU. */
        if (fp.instance_size_octets
            != fp.instance_size_units * ((size_t)POOL_ADDR_UNIT_BITS / 8U)) {
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
        printf("Running %d unit tests...\n\n", 16);

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
        RUN_TEST(test_pool_uninitialized_garbage);
        RUN_TEST(test_pool_alignment);
        RUN_TEST(test_pool_release_clears_memory);
        RUN_TEST(test_pool_no_overlap);
        RUN_TEST(test_pool_footprint);

        printf("\n%d/%d tests passed.\n", g_tests_run - g_tests_failed,
               g_tests_run);

        return (g_tests_failed == 0) ? 0 : 1;
}
