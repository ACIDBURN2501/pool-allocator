# pool-allocator

[![CI](https://github.com/ACIDBURN2501/pool_allocator/actions/workflows/ci.yml/badge.svg)](https://github.com/ACIDBURN2501/pool_allocator/actions/workflows/ci.yml)

A static object pool allocator in C for safety-critical embedded systems.

## Features

- **No dynamic memory** — fixed-size static arrays; no `malloc` / `free`,
  no VLAs, no recursion.
- **Deterministic WCET** — every loop is bounded by a compile-time
  constant (`POOL_MAX_SLOTS`).
- **Configurable lookup** — switch between linear scan (`O(n)`) and
  hash-based round-robin allocation at compile time.
- **ISR-safe single-writer contract** — per-slot status flags are
  `_Atomic uint_least8_t` on hosted targets and `volatile` on
  toolchains without `<stdatomic.h>` (e.g. TI C2000), selected
  automatically.
- **Cross-platform** — auto-detected addressing model: works on any C11
  target with an 8-bit or 16-bit minimum addressable unit. Validated on
  x86_64, with the TI C2000 family as a first-class supported target.
- **MISRA C:2023 / IEC 61508 aware** — written with safety-critical
  environments in mind. Not formally certified; one acknowledged
  advisory deviation (Rule 15.5).
- **Explicit error codes** — every public function returns
  `pool_status_t`; no `errno`, no exceptions.

## Requirements

- A C11-compatible toolchain (uses `_Static_assert`; uses `_Atomic` in
  the default atomicity mode).
- Conformant `<stdint.h>` and `<limits.h>`.

## Installation

### Copy-in (recommended for embedded targets)

Copy four files into your project tree — no build system required:

```
include/pool.h
include/pool_conf.h
include/pool_platform.h
src/pool.c
```

Place the headers on the same include path and compile `pool.c` into
your build. Then include the public header:

```c
#include "pool.h"
```

`pool_conf.h` and `pool_platform.h` are pulled in transitively.

### Meson subproject

Add this repo as a wrap dependency or subproject:

```meson
pool_dep = dependency('pool', fallback: ['pool_allocator', 'pool_dep'])
```

The project also exports `meson.override_dependency('pool', ...)` so
downstream Meson builds resolve the subproject dependency by name.

### As an installed dependency

If the library is installed system-wide:

```c
#include <pool/pool.h>
```

A `pkg-config` file (`pool.pc`) is installed, so downstream builds can
also discover the package as `pool`. The generated version header is
available at `<pool/pool_version.h>` after install.

## Quick Start

```c
#include <inttypes.h>
#include <stdio.h>
#include "pool.h"

int main(void)
{
        struct pool_t   pool;
        pool_status_t   status;
        pool_id_t       id;
        uint32_t       *data;

        if (pool_init(&pool) != POOL_OK) {
                return 1;
        }

        status = pool_acquire(&pool, &id);
        if (status == POOL_OK) {
                data = (uint32_t *)pool_get_pointer(&pool, id);
                *data = 0xDEADBEEFu;
        }

        pool_release(&pool, id);

        return 0;
}
```

## Building

```sh
# Library only (release)
meson setup build --buildtype=release -Dbuild_tests=false
meson compile -C build

# With unit tests
meson setup build --buildtype=debug -Dbuild_tests=true
meson compile -C build
meson test -C build --verbose

# Volatile-mode host build (covers the TI C2000 atomicity path)
meson setup build_volatile --buildtype=debug -Dbuild_tests=true \
                            -Datomicity_mode=volatile
meson compile -C build_volatile && meson test -C build_volatile

# TI C2000 cross build (library only)
meson setup build_c2000 --cross-file=ti-c2000.ini -Dbuild_tests=false
meson compile -C build_c2000
```

## Configuration

All macros may be overridden via compiler flags (`-D...`) or by defining
them before including `pool.h`. The Meson build front-end exposes the
same options.

| Macro | Description | Default |
|---|---|---|
| `POOL_ITEM_SIZE` | Size in bytes of each pool slot. Must be a multiple of `_Alignof(pool_align_t)`; enforced by `_Static_assert`. | `64U` |
| `POOL_MAX_SLOTS` | Maximum number of slots in the pool. Must fit in `uint16_t`. | `16U` |
| `POOL_LOOKUP_STRATEGY` | `POOL_LOOKUP_LINEAR` or `POOL_LOOKUP_HASH`. | `POOL_LOOKUP_LINEAR` |
| `POOL_ATOMIC_MODE` | `POOL_ATOMIC_MODE_C11` or `POOL_ATOMIC_MODE_VOLATILE`. Auto-selected; can be pinned with `-DPOOL_USE_C11_ATOMIC` or `-DPOOL_USE_VOLATILE_ATOMIC`. | auto |
| `POOL_NO_MAX_ALIGN_T` | Define to force `pool_align_t` to the `uint64_t` fallback even on a C11-hosted target. Use when `<stddef.h>` on your toolchain provides a `max_align_t` you do not want to depend on. | undefined |
| `POOL_SIMULATE_16BIT_MAU` | Define on a byte-addressable host to force the 16-bit MAU storage path. For exercising the C2000 layout under host tests. | undefined |

### Meson options

| Option | Type | Default | Effect |
|---|---|---|---|
| `build_tests` | boolean | `false` | Build and run the unit tests |
| `lookup_strategy` | combo | `linear` | Sets `POOL_LOOKUP_STRATEGY` |
| `pool_max_slots` | integer | `16` | Sets `POOL_MAX_SLOTS` |
| `pool_item_size` | integer | `64` | Sets `POOL_ITEM_SIZE` |
| `atomicity_mode` | combo | `auto` | `auto` / `c11` / `volatile`; pins the per-slot status qualifier |

### Choosing a lookup strategy

```c
/* Predictable scan order — simplest for formal verification */
#define POOL_LOOKUP_STRATEGY POOL_LOOKUP_LINEAR

/* Round-robin allocation distribution — better wear leveling for NVM pools */
#define POOL_LOOKUP_STRATEGY POOL_LOOKUP_HASH
```

Or via the Meson option:

```sh
meson setup build -Dlookup_strategy=hash
```

## API Reference

### Lifecycle

```c
pool_status_t pool_init   (pool_handle_t p_pool);
pool_status_t pool_acquire(pool_handle_t p_pool, pool_id_t *const p_id);
pool_status_t pool_release(pool_handle_t p_pool, const pool_id_t id);
```

### Accessors

```c
void          *pool_get_pointer        (pool_handle_t p_pool,
                                        const pool_id_t id);

pool_status_t  pool_get_pointer_checked(pool_handle_t p_pool,
                                        const pool_id_t id,
                                        void **const p_ptr);
```

`pool_get_pointer` returns `NULL` for an out-of-range ID, a free slot,
or a NULL pool. `pool_get_pointer_checked` distinguishes the failure
modes via the returned status code and always writes a defined value to
`*p_ptr` (`NULL` on failure).

### Types

| Type | Description |
|---|---|
| `struct pool_t` | Caller-owned pool instance. Opaque by contract — use the API. |
| `pool_handle_t` | `struct pool_t *`. Type used at every API boundary. |
| `pool_id_t` | `uint16_t`. Unique identifier for a slot. |
| `pool_byte_t` | Storage unit for the raw payload buffer. `uint8_t` on byte-addressable targets, `uint16_t` on `CHAR_BIT == 16` targets (TI C2000). |
| `pool_align_t` | Strictest alignment honoured by the storage union. `max_align_t` when available, `uint64_t` fallback otherwise. |
| `pool_status_t` | Return code (see below). |

### Error codes

| Code | Value | Meaning |
|---|---|---|
| `POOL_OK` | `0` | Operation succeeded |
| `POOL_ERR_NULL_PTR` | `-1` | A required pointer argument was `NULL` |
| `POOL_ERR_FULL` | `-2` | No free slots available in the pool |
| `POOL_ERR_INVALID_ID` | `-3` | ID is out of range or the slot is not currently allocated (also returned for double-free attempts) |

## Platform Support

### Supported architectures

The library works on any toolchain meeting the requirements below.
There are no chip-specific code paths; the addressing model is
auto-detected from the standard library headers.

| Requirement | Notes |
|---|---|
| C11 toolchain | Uses `_Static_assert`, and `_Atomic` in the default atomicity mode. |
| `CHAR_BIT == 8` or `CHAR_BIT == 16` | Detected from `<limits.h>`; other values rejected at compile time. |
| `<stdint.h>` and `<limits.h>` | Standard headers, present on every supported toolchain. |
| `<stdatomic.h>` *(optional)* | Required only for `POOL_ATOMIC_MODE_C11`. Targets without it (e.g. TI C2000 codegen) must use `POOL_ATOMIC_MODE_VOLATILE`. Auto-selected. |

### Validated configurations

| Toolchain | Target | Atomicity | Status |
|---|---|---|---|
| GCC, Clang | x86_64 Linux | `c11` | Host tests |
| GCC, Clang | x86_64 Linux | `volatile` | Host tests (covers the C2000 atomicity path) |
| TI C2000 CGT 25.11 LTS | TI C2000 family (16-bit MAU) | `volatile` (auto) | Library cross-build |

### 16-bit-MAU specifics

On the TI C2000 family `CHAR_BIT == 16` and `uint8_t` is not provided
by `<stdint.h>` (the C11 standard requires `uint8_t` to be exactly 8
bits). The pool handles this via `pool_platform.h`:

- `pool_byte_t` falls back to `uint16_t`. Each logical "byte" of the
  storage block consumes one 16-bit word; the public API
  (`void *` returned by `pool_get_pointer`) is unchanged.
- `slot_status[]` uses `uint_least8_t`, which is guaranteed by the
  standard on every conforming target.
- `POOL_ATOMIC_MODE` defaults to `VOLATILE` because the C2000 codegen
  ships no `<stdatomic.h>`. This is correct on the single-core C28x
  core where naturally-aligned word stores are indivisible.

## MISRA C:2023 / IEC 61508 awareness

The library is written with MISRA C:2023 in mind and is intended for
use in IEC 61508 environments. It is **not** formally certified.

- One intentional, repository-wide advisory deviation: **Rule 15.5**
  (single point of exit). Guard clauses use early `return` at API
  boundaries.
- All state is deterministic: no dynamic memory, no recursion, no
  data-dependent loop bounds.
- Defensive `NULL` checks at every public-API boundary.
- The release path clears slot memory before flipping the status flag.
  The serialised-writer contract already prevents a concurrent acquire
  from racing the release; this ordering is defensive and enforces the
  post-condition that any later acquire of the same slot starts with
  zeroed memory.

The `volatile` qualifier inside `pool_release` is the "secure memset"
pattern (dead-store-elimination prevention); it is unrelated to the
`POOL_ATOMIC` qualifier used for cross-context visibility on
`slot_status[]`.

## Notes

| Topic | Note |
|---|---|
| **Memory** | All storage is static. Verify `POOL_MAX_SLOTS * POOL_ITEM_SIZE` fits your BSS budget. |
| **Threading** | Single-writer / many-readers per pool instance. Two writer contexts that share a pool must be serialised by the caller. |
| **Double-free protection** | `pool_release()` verifies slot state before freeing; returns `POOL_ERR_INVALID_ID` on double-free attempts and on uninitialised slot state. |
| **WCET** | Worst-case allocation scan equals `POOL_MAX_SLOTS` iterations. Bounded and deterministic. |
| **Pointer alignment** | Base storage is aligned to `pool_align_t`; `POOL_ITEM_SIZE` must be a multiple of that alignment (enforced by `_Static_assert`). |
| **Version header** | `pool_version.h` is auto-generated by the Meson build and installed as `<pool/pool_version.h>`. |
