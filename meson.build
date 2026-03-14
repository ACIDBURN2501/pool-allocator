project(
  'so_pool_allocator',
  'c',
  version: '1.1.3',
  license: 'MIT',
  default_options: ['warning_level=3'],
)

build_tests = get_option('build_tests')
lookup_strategy = get_option('lookup_strategy')
pool_max_slots = get_option('pool_max_slots')
pool_item_size = get_option('pool_item_size')

merge_method = {
  'linear': '-DPOOL_LOOKUP_STRATEGY=POOL_LOOKUP_LINEAR',
  'hash': '-DPOOL_LOOKUP_STRATEGY=POOL_LOOKUP_HASH',
}[lookup_strategy]

inc = include_directories('include')

lib_src = files('src/pool.c')

pool_lib = static_library(
  'pool',
  lib_src,
  include_directories: inc,
  c_args: [
    merge_method,
    '-DPOOL_MAX_SLOTS=@0@'.format(pool_max_slots),
    '-DPOOL_ITEM_SIZE=@0@'.format(pool_item_size),
  ],
)

if build_tests
  exe = executable(
    'test_pool',
    'tests/test_pool.c',
    link_with: pool_lib,
    include_directories: inc,
  )

  test('test_pool', exe)
endif
