/**
 * @file pool.h
 * @brief Static Object Pool Allocator for Safety-Critical Systems
 *
 * This module implements a static memory pool allocator designed to comply with
 * MISRA C:2012 and IEC 61508 standards. It prohibits dynamic memory allocation
 * (malloc/free) in favor of pre-allocated static buffers, ensuring
 * deterministic behavior and eliminating fragmentation risks.
 *
 * @copyright MIT License
 */

#ifndef POOL_H
#define POOL_H

/* -------------------------------------------------------------------------- */
/*                                 Includes                                   */
/* -------------------------------------------------------------------------- */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/*                               Configuration                                */
/* -------------------------------------------------------------------------- */

/**
 * @def POOL_ITEM_SIZE
 * @brief The size in bytes of each object managed by the pool.
 *        Must be defined before including this header if not using default.
 */
#ifndef POOL_ITEM_SIZE
#define POOL_ITEM_SIZE 64U
#endif

/**
 * @def POOL_MAX_SLOTS
 * @brief The maximum number of objects the pool can manage simultaneously.
 *        Must be defined before including this header if not using default.
 */
#ifndef POOL_MAX_SLOTS
#define POOL_MAX_SLOTS 16U
#endif

/* -------------------------------------------------------------------------- */
/*                         Lookup Strategy Constants */
/* -------------------------------------------------------------------------- */

/**
 * @brief Lookup strategy constants.
 */
#define POOL_LOOKUP_LINEAR 0U
#define POOL_LOOKUP_HASH   1U

/**
 * @def POOL_LOOKUP_STRATEGY
 * @brief Defines the algorithm used to find a free slot during acquisition.
 *
 * Options:
 *   - POOL_LOOKUP_LINEAR (0): Scans from index 0 upwards. Deterministic, O(N).
 *   - POOL_LOOKUP_HASH   (1): Starts scan based on allocation history modulo N.
 *                             Distributes wear more evenly across memory array.
 */
#ifndef POOL_LOOKUP_STRATEGY
#define POOL_LOOKUP_STRATEGY POOL_LOOKUP_LINEAR
#endif

/* -------------------------------------------------------------------------- */
/*                         Compile-Time Configuration */
/* -------------------------------------------------------------------------- */

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(POOL_ITEM_SIZE > 0U, "POOL_ITEM_SIZE must be > 0");
_Static_assert(POOL_MAX_SLOTS > 0U, "POOL_MAX_SLOTS must be > 0");
_Static_assert(((size_t)POOL_ITEM_SIZE % (size_t)_Alignof(max_align_t)) == 0U,
               "POOL_ITEM_SIZE must be a multiple of max_align_t alignment");
_Static_assert(((size_t)POOL_MAX_SLOTS) <= (SIZE_MAX / (size_t)POOL_ITEM_SIZE),
               "POOL_MAX_SLOTS * POOL_ITEM_SIZE overflows size_t");
_Static_assert(((size_t)POOL_MAX_SLOTS) <= (size_t)UINT16_MAX,
               "POOL_MAX_SLOTS must fit in uint16_t");
#endif

/* -------------------------------------------------------------------------- */
/*                               Type Definitions                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief Unique identifier for a slot within the pool.
 */
typedef uint16_t pool_id_t;

/**
 * @brief Pool manager instance.
 *
 * The pool is intended to be allocated by the user (e.g., as a static or stack
 * variable) and initialized with pool_init(). Do not modify members directly.
 */
typedef union {
        max_align_t align;
        uint8_t bytes[POOL_MAX_SLOTS * POOL_ITEM_SIZE];
} pool_storage_t;

struct pool_t {
        pool_storage_t storage;
        uint8_t slot_status[POOL_MAX_SLOTS];
        pool_id_t next_index;
};

typedef struct pool_t *pool_handle_t;

/**
 * @brief Return codes for pool operations.
 */
typedef enum {
        POOL_OK = 0,            ///< Operation completed successfully
        POOL_ERR_NULL_PTR = -1, ///< A provided pointer argument was NULL
        POOL_ERR_FULL = -2,     ///< No free slots available in the pool
        POOL_ERR_INVALID_ID =
            -3 ///< Attempted to release an invalid or already freed slot
} pool_status_t;

/* -------------------------------------------------------------------------- */
/*                                API Functions                               */
/* -------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the pool manager instance.
 *
 * This function must be called before any other operation on the pool handle.
 * It resets internal counters and marks all slots as free.
 *
 * @param[in]  p_pool     Pointer to the pool structure to initialize. Must not
 * be NULL.
 * @return                 POOL_OK if successful, POOL_ERR_NULL_PTR otherwise.
 */
pool_status_t pool_init(pool_handle_t p_pool);

/**
 * @brief Acquires a free slot from the pool.
 *
 * Allocates one chunk of memory (POOL_ITEM_SIZE bytes) and returns its ID.
 * The first slot is aligned to at least alignof(max_align_t). All slots are
 * equally aligned if POOL_ITEM_SIZE is a multiple of alignof(max_align_t).
 *
 * @param[in]  p_pool     Pointer to the initialized pool handle. Must not be
 * NULL.
 * @param[out] p_id       Pointer to store the ID of the acquired slot. Must not
 * be NULL.
 * @return                 POOL_OK if successful, POOL_ERR_NULL_PTR or
 * POOL_ERR_FULL otherwise.
 */
pool_status_t pool_acquire(pool_handle_t p_pool, pool_id_t *const p_id);

/**
 * @brief Releases a previously acquired slot back to the pool.
 *
 * Marks the specified slot as free for future allocation. This function
 * includes checks to prevent double-free errors by verifying the current status
 * of the slot.
 *
 * @param[in]  p_pool     Pointer to the initialized pool handle. Must not be
 * NULL.
 * @param[in]  id         The ID of the slot to release.
 * @return                 POOL_OK if successful, POOL_ERR_NULL_PTR or
 * POOL_ERR_INVALID_ID otherwise.
 */
pool_status_t pool_release(pool_handle_t p_pool, const pool_id_t id);

/**
 * @brief Retrieves a pointer to the memory block for a specific ID.
 *
 * This function does not change state; it calculates the address based on the
 * provided ID and returns NULL if the slot is out of range or currently free.
 *
 * @param[in]  p_pool     Pointer to the initialized pool handle. Must not be
 * NULL.
 * @param[in]  id         The ID of the slot to access.
 * @return                 Pointer to the start of the memory block, or NULL if
 * ID is out of bounds or the slot is free.
 */
void *pool_get_pointer(pool_handle_t p_pool, const pool_id_t id);

/**
 * @brief Retrieves a pointer to the memory block for a specific ID (checked).
 *
 * Validates that the ID is in range and currently allocated.
 *
 * @param[in]  p_pool     Pointer to the initialized pool handle. Must not be
 * NULL.
 * @param[in]  id         The ID of the slot to access.
 * @param[out] p_ptr      Receives pointer to the start of the memory block.
 * Must not be NULL.
 * @return                 POOL_OK if successful, POOL_ERR_NULL_PTR or
 * POOL_ERR_INVALID_ID otherwise.
 */
pool_status_t pool_get_pointer_checked(pool_handle_t p_pool, const pool_id_t id,
                                       void **const p_ptr);

#ifdef __cplusplus
}
#endif

#endif /* POOL_H */
