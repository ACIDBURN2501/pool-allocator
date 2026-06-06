/**
 * SPDX-License-Identifier: MIT
 *
 * @file: pool.h
 *
 * @brief
 *    Static object pool allocator for safety-critical systems.
 */

#ifndef POOL_H_
#define POOL_H_

#ifdef __cplusplus
extern "C" {
#endif

/* ================ INCLUDES ================================================ */

#include "pool_conf.h"
#include "pool_platform.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @defgroup pool_api Pool Allocator Library
 *
 * @brief Fixed-size static object pool for deeply embedded firmware.
 *
 * @details
 *    This module implements a fixed-size object pool. It is designed to
 *    be drop-in safe in deeply embedded and audited firmware: no dynamic
 *    memory, no VLAs, no recursion, no data-dependent loop bounds.
 *
 *    ## MISRA C:2023 / IEC 61508 awareness
 *
 *    The implementation is written with MISRA C:2023 in mind and is
 *    intended to be used in IEC 61508 environments. The codebase is
 *    not formally certified. The one intentional, repository-wide
 *    advisory deviation is Rule 15.5 (single point of exit); guard
 *    clauses use early @c return at API boundaries.
 *
 *    ## Concurrency model and threading contract
 *
 *    The supported contract is single-writer / many-readers per pool
 *    instance:
 *
 *      - SINGLE WRITER: exactly one context (one ISR, one task, or
 *        one control loop) may call the mutating functions
 *        (@c pool_init, @c pool_acquire, @c pool_release) on a given
 *        pool.
 *      - MANY READERS: any number of other contexts may concurrently
 *        call @c pool_get_pointer and @c pool_get_pointer_checked.
 *
 *    The per-slot status flags are qualified with @c POOL_ATOMIC, which
 *    expands to @c _Atomic on C11 hosts and @c volatile on toolchains
 *    that ship no @c <stdatomic.h> (e.g. TI C2000). The qualifier makes
 *    each individual field access well-defined; it does NOT make
 *    @c pool_acquire or @c pool_release atomic as a whole. Two writer
 *    contexts that share a pool MUST be serialised by the caller.
 *
 *    ## Platform support
 *
 *    Works on any C11 toolchain with an 8-bit or 16-bit minimum
 *    addressable unit. On the TI C2000 family (@c CHAR_BIT == 16) the
 *    storage falls back to a @c uint16_t array transparently via
 *    @c pool_platform.h; the public API is unchanged. Build-time
 *    configuration lives in @c pool_conf.h.
 *
 * @{
 */

/* ================ DEFINES ================================================= */

/* ---------------- Derived size constants ---------------------------------- */

/**
 * @brief Total usable payload of one pool, in native storage units.
 *
 * Exact and padding-free: @c POOL_MAX_SLOTS contiguous slots of
 * @c POOL_ITEM_SIZE each. The instance's *total* size (payload + status
 * array + cursor + alignment padding) is given by
 * @c sizeof(struct pool_t); always prefer that for RAM budgeting,
 * because a hand-rolled total would miss implementation-defined padding.
 */
#define POOL_PAYLOAD_UNITS ((size_t)POOL_MAX_SLOTS * (size_t)POOL_ITEM_SIZE)

/* ---------------- Compile-time configuration checks ----------------------- */

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(POOL_ITEM_SIZE > 0U, "POOL_ITEM_SIZE must be > 0");
_Static_assert(POOL_MAX_SLOTS > 0U, "POOL_MAX_SLOTS must be > 0");
_Static_assert(((size_t)POOL_ITEM_SIZE % (size_t)_Alignof(pool_align_t)) == 0U,
               "POOL_ITEM_SIZE must be a multiple of pool_align_t alignment");
_Static_assert(((size_t)POOL_MAX_SLOTS) <= (SIZE_MAX / (size_t)POOL_ITEM_SIZE),
               "POOL_MAX_SLOTS * POOL_ITEM_SIZE overflows size_t");
_Static_assert(((size_t)POOL_MAX_SLOTS) <= (size_t)UINT16_MAX,
               "POOL_MAX_SLOTS must fit in uint16_t");
_Static_assert((POOL_LOOKUP_STRATEGY == POOL_LOOKUP_LINEAR)
                   || (POOL_LOOKUP_STRATEGY == POOL_LOOKUP_HASH),
               "POOL_LOOKUP_STRATEGY must be either LINEAR or HASH");
#endif

/* ================ TYPEDEFS ================================================ */

/**
 * @brief Unique identifier for a slot within the pool.
 */
typedef uint16_t pool_id_t;

/**
 * @brief Opaque handle passed at every public-API boundary.
 */
typedef struct pool_t *pool_handle_t;

/* ================ STRUCTURES ============================================== */

/**
 * @brief Raw storage backing a single pool instance.
 *
 * The union forces the storage block to be aligned at least as
 * strictly as @c pool_align_t, so every slot can hold any standard
 * scalar type when @c POOL_ITEM_SIZE is a multiple of that alignment.
 */
typedef union {
        pool_align_t align;
        pool_byte_t bytes[POOL_MAX_SLOTS * POOL_ITEM_SIZE];
} pool_storage_t;

/**
 * @brief Pool manager instance.
 *
 * The pool is allocated by the caller (e.g. as a static or stack
 * variable) and initialised with @c pool_init. Do not modify members
 * directly; use the public API.
 */
struct pool_t {
        pool_storage_t storage;
        POOL_ATOMIC(uint_least8_t) slot_status[POOL_MAX_SLOTS];
        pool_id_t next_index;
};

/*
 * Optional RAM-budget guard. Define POOL_RAM_BUDGET_OCTETS (via a
 * compiler flag or before including this header) to fail the build when
 * a single pool instance would exceed the budget. The comparison is in
 * octets, so the same number is meaningful on 8-bit and 16-bit MAU
 * targets. sizeof is a constant expression, so this is a pure
 * compile-time check with no code or data cost.
 */
#if defined(POOL_RAM_BUDGET_OCTETS) && defined(__STDC_VERSION__)               \
    && (__STDC_VERSION__ >= 201112L)
_Static_assert(POOL_UNITS_TO_OCTETS(sizeof(struct pool_t))
                   <= (size_t)(POOL_RAM_BUDGET_OCTETS),
               "pool instance exceeds POOL_RAM_BUDGET_OCTETS");
#endif

/**
 * @brief Static memory-footprint descriptor for a pool instance.
 *
 * Every field is a compile-time property of the configured pool type;
 * @c pool_footprint() needs no instance to populate it. Sizes are
 * reported in two units:
 *
 *   - @c *_units    native storage units, i.e. what @c sizeof yields on
 *                   this target (8-bit bytes on byte-addressable cores,
 *                   16-bit words on a 16-bit MAU target such as the TI
 *                   C2000 family).
 *   - @c *_octets   the same quantity normalised to 8-bit octets, so a
 *                   footprint is directly comparable across platforms.
 */
typedef struct {
        size_t instance_size_units;  /**< @c sizeof(struct pool_t)          */
        size_t instance_size_octets; /**< instance size, normalised         */
        size_t payload_units;        /**< usable payload (POOL_PAYLOAD_UNITS) */
        size_t overhead_units;       /**< status array + cursor + padding   */
        pool_id_t capacity;          /**< POOL_MAX_SLOTS                     */
        uint16_t item_size;          /**< POOL_ITEM_SIZE                     */
        uint8_t addr_unit_bits;      /**< POOL_ADDR_UNIT_BITS (8 or 16)      */
} pool_footprint_t;

/**
 * @brief Return codes for pool operations.
 */
typedef enum {
        POOL_OK = 0,             /**< Operation completed successfully       */
        POOL_ERR_NULL_PTR = -1,  /**< A provided pointer argument was NULL   */
        POOL_ERR_FULL = -2,      /**< No free slots available in the pool    */
        POOL_ERR_INVALID_ID = -3 /**< ID is out of range or refers to a
                                    slot that is not currently allocated   */
} pool_status_t;

/* ================ MACROS ================================================== */

/* ================ GLOBAL VARIABLES ======================================== */

/* ================ GLOBAL PROTOTYPES ======================================= */

/**
 * @brief Initialise the pool manager instance.
 *
 * Must be called once before any other operation on the pool. Resets
 * the per-slot status flags to FREE, clears the storage block, and
 * resets the next-index cursor.
 *
 * @param[in] p_pool  Pointer to the pool structure to initialise.
 *                    Must not be NULL.
 *
 * @return  @c POOL_OK on success.
 * @return  @c POOL_ERR_NULL_PTR if @p p_pool is NULL.
 */
pool_status_t pool_init(pool_handle_t p_pool);

/**
 * @brief Acquire a free slot from the pool.
 *
 * Finds a free slot using the strategy configured by
 * @c POOL_LOOKUP_STRATEGY, marks it allocated, and writes its ID to
 * @p p_id. Every slot is aligned to at least @c _Alignof(pool_align_t)
 * when @c POOL_ITEM_SIZE is a multiple of that alignment (enforced by
 * a @c _Static_assert in this header).
 *
 * @note  The find-then-claim sequence is not a single atomic operation.
 *        Two concurrent acquire calls could observe the same slot as
 *        free and both claim it. This is why the threading contract
 *        (see the file header) requires callers to serialise mutators.
 *
 * @param[in]  p_pool  Pointer to the initialised pool. Must not be NULL.
 * @param[out] p_id    Pointer that receives the allocated slot ID.
 *                     Must not be NULL.
 *
 * @return  @c POOL_OK on success.
 * @return  @c POOL_ERR_NULL_PTR if either pointer is NULL.
 * @return  @c POOL_ERR_FULL if no slot is currently free.
 */
pool_status_t pool_acquire(pool_handle_t p_pool, pool_id_t *const p_id);

/**
 * @brief Release a previously acquired slot back to the pool.
 *
 * Verifies that the slot is currently marked USED before clearing it
 * (so double-free attempts are rejected with @c POOL_ERR_INVALID_ID).
 *
 * The slot's memory is cleared to zero before the status flag is
 * flipped back to FREE. The serialised-writer contract above already
 * prevents a concurrent acquire from racing the release; this ordering
 * is defensive and enforces the post-condition that any later acquire
 * of the same slot starts with zeroed memory.
 *
 * @param[in] p_pool  Pointer to the initialised pool. Must not be NULL.
 * @param[in] id      The ID of the slot to release.
 *
 * @return  @c POOL_OK on success.
 * @return  @c POOL_ERR_NULL_PTR if @p p_pool is NULL.
 * @return  @c POOL_ERR_INVALID_ID if @p id is out of range or the slot
 *          is not currently allocated.
 */
pool_status_t pool_release(pool_handle_t p_pool, const pool_id_t id);

/**
 * @brief Retrieve a pointer to the memory block for a specific ID.
 *
 * Does not mutate pool state. Returns @c NULL when @p id is out of
 * range, when the slot is not currently allocated, or when @p p_pool
 * is NULL.
 *
 * @param[in] p_pool  Pointer to the initialised pool. Must not be NULL
 *                    to receive a non-NULL result.
 * @param[in] id      The ID of the slot to access.
 *
 * @return  Pointer to the start of the slot's memory block, or @c NULL
 *          on any of the rejection conditions above.
 */
void *pool_get_pointer(pool_handle_t p_pool, const pool_id_t id);

/**
 * @brief Retrieve a pointer to the memory block for a specific ID
 *        (checked variant).
 *
 * Same checks as @c pool_get_pointer, but returns a status code and
 * always writes a defined value to @p p_ptr (@c NULL on failure).
 *
 * @param[in]  p_pool  Pointer to the initialised pool. Must not be NULL.
 * @param[in]  id      The ID of the slot to access.
 * @param[out] p_ptr   Receives the slot pointer on success, @c NULL on
 *                     failure. Must not be NULL.
 *
 * @return  @c POOL_OK on success.
 * @return  @c POOL_ERR_NULL_PTR if @p p_pool or @p p_ptr is NULL.
 * @return  @c POOL_ERR_INVALID_ID if @p id is out of range or the slot
 *          is not currently allocated.
 */
pool_status_t pool_get_pointer_checked(pool_handle_t p_pool, const pool_id_t id,
                                       void **const p_ptr);

/**
 * @brief Report the static memory footprint of a pool instance.
 *
 * Pure and instance-free: every field is a compile-time property of the
 * configured pool type (@c POOL_MAX_SLOTS, @c POOL_ITEM_SIZE, the
 * target's addressable-unit width, and the layout chosen by the
 * compiler). Does not allocate, does not read or write any pool, and is
 * safe to call from any context.
 *
 * Typical uses are a one-line boot/telemetry log of RAM consumption, or
 * a runtime cross-check against a budget computed elsewhere.
 *
 * @return  A fully-populated @c pool_footprint_t by value.
 */
pool_footprint_t pool_footprint(void);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* POOL_H_ */
