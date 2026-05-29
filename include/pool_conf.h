/**
 * @copyright MIT Licence
 *
 * @file: pool_conf.h
 *
 * @brief
 *    Compile-time configuration for the pool allocator library.
 *
 *    Users may override any option below by defining the macro before
 *    including this header, or via a compiler flag (e.g.
 *    -DPOOL_MAX_SLOTS=64). The #ifndef guards ensure that user-supplied
 *    definitions take precedence.
 *
 *    The Meson build front-end exposes the same options as project
 *    options; see meson_options.txt.
 */

#ifndef POOL_CONF_H_
#define POOL_CONF_H_

#include <limits.h>

/*
 * POOL_ITEM_SIZE
 *   Size in bytes of each object managed by the pool. Must be a
 *   positive multiple of the strictest alignment of the target.
 *   pool.h enforces this with a _Static_assert.
 */
#ifndef POOL_ITEM_SIZE
#define POOL_ITEM_SIZE 64U
#endif

/*
 * POOL_MAX_SLOTS
 *   Maximum number of objects the pool can manage simultaneously.
 *   Bounded above by UINT16_MAX (the storage type of pool_id_t).
 */
#ifndef POOL_MAX_SLOTS
#define POOL_MAX_SLOTS 16U
#endif

/*
 * POOL_LOOKUP_STRATEGY
 *   Selects the algorithm used by pool_acquire() to find a free slot:
 *
 *     POOL_LOOKUP_LINEAR  - Scan from index 0 upwards. Deterministic
 *                           scan order; the simplest target for formal
 *                           verification tools.
 *     POOL_LOOKUP_HASH    - Start the scan at the saved next-index
 *                           cursor. Distributes allocations across the
 *                           array, useful for NVM-backed pools.
 *
 *   Both strategies have the same O(N) worst case bounded by
 *   POOL_MAX_SLOTS.
 */
#define POOL_LOOKUP_LINEAR 0U
#define POOL_LOOKUP_HASH   1U

#ifndef POOL_LOOKUP_STRATEGY
#define POOL_LOOKUP_STRATEGY POOL_LOOKUP_LINEAR
#endif

/*
 * POOL_ATOMIC_MODE
 *   Selection of the concurrency model for the per-slot status flags.
 *
 *   POOL_ATOMIC_MODE_C11:
 *     slot_status entries are _Atomic uint_least8_t (<stdatomic.h>).
 *     Race-free for a single-writer / many-readers contract on
 *     multi-core hosts and RTOSes. Default on hosted targets.
 *
 *   POOL_ATOMIC_MODE_VOLATILE:
 *     slot_status entries are volatile uint_least8_t. Correct only on
 *     single-core targets where a naturally-aligned byte/word store is
 *     a single, indivisible instruction. Required on toolchains that
 *     ship no <stdatomic.h> (e.g. TI C2000 codegen tools).
 *
 *   To select a mode explicitly, define one of these via compiler flag:
 *     -DPOOL_USE_C11_ATOMIC
 *     -DPOOL_USE_VOLATILE_ATOMIC
 *
 *   With no flag, the mode is auto-detected: POOL_ATOMIC_MODE_VOLATILE
 *   if <stdatomic.h> is unavailable (__STDC_NO_ATOMICS__ is set, or
 *   CHAR_BIT > 8 as on the C2000), and POOL_ATOMIC_MODE_C11 otherwise.
 */
#define POOL_ATOMIC_MODE_C11      1
#define POOL_ATOMIC_MODE_VOLATILE 2

#ifndef POOL_ATOMIC_MODE
#if defined(POOL_USE_C11_ATOMIC)
#define POOL_ATOMIC_MODE POOL_ATOMIC_MODE_C11
#elif defined(POOL_USE_VOLATILE_ATOMIC)
#define POOL_ATOMIC_MODE POOL_ATOMIC_MODE_VOLATILE
#elif defined(__STDC_NO_ATOMICS__) || (CHAR_BIT > 8)
#define POOL_ATOMIC_MODE POOL_ATOMIC_MODE_VOLATILE
#else
#define POOL_ATOMIC_MODE POOL_ATOMIC_MODE_C11
#endif
#endif

#endif /* POOL_CONF_H_ */
