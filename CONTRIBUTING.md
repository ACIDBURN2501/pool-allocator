# Contributing to pool

pool is a small C library providing a static object pool allocator for
safety-critical systems. It is designed to be safe to drop into deeply
embedded firmware, ISR contexts, and audited code bases.

## Getting started

The same commands CI runs, locally:

```sh
# Configure with tests + sanitisers (CI default)
meson setup build --buildtype=debug -Dbuild_tests=true \
                  -Db_sanitize=address,undefined
meson compile -C build
meson test -C build --verbose

# Volatile-mode host build (covers the TI C2000 atomicity path)
meson setup build_volatile --buildtype=debug -Dbuild_tests=true \
                            -Datomicity_mode=volatile
meson compile -C build_volatile
meson test -C build_volatile --verbose

# TI C2000 cross build (library only) - requires a user-provided
# Meson cross file describing your TI codegen install. Place it
# anywhere Meson searches for cross files, or pass an absolute path
# with --cross-file=.
meson setup build_c2000 --cross-file=ti-c2000.ini -Dbuild_tests=false
meson compile -C build_c2000
```

## Source style

- `.clang-format` is mandatory. Run `clang-format -i` on every modified
  `.c` / `.h` file before submitting.
- 8-space indent, Linux brace style, 80-column limit. Match the existing
  conventions; do not reformat unrelated code.
- The Meson build system is the single source of truth. Update
  `meson.build` / `tests/meson.build` when adding or removing source files.
- No CMake, no Make, no other build systems.

## C language rules

- C11 only (uses `_Static_assert` and, in the default mode, `_Atomic`).
- Use fixed-width types from `<stdint.h>` / `<stdbool.h>`. Never plain
  `int` for fixed-width fields. For byte-like buffers reachable from
  the pool's storage, use `pool_byte_t` (defined in `pool_platform.h`)
  so the code stays portable across 8-bit and 16-bit MAU targets.
- No heap allocation (`malloc`, `free`, VLAs), no recursion, no
  data-dependent loop bounds. All state lives in the caller-owned
  `struct pool_t`.
- Validate pointer arguments at every public-API boundary; return
  `POOL_ERR_NULL_PTR` (or `NULL`) when a required pointer is missing.
- Public functions return `pool_status_t` (or `void *` for the
  pointer accessor). No `errno`, no exceptions.

## Concurrency

The supported contract is **single-writer / many-readers** (see the
concurrency section of `pool.h`):

- One context may call the mutating functions (`pool_init`,
  `pool_acquire`, `pool_release`) on a pool; any number of contexts
  may call the read-only `pool_get_pointer` / `pool_get_pointer_checked`.
- The atomicity modes make _individual_ field accesses well-defined; they
  do **not** make `pool_acquire` or `pool_release` atomic as a whole.
  Two mutating contexts must be serialised by the caller.
- `c11` mode must remain ThreadSanitizer-clean for the serialised-writer
  / many-readers contract. Exercised by `tests/test_pool_concurrency.c`
  and gated in CI under `-Dtsan=true`.
- `volatile` mode is correct only on single-core targets and is
  expected to report races under TSAN on a multi-core host — do not
  "fix" that by changing the algorithm. The TSAN job runs `c11` only.

## MISRA C:2023 awareness

The library is written with MISRA C:2023 in mind (not formally
certified). The one intentional, repository-wide advisory deviation is
Rule 15.5 (single point of exit); guard clauses use early `return` at
API boundaries. If your change introduces a new deviation:

1. Add it to the file header deviation note (rule, site, justification).
2. Add an inline `/* MISRA <rule> */` marker at the deviation site.
3. Justify the deviation in the PR description.

Required-rule deviations face a higher bar than advisory ones. We
currently have a single advisory deviation (Rule 15.5) and want to keep
it that way.

## Tests and coverage

- Add a test for every bug fix and every new feature.
- Tests live in `tests/test_*.c`.
- New code must build and pass in **both** atomicity modes
  (`-Datomicity_mode=c11` and `-Datomicity_mode=volatile`).
- New code must also build under the TI C2000 cross configuration
  (library target only) so the C2000 consumer is not regressed. The
  GitHub Actions runner does not have the TI toolchain, so this is a
  **local pre-submit** check, not a CI gate — run it before opening
  the PR if your change touches `src/` or `include/`.
- Run the host suite with ASan + UBSan before submitting.
- CI also gates on `cppcheck` (warning + style + performance + portability)
  and on `valgrind memcheck` (leak-check full, errors-for-leak-kinds=all,
  built without sanitizers because ASan and Valgrind are incompatible).
  New findings must either be fixed or silenced with a justified inline
  `/* cppcheck-suppress <id> */` marker.

### Running the gates locally

```sh
# Static analysis
cppcheck --enable=warning,style,performance,portability --error-exitcode=1 \
         --inline-suppr --std=c11 \
         --suppress=missingIncludeSystem --suppress=unusedFunction \
         -I include src/ include/

# Memcheck (debug build with NO sanitizers)
meson setup build_mem --buildtype=debug -Dbuild_tests=true
meson compile -C build_mem
meson test -C build_mem --verbose \
  --wrapper='valgrind --error-exitcode=1 --leak-check=full \
             --show-leak-kinds=all --track-origins=yes \
             --errors-for-leak-kinds=all'

# ThreadSanitizer on the concurrency test (c11 mode only)
meson setup build_tsan --buildtype=debug -Dbuild_tests=true \
                       -Dtsan=true -Datomicity_mode=c11
meson compile -C build_tsan
meson test -C build_tsan --verbose

# 16-bit MAU simulation (exercises the C2000 storage path on the host)
meson setup build --buildtype=debug -Dbuild_tests=true
meson compile -C build
meson test -C build test_pool_16bit_sim --verbose
```

## API stability

- `struct pool_t` is caller-owned but opaque by contract: use the API
  functions, never touch fields directly. Its layout may change between
  minor versions (e.g. atomicity qualifiers, platform byte width).
- The public function signatures in `pool.h` are stable across the
  v2.x line.
- Breaking changes go in a new major release and require a deprecation
  note in `CHANGELOG.md`.

## Commits

Use Conventional Commits:

- `feat: ...` new feature
- `fix: ...` bug fix
- `doc: ...` documentation only
- `test: ...` test-only changes
- `chore: ...` build, CI, release work
- `refactor: ...` code change that neither fixes a bug nor adds a feature

Keep the subject under ~70 characters. Use the body to explain _why_ the
change is needed, not _what_ the diff already shows.

## Pull requests

- Open an issue first for non-trivial changes so the design can be agreed
  before implementation.
- Keep PRs focused. One feature or one fix per PR.
- All CI checks must pass: host tests in both atomicity modes, the
  release build matrix, cppcheck, valgrind memcheck (both atomicity
  modes), ThreadSanitizer on the concurrency test, and the 16-bit MAU
  simulation. ASan + UBSan are folded into the host test job. The TI
  C2000 cross-build is local-only.
- Any new MISRA deviation must be flagged in the PR description and
  added to the deviation record in `src/pool.c`.

## When in doubt

Open an issue and discuss before writing code. The library is small
enough that even modest design changes have outsized implications.
