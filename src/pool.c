/**
 * @file pool.c
 * @brief Implementation of the Static Object Pool Allocator
 *
 * This file contains the internal logic for memory management. It adheres to
 * MISRA C guidelines regarding pointer arithmetic, initialization, and error
 * handling.
 */

#include "pool.h"
#include <string.h> /* For memset */

/* -------------------------------------------------------------------------- */
/*                             Internal Definitions                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief Status flags for individual slots within the pool.
 */
typedef enum {
        POOL_SLOT_FREE = 0U, ///< Slot is available for allocation
        POOL_SLOT_USED = 1U  ///< Slot is currently allocated
} pool_slot_status_t;

/**
 * @brief Lookup strategy constants matching configuration macros.
 */
#define POOL_LOOKUP_LINEAR 0U
#define POOL_LOOKUP_HASH   1U

/* -------------------------------------------------------------------------- */
/*                             Internal Structs                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Internal structure for the pool manager.
 *
 * This struct is opaque to users (defined only in .c file). It contains the
 * raw memory buffer and an array of status flags to track allocation state.
 */
struct pool_t {
        uint8_t
            memory_block[POOL_MAX_SLOTS
                         * POOL_ITEM_SIZE];  ///< Static storage for all objects
        uint8_t slot_status[POOL_MAX_SLOTS]; ///< Status flag for each slot
                                             ///< (0=Free, 1=Used)
        uint32_t
            allocation_count; ///< Counter for hash-based distribution logic
};

/* -------------------------------------------------------------------------- */
/*                             Implementation                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief Helper function to validate the pool handle.
 *
 * @param[in] p_pool Pointer to check.
 * @return           true if valid (non-NULL), false otherwise.
 */
static bool
pool_validate_handle(const pool_handle_t p_pool)
{
        return (p_pool != NULL);
}

/**
 * @brief Helper function to validate the ID pointer.
 *
 * @param[in] p_id Pointer to check.
 * @return         true if valid (non-NULL), false otherwise.
 */
static bool
pool_validate_id_ptr(pool_id_t *const p_id)
{
        return (p_id != NULL);
}

/**
 * @brief Finds a free slot index based on the configured lookup strategy.
 *
 * @param[in]  p_pool     Pointer to the pool instance.
 * @param[out] p_index    Pointer to store the found free index.
 * @return               POOL_OK if found, POOL_ERR_FULL if none available.
 */
static pool_status_t
pool_find_free_slot(const struct pool_t *const p_pool, uint8_t *const p_index)
{
        uint16_t start_index = 0U;
        uint16_t current_index = 0U;
        uint16_t iterations = 0U;

        /* Determine starting point based on configuration */
        if (POOL_LOOKUP_STRATEGY == POOL_LOOKUP_HASH) {
                /*
                 * Hash/Round-Robin Strategy: Start searching from the last
                 * allocation count modulo size. This distributes allocations
                 * across the array over time, preventing wear concentration in
                 * specific memory regions (relevant for NVM pools).
                 */
                start_index =
                    (uint16_t)(p_pool->allocation_count % POOL_MAX_SLOTS);
        } else {
                /*
                 * Linear Strategy: Always start from index 0. This is the most
                 * predictable behavior for verification tools, ensuring
                 * deterministic scan order.
                 */
                start_index = 0U;
        }

        current_index = (uint16_t)start_index;

        /* Scan loop with bounds check to prevent infinite loops on full pool */
        while (iterations < POOL_MAX_SLOTS) {
                if (p_pool->slot_status[current_index] == POOL_SLOT_FREE) {
                        *p_index = (uint8_t)current_index;
                        return POOL_OK;
                }

                current_index++;
                if (current_index >= POOL_MAX_SLOTS) {
                        current_index = 0U; /* Wrap around */
                }
                iterations++;
        }

        return POOL_ERR_FULL;
}

pool_status_t
pool_init(pool_handle_t p_pool)
{
        uint16_t i;

        if (!pool_validate_handle(p_pool)) {
                return POOL_ERR_NULL_PTR;
        }

        /*
         * MISRA C:2012 Rule 14.3: Initializers for objects with static storage
         * duration should be constant expressions where possible. However,
         * memset is acceptable here as initialization logic. We zero out memory
         * and status flags explicitly.
         */

        /* Clear the entire memory block to ensure no garbage data exists */
        (void)memset(p_pool->memory_block, 0x00U, sizeof(p_pool->memory_block));

        /* Explicitly set all slots to FREE state */
        for (i = 0U; i < POOL_MAX_SLOTS; i++) {
                p_pool->slot_status[i] = POOL_SLOT_FREE;
        }

        /* Reset allocation counter */
        p_pool->allocation_count = 0U;

        return POOL_OK;
}

pool_status_t
pool_acquire(pool_handle_t p_pool, pool_id_t *const p_id)
{
        uint8_t free_index;
        pool_status_t status;

        if (!pool_validate_handle(p_pool)) {
                return POOL_ERR_NULL_PTR;
        }

        if (!pool_validate_id_ptr(p_id)) {
                return POOL_ERR_NULL_PTR;
        }

        /* Find a free slot using the configured strategy */
        status = pool_find_free_slot(p_pool, &free_index);

        if (status == POOL_OK) {
                /* Mark slot as used before returning ID to prevent race
                   conditions in multi-threaded envs (though this lib is
                   single-threaded by design for safety determinism). */
                p_pool->slot_status[free_index] = POOL_SLOT_USED;

                /* Update counter for hash-based distribution logic */
                p_pool->allocation_count++;

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

        /* Bounds check on ID */
        if (id >= POOL_MAX_SLOTS) {
                return POOL_ERR_INVALID_ID;
        }

        /*
         * Safety Check: Prevent Double Free.
         * If the slot is already FREE, this is an error condition.
         * In a safety-critical system, we must not silently accept invalid
         * operations.
         */
        if (p_pool->slot_status[id] == POOL_SLOT_FREE) {
                return POOL_ERR_INVALID_ID;
        }

        /* Mark slot as free */
        p_pool->slot_status[id] = POOL_SLOT_FREE;

        /*
         * Optional: Clear memory block content for security/safety to prevent
         * information leakage if this pool holds sensitive data.
         * This is a best practice in IEC 61508 safety lifecycle management.
         */
        {
                uint32_t offset = (uint32_t)id * POOL_ITEM_SIZE;
                /* Use volatile to prevent compiler optimization removing the
                 * clear if memory is critical */
                volatile uint8_t *const p_mem_ptr =
                    &p_pool->memory_block[offset];
                for (uint16_t k = 0U; k < POOL_ITEM_SIZE; k++) {
                        p_mem_ptr[k] = 0x00U;
                }
        }

        return POOL_OK;
}

void *
pool_get_pointer(pool_handle_t p_pool, const pool_id_t id)
{
        if (!pool_validate_handle(p_pool)) {
                return NULL;
        }

        /* Bounds check */
        if (id >= POOL_MAX_SLOTS) {
                return NULL;
        }

        /* Calculate address safely using offset arithmetic on uint8_t array */
        uint32_t offset = (uint32_t)id * POOL_ITEM_SIZE;

        /* Return void pointer to the start of the slot's memory block */
        return &p_pool->memory_block[offset];
}
