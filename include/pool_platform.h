/**
 * @file pool_platform.h
 * @brief Platform abstraction for the pool allocator library.
 *
 * @details
 *    The pool's storage block is addressed in fixed-size byte units.
 *    This header picks the right storage unit and alignment helper for
 *    the target:
 *
 *      - On byte-addressable targets (x86_64, ARM, AArch64, RISC-V,
 *        AVR, etc.) @c pool_byte_t is @c uint8_t.
 *      - On word-addressable targets where @c CHAR_BIT is 16 (the
 *        Texas Instruments C2000 family is the canonical example),
 *        @c uint8_t is not provided by @c <stdint.h>. @c pool_byte_t
 *        falls back to @c uint16_t; the slot count and item size are
 *        unchanged, the storage simply consumes one 16-bit word per
 *        logical byte.
 *
 *    The header also exposes a small concurrency-qualifier macro:
 *
 *      POOL_ATOMIC(type) - @c _Atomic @c type on C11 targets that ship
 *                          @c <stdatomic.h>; @c volatile @c type when
 *                          atomics are unavailable. The mode is
 *                          selected by @c POOL_ATOMIC_MODE in
 *                          @c pool_conf.h.
 *
 *    The intent matches the debounce primitive: an individual field
 *    load or store is well-defined, but the public API is not atomic
 *    as a whole. Callers sharing a pool across two writer contexts
 *    must serialise with their own mutex / interrupt disable.
 *
 *    Define @c POOL_SIMULATE_16BIT_MAU at compile time on a byte-
 *    addressable host to force the 16-bit storage path for host unit
 *    tests against the C2000 layout. Production builds should leave it
 *    undefined and rely on auto-detection.
 */

#ifndef POOL_PLATFORM_H_
#define POOL_PLATFORM_H_

#include "pool_conf.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/*                       Byte storage unit selection                          */
/* -------------------------------------------------------------------------- */

/*
 * Auto-detect a 16-bit MAU target. C11 requires uint8_t to be exactly
 * 8 bits; on a target with CHAR_BIT==16 the type is therefore not
 * provided. POOL_SIMULATE_16BIT_MAU forces this branch on byte-
 * addressable hosts so the same code path can be exercised under host
 * tests.
 */
#if defined(POOL_SIMULATE_16BIT_MAU) || (CHAR_BIT > 8) || (UCHAR_MAX > 255U)
/** @brief Storage unit used for the pool's raw byte buffer. */
typedef uint16_t pool_byte_t;
/** @brief Bits per minimum addressable unit on this target. */
#define POOL_ADDR_UNIT_BITS 16U
#else
typedef uint8_t pool_byte_t;
#define POOL_ADDR_UNIT_BITS 8U
#endif

/* -------------------------------------------------------------------------- */
/*                       Alignment-helper selection                           */
/* -------------------------------------------------------------------------- */

/*
 * pool_align_t is the strictest scalar alignment the pool needs to
 * honour. Hosted C11 implementations provide max_align_t in <stddef.h>;
 * freestanding toolchains (including the TI C2000 codegen) frequently
 * do not. The uint64_t fallback covers every target the pool currently
 * supports.
 *
 * Define POOL_NO_MAX_ALIGN_T at the toolchain level to force the
 * uint64_t fallback even on a C11-hosted target — useful when
 * <stddef.h> on that toolchain provides a max_align_t that the
 * application explicitly does not want to depend on.
 */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)                 \
    && !defined(POOL_NO_MAX_ALIGN_T)
typedef max_align_t pool_align_t;
#else
typedef uint64_t pool_align_t;
#endif

/* -------------------------------------------------------------------------- */
/*                       Concurrency qualifier macro                          */
/* -------------------------------------------------------------------------- */

#if POOL_ATOMIC_MODE == POOL_ATOMIC_MODE_C11
#include <stdatomic.h>
#define POOL_ATOMIC(type) _Atomic type
#elif POOL_ATOMIC_MODE == POOL_ATOMIC_MODE_VOLATILE
#define POOL_ATOMIC(type) volatile type
#else
#error "Unsupported POOL_ATOMIC_MODE; select C11 or VOLATILE in pool_conf.h"
#endif

/* -------------------------------------------------------------------------- */
/*                       Compile-time platform invariants                     */
/* -------------------------------------------------------------------------- */

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(POOL_ADDR_UNIT_BITS == 8U || POOL_ADDR_UNIT_BITS == 16U,
               "pool allocator only supports 8-bit or 16-bit "
               "addressable units");
#endif

#endif /* POOL_PLATFORM_H_ */
