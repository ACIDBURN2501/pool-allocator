/**
 * SPDX-License-Identifier: MIT
 *
 * @file: pool.c
 *
 * @brief
 *    Implementation of the static object pool allocator.
 *
 *    See @c pool.h for the public contract, the threading model, and
 *    the MISRA C:2023 / IEC 61508 awareness notes. This translation
 *    unit holds only the internal slot bookkeeping and the find /
 *    acquire / release machinery.
 *
 * @par MISRA C:2023 Deviation Record
 *
 *    Deviations are intentional and have been reviewed. Each site is
 *    also tagged in-line at point-of-use (search for "MISRA <rule>").
 *
 *    Rule 15.5 (advisory) - A function should have a single point of
 *                            exit at its end.
 *      Sites:        pool_init, pool_acquire, pool_release,
 *                    pool_get_pointer, pool_get_pointer_checked,
 *                    pool_find_free_slot, pool_validate_handle,
 *                    pool_validate_id_ptr.
 *      Justification: Early-return-on-error guard clauses are the
 *                    project's idiomatic control flow. Refactoring to
 *                    a single-exit pattern would either deeply nest
 *                    the validation arms (hurting readability) or
 *                    introduce a goto-cleanup (Rule 15.1 deviation),
 *                    trading one advisory deviation for another with
 *                    no readability gain.
 *      Mitigation:   All exit points return one of the documented
 *                    @c pool_status_t codes (or @c NULL for the
 *                    pointer accessor). Every guard-clause path is
 *                    exercised by @c tests/test_pool.c (NULL handle,
 *                    NULL ID pointer, out-of-range ID, not-USED
 *                    slot, full pool, double-free, uninitialised
 *                    garbage state).
 *
 *    Note on defensive bounds checks:
 *      @c pool_release and @c pool_get_pointer each contain an
 *      @c offset > (block_size - POOL_ITEM_SIZE) check after the
 *      @c id >= POOL_MAX_SLOTS guard. cppcheck (correctly) flags the
 *      inner check as redundant because the prior guard proves the
 *      condition is unreachable. The check is kept for IEC 61508
 *      defensive coding posture and is silenced with an inline
 *      @c cppcheck-suppress at the site.
 */

/* ================ INCLUDES ================================================ */

#include "pool.h"

#include <string.h> /* memset */

/* ================ DEFINES ================================================= */

/* ================ STRUCTURES ============================================== */

/**
 * @brief Status flags for individual slots within the pool.
 */
typedef enum {
        POOL_SLOT_FREE = 0U, /**< Slot is available for allocation */
        POOL_SLOT_USED = 1U  /**< Slot is currently allocated      */
} pool_slot_status_t;

/* ================ TYPEDEFS ================================================ */

/* ================ STATIC PROTOTYPES ======================================= */

static bool pool_validate_handle(const pool_handle_t p_pool);
static bool pool_validate_id_ptr(const pool_id_t *const p_id);
static pool_status_t pool_find_free_slot(const struct pool_t *const p_pool,
                                         pool_id_t *const p_index);

/* ================ STATIC VARIABLES ======================================== */

/* ================ MACROS ================================================== */

/* ================ STATIC FUNCTIONS ======================================== */

static bool
pool_validate_handle(const pool_handle_t p_pool)
{
        return (p_pool != NULL);
}

static bool
pool_validate_id_ptr(const pool_id_t *const p_id)
{
        return (p_id != NULL);
}

/**
 * @brief Find a free slot using the configured lookup strategy.
 *
 * @param[in]  p_pool   Pointer to the pool instance.
 * @param[out] p_index  Receives the found slot index on success.
 *
 * @return  @c POOL_OK if a free slot was found, @c POOL_ERR_FULL otherwise.
 */
static pool_status_t
pool_find_free_slot(const struct pool_t *const p_pool, pool_id_t *const p_index)
{
        size_t start_index = 0U;
        size_t current_index = 0U;
        size_t iterations = 0U;
        const size_t max_slots = (size_t)POOL_MAX_SLOTS;

        /*
         * MISRA C:2023 Rule 14.3 (advisory): controlling expression
         * should not be invariant. The lookup strategy is a build-time
         * constant, so the branch is resolved at preprocessing time
         * rather than runtime.
         */
#if POOL_LOOKUP_STRATEGY == POOL_LOOKUP_HASH
        /*
         * Hash / round-robin: resume from the saved next-index cursor
         * so allocations spread across the array over time (useful for
         * NVM-backed pools).
         */
        start_index = (size_t)p_pool->next_index;
        if (start_index >= max_slots) {
                start_index = 0U;
        }
#else
        /*
         * Linear: always start at index 0. Deterministic scan order;
         * the simplest target for verification tools. next_index is
         * unused in this mode.
         */
        start_index = 0U;
#endif

        current_index = start_index;

        while (iterations < max_slots) {
                if (p_pool->slot_status[current_index] == POOL_SLOT_FREE) {
                        *p_index = (pool_id_t)current_index;
                        return POOL_OK;
                }

                current_index++;
                if (current_index >= max_slots) {
                        current_index = 0U;
                }
                iterations++;
        }

        return POOL_ERR_FULL;
}

/* ================ GLOBAL PROTOTYPES ======================================= */

pool_status_t
pool_init(pool_handle_t p_pool)
{
        size_t i;

        if (!pool_validate_handle(p_pool)) {
                return POOL_ERR_NULL_PTR;
        }

        /* Zero the storage block. memset writes pool_byte_t units; on
         * a 16-bit MAU target each unit covers one addressable word. */
        (void)memset(p_pool->storage.bytes, 0x00,
                     sizeof(p_pool->storage.bytes));

        for (i = 0U; i < (size_t)POOL_MAX_SLOTS; i++) {
                p_pool->slot_status[i] = POOL_SLOT_FREE;
        }

        p_pool->next_index = 0U;

        return POOL_OK;
}

pool_status_t
pool_acquire(pool_handle_t p_pool, pool_id_t *const p_id)
{
        pool_id_t free_index;
        pool_status_t status;

        if (!pool_validate_handle(p_pool)) {
                return POOL_ERR_NULL_PTR;
        }

        if (!pool_validate_id_ptr(p_id)) {
                return POOL_ERR_NULL_PTR;
        }

        status = pool_find_free_slot(p_pool, &free_index);

        if (status == POOL_OK) {
                /*
                 * Single-writer contract: see the threading note in
                 * pool.h. Two concurrent writers must serialise.
                 */
                p_pool->slot_status[free_index] = POOL_SLOT_USED;

                /* Advance the next-index cursor for the hash strategy. */
                {
                        pool_id_t next = (pool_id_t)(free_index + 1U);
                        if (next >= (pool_id_t)POOL_MAX_SLOTS) {
                                next = 0U;
                        }
                        p_pool->next_index = next;
                }

                *p_id = free_index;
        }

        return status;
}

pool_status_t
pool_release(pool_handle_t p_pool, const pool_id_t id)
{
        if (!pool_validate_handle(p_pool)) {
                return POOL_ERR_NULL_PTR;
        }

        if (id >= POOL_MAX_SLOTS) {
                return POOL_ERR_INVALID_ID;
        }

        /*
         * Reject anything that is not explicitly USED. This catches
         * both double-free attempts and uninitialised slot_status
         * bytes (e.g. a struct memset to 0xFF prior to pool_init()).
         */
        if (p_pool->slot_status[id] != POOL_SLOT_USED) {
                return POOL_ERR_INVALID_ID;
        }

        /*
         * Clear the slot's payload BEFORE flipping the status flag.
         * If a higher-priority context (e.g. an ISR that obeys the
         * single-writer contract by virtue of priority) acquires this
         * slot after the FREE store, it must not see stale bytes.
         *
         * The volatile qualifier here is for dead-store-elimination
         * prevention (the "secure memset" pattern); it is unrelated to
         * the POOL_ATOMIC qualifier on slot_status[] above, which is
         * for cross-context visibility.
         */
        {
                const size_t block_size = sizeof(p_pool->storage.bytes);
                const size_t offset = ((size_t)id) * ((size_t)POOL_ITEM_SIZE);
                size_t k;

                /*
                 * Defence in depth: the `id >= POOL_MAX_SLOTS` check above
                 * already proves this branch is unreachable, but we keep it
                 * to guarantee `offset + POOL_ITEM_SIZE <= block_size` at the
                 * point of access, in line with IEC 61508 defensive coding.
                 */
                /* cppcheck-suppress arrayIndexOutOfBoundsCond */
                if (offset > (block_size - (size_t)POOL_ITEM_SIZE)) {
                        return POOL_ERR_INVALID_ID;
                }

                volatile pool_byte_t *const p_mem_ptr =
                    /* cppcheck-suppress arrayIndexOutOfBoundsCond */
                    &p_pool->storage.bytes[offset];

                for (k = 0U; k < (size_t)POOL_ITEM_SIZE; k++) {
                        p_mem_ptr[k] = (pool_byte_t)0x00;
                }
        }

        p_pool->slot_status[id] = POOL_SLOT_FREE;

        return POOL_OK;
}

void *
pool_get_pointer(pool_handle_t p_pool, const pool_id_t id)
{
        if (!pool_validate_handle(p_pool)) {
                return NULL;
        }

        if (id >= POOL_MAX_SLOTS) {
                return NULL;
        }

        if (p_pool->slot_status[id] != POOL_SLOT_USED) {
                return NULL;
        }

        const size_t block_size = sizeof(p_pool->storage.bytes);
        const size_t offset = ((size_t)id) * ((size_t)POOL_ITEM_SIZE);

        /*
         * Defence in depth: the `id >= POOL_MAX_SLOTS` check above already
         * proves this branch is unreachable, but we keep it for IEC 61508
         * defensive coding posture.
         */
        /* cppcheck-suppress arrayIndexOutOfBoundsCond */
        if (offset > (block_size - (size_t)POOL_ITEM_SIZE)) {
                return NULL;
        }

        /* cppcheck-suppress arrayIndexOutOfBoundsCond */
        return &p_pool->storage.bytes[offset];
}

pool_status_t
pool_get_pointer_checked(pool_handle_t p_pool, const pool_id_t id,
                         void **const p_ptr)
{
        if (!pool_validate_handle(p_pool)) {
                return POOL_ERR_NULL_PTR;
        }

        if (p_ptr == NULL) {
                return POOL_ERR_NULL_PTR;
        }

        if (id >= POOL_MAX_SLOTS) {
                *p_ptr = NULL;
                return POOL_ERR_INVALID_ID;
        }

        *p_ptr = pool_get_pointer(p_pool, id);
        if (*p_ptr == NULL) {
                return POOL_ERR_INVALID_ID;
        }

        return POOL_OK;
}

pool_footprint_t
pool_footprint(void)
{
        pool_footprint_t footprint;
        const size_t instance_units = sizeof(struct pool_t);

        /*
         * sizeof(struct pool_t) is authoritative: it captures the
         * alignment padding a hand-rolled total would miss. The payload
         * is always <= the instance size, so the overhead subtraction
         * cannot underflow.
         */
        footprint.instance_size_units = instance_units;
        footprint.instance_size_octets = POOL_UNITS_TO_OCTETS(instance_units);
        footprint.payload_units = POOL_PAYLOAD_UNITS;
        footprint.overhead_units = instance_units - POOL_PAYLOAD_UNITS;
        footprint.capacity = (pool_id_t)POOL_MAX_SLOTS;
        footprint.item_size = (uint16_t)POOL_ITEM_SIZE;
        footprint.addr_unit_bits = (uint8_t)POOL_ADDR_UNIT_BITS;

        return footprint;
}
