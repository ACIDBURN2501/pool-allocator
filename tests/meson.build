test_exe = executable(
  'test_pool',
  'test_pool.c',
  dependencies: [pool_dep],
)

test('test_pool', test_exe)

# Re-run the same source against the sim library so the test suite
# exercises the pool_byte_t == uint16_t (C2000) code path on a host CI
# runner. The cross-compile catches build-time issues; this catches
# runtime issues that only manifest under 16-bit storage.

test_16bit_exe = executable(
  'test_pool_16bit',
  'test_pool.c',
  dependencies: [pool_sim_dep],
)

test('test_pool_16bit_sim', test_16bit_exe)

# Concurrency stress test - single writer, many readers - exercises the
# documented threading contract. TSAN is opt-in via -Dtsan=true (it is
# incompatible with ASan, so it lives in its own override).

thread_dep = dependency('threads')

concurrency_override = []
if get_option('tsan')
  concurrency_override = ['b_sanitize=thread']
endif

test_concurrency_exe = executable(
  'test_pool_concurrency',
  'test_pool_concurrency.c',
  dependencies: [pool_dep, thread_dep],
  override_options: concurrency_override,
)

test('test_pool_concurrency', test_concurrency_exe, timeout: 60)
