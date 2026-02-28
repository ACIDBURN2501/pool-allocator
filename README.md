# pool-allocator

[![CI](https://github.com/ACIDBURN2501/pool_allocator/actions/workflows/ci.yml/badge.svg)](https://github.com/ACIDBURN2501/pool_allocator/actions/workflows/ci.yml)

A static object pool allocator in C for safety-critical embedded systems.

## Features

- **No dynamic memory** — fixed-size static arrays; no `malloc` / `free`.
- **Pointer-free internals** — slot tracking uses integer status flags,
  satisfying MISRA C:2012 Rule 18.4 (no pointer arithmetic).
- **Configurable lookup** — switch between linear scan (`O(n)`) and
  hash-based round-robin allocation at compile time with a single macro.
- **Deterministic WCET** — every loop is bounded by a compile-time constant
  (`POOL_MAX_SLOTS`).
- **MISRA C:2012 style** — suitable for use in IEC 61508 environments.

## Installation

### Copy-in (recommended for embedded targets)

Copy two files into your project tree — no build system required:

```
include/pool.h
src/pool.c
```

Place them in the same directory, then include the header:

```c
#include "pool.h"
```

### Meson subproject

Add this repo as a wrap dependency or subproject. The library exposes a
`pool_lib` static library with the correct include path:

```meson
inc = include_directories('path/to/pool/include')

pool_src = files('path/to/pool/src/pool.c')

pool_lib = static_library('pool', pool_src,
  include_directories: inc
)
```

## Quick Start

```c
#include <inttypes.h>
#include <stdio.h>
#include "pool.h"

int main(void)
{
        struct pool_t      pool;
        pool_status_t      status;
        pool_id_t          id;
        uint32_t          *data;

        pool_init(&pool);

        status = pool_acquire(&pool, &id);
        if (status == POOL_OK) {
                data = (uint32_t *)pool_get_pointer(&pool, id);
                *data = 0xDEADBEEF;
        }

        pool_release(&pool, id);

        return 0;
}
```

## Configuration

All macros can be overridden before including the header or passed as
`-D` flags on the compiler command line.

| Macro | Description | Default |
|---|---|---|
| `POOL_ITEM_SIZE` | Size in bytes of each object in the pool. | `64U` |
| `POOL_MAX_SLOTS` | Maximum number of slots/objects in the pool. | `16U` |
| `POOL_LOOKUP_STRATEGY` | `POOL_LOOKUP_LINEAR` or `POOL_LOOKUP_HASH`. | `POOL_LOOKUP_LINEAR` |

### Choosing a lookup strategy

```c
/* Predictable scan order — simplest for formal verification */
#define POOL_LOOKUP_STRATEGY POOL_LOOKUP_LINEAR

/* Round-robin allocation distribution — better wear leveling for NVM pools */
#define POOL_LOOKUP_STRATEGY POOL_LOOKUP_HASH
```

With the Meson build system, set the strategy at configure time:

```sh
meson setup build -Dlookup_strategy=hash
```

## Building

```sh
# Library only (release)
meson setup build --buildtype=release
meson compile -C build

# With unit tests
meson setup build --buildtype=debug -Dbuild_tests=true
meson compile -C build
meson test -C build
```

## Notes

| Topic | Note |
|---|---|
| **Memory** | All storage is static. Verify `POOL_MAX_SLOTS * POOL_ITEM_SIZE` fits your BSS budget. |
| **Thread safety** | Not thread-safe. Protect pool handles with an external mutex. |
| **Double-free protection** | `pool_release()` verifies slot state before freeing; returns `POOL_ERR_INVALID_ID` on double-free attempts. |
| **WCET** | Worst-case allocation scan equals `POOL_MAX_SLOTS` iterations. Bounded and deterministic. |
| **Pointer alignment** | Base storage is aligned to `max_align_t`; ensure `POOL_ITEM_SIZE` is a multiple of your target's strictest alignment requirement so every slot stays aligned. |

For a checked pointer accessor that enforces allocation state, use
`pool_get_pointer_checked()`.
