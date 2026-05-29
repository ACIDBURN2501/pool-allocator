# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [2.1.0] - 2026-05-29

### Added

- `pool_footprint()` and the `pool_footprint_t` descriptor: report the static memory footprint of a pool instance (instance size, usable payload, overhead, capacity, item size, addressable-unit width). Pure and instance-free — allocates nothing, touches no pool, safe from any context. Sizes are given both in native storage units (what `sizeof` yields) and normalised to 8-bit octets, so footprints are comparable across the 8-bit and 16-bit-MAU targets.
- `POOL_PAYLOAD_UNITS` macro: the exact, padding-free payload size (`POOL_MAX_SLOTS * POOL_ITEM_SIZE`) in native storage units.
- `POOL_UNITS_TO_OCTETS()` macro in `pool_platform.h`: normalises a native-storage-unit count to 8-bit octets using `POOL_ADDR_UNIT_BITS` (identity on byte-addressable targets, ×2 on a 16-bit MAU).
- Optional `POOL_RAM_BUDGET_OCTETS` compile-time guard: when defined, the header `_Static_assert`s that one pool instance fits the given octet budget. `sizeof` is a constant expression, so the check costs no code or data.
- `tests/test_pool.c`: footprint self-consistency test (matches `sizeof`, the payload macro, the octet conversion, and the overhead arithmetic). Runs under both the 8-bit and 16-bit-MAU-sim builds.

### Changed

- Adopted the shared primitive-family source style across the library files (`pool.h`, `pool.c`, `pool_platform.h`, `pool_conf.h`): the `/* ====== SECTION ====== */` banner skeleton in the public header and implementation, family file-header blocks (`@copyright` / `@file:` / `@brief`), and a `@defgroup pool_api` grouping. Comment/layout only.

## [2.0.0] - 2026-05-28

### Added

- Platform abstraction header `include/pool_platform.h`:
  - `pool_byte_t` storage unit, auto-selected from `<limits.h>`. Resolves to `uint8_t` on byte-addressable targets and `uint16_t` on word-addressable targets where `CHAR_BIT == 16` (the TI C2000 family, where `uint8_t` is not provided by `<stdint.h>`).
  - `pool_align_t` alignment helper with a `uint64_t` fallback for freestanding toolchains that do not provide `max_align_t`.
  - `POOL_ATOMIC(type)` qualifier macro selecting `_Atomic` or `volatile` based on `POOL_ATOMIC_MODE`.
- Build-time configuration header `include/pool_conf.h`. Holds overridable defaults for `POOL_ITEM_SIZE`, `POOL_MAX_SLOTS`, `POOL_LOOKUP_STRATEGY`, and `POOL_ATOMIC_MODE`. Mirrors the `*_conf.h` convention used by the rest of the primitive library set.
- Auto-generated version header `pool_version.h` from `config/pool_version.h.in` (Meson `configure_file`), installed alongside the public headers as `<pool/pool_version.h>`.
- Configurable atomicity model via `POOL_ATOMIC_MODE`, selectable at the toolchain level or with the Meson `-Datomicity_mode=` option:
  - `c11` (default on hosted targets): `slot_status` entries are `_Atomic uint_least8_t` (`<stdatomic.h>`). Race-free for the single-writer / many-readers contract on multi-core hosts and RTOSes.
  - `volatile`: `volatile`-qualified entries for single-core MCUs whose toolchain ships no `<stdatomic.h>` (e.g. TI C2000), where a naturally-aligned word access is a single, indivisible instruction.
  - `auto` (Meson option default): the header picks `volatile` when `__STDC_NO_ATOMICS__` is defined or `CHAR_BIT > 8`, and `c11` otherwise.
- Documented **single-writer / many-readers** threading contract in the public header.
- `pkg-config` file via `import('pkgconfig')`; downstream Meson and pkg-config consumers can resolve the library by name.
- `meson.override_dependency('pool', pool_dep)` so subproject consumers can resolve it as `dependency('pool')`.
- `CHANGELOG.md` (this file) and `CONTRIBUTING.md`.
- CI workflow expansion: the `test` and `release-build` matrices now cover `OS × lookup_strategy × atomicity_mode`. New jobs run `cppcheck` (warning + style + performance + portability gate), `valgrind memcheck` (leak-check full, errors-for-leak-kinds=all, parameterised across both atomicity modes), `ThreadSanitizer` on the concurrency stress test (c11 mode only), and a `16-bit MAU simulation` job that runs `test_pool_16bit_sim` against the `-DPOOL_SIMULATE_16BIT_MAU` library build. Inline `/* cppcheck-suppress arrayIndexOutOfBoundsCond */` markers are used at the two defensive bounds-checks in `pool.c`.
- `tests/test_pool_concurrency.c`: pthread-based single-writer / many-readers stress test (1 writer × 200k iterations, 4 readers × 200k iterations each). Opt-in TSAN build via `-Dtsan=true`.
- `tests/test_pool.c`: refactored byte-pattern fills to per-element `pool_byte_t` assignment so the same test source compiles and passes against both the 8-bit MAU and 16-bit MAU code paths.
- `pool_sim_lib` / `pool_sim_dep` in `meson.build`: second library build with `-DPOOL_SIMULATE_16BIT_MAU`, mirroring ppack.
- MISRA C:2023 deviation record (Rule 15.5 sites / justification / mitigation) at the head of `src/pool.c`.

### Changed (BREAKING)

- `struct pool_t::slot_status` is now `POOL_ATOMIC(uint_least8_t)` (was plain `uint8_t`). The struct layout therefore changes between `c11` and `volatile` modes. Consumers must rebuild against the new headers, and any code that reached into the struct directly must be updated (the public API has always been the supported interface; this is now enforced by the qualifier).
- The pool's storage backing array is now `pool_byte_t[]` rather than `uint8_t[]`. The public API (`pool_get_pointer` returns `void *`) is unchanged for the common byte-addressable case; on C2000 each logical "byte" consumes one 16-bit word.
- The build pins `c_std=c11`. Previous builds tolerated C99, but the documented requirement was already C11 (the header uses `_Static_assert`).
- `meson_options.txt` gains an `atomicity_mode` option (`auto` / `c11` / `volatile`). Default `auto` preserves prior behaviour on hosted builds and unlocks the cross-build on C2000.
- The build now generates `pool_version.h` and installs all public headers under `<pool/...>`.

### Fixed

- Cross-build for TI C2000 no longer fails with `identifier "uint8_t" is undefined`. The C11 standard requires `uint8_t` to be exactly 8 bits, so on a target with `CHAR_BIT == 16` the type is not provided; the new `pool_byte_t` / `uint_least8_t` choices cover that case.

## [1.1.3] - 2026-04-30

Historical baseline before the platform-abstraction refactor.

### Added

- Hash and linear lookup strategies selectable at compile time via `POOL_LOOKUP_STRATEGY`.
- `pool_get_pointer_checked()` accessor that enforces allocation state and returns a status code.
- `_Static_assert` checks for `POOL_ITEM_SIZE`, `POOL_MAX_SLOTS`, alignment, and overflow.
- Meson options for `POOL_MAX_SLOTS` and `POOL_ITEM_SIZE`.

### Fixed

- ISR-safe release ordering: payload clear runs before the status flag flips to FREE, so a higher-priority context cannot observe stale bytes.
- `pool_get_pointer()` rejects free slots.
- Hash strategy switched to a wrap-free next-index cursor.
- Slot sizing enforced as a multiple of `max_align_t` alignment.
