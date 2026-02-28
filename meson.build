project('so_pool_allocator', 'c',
  version: '1.0.0',
  license: 'MIT',
  default_options: ['warning_level=3']
)

build_tests = get_option('build_tests')
lookup_strategy = get_option('lookup_strategy')

merge_method = {
  'linear': '-DPOOL_LOOKUP_STRATEGY=POOL_LOOKUP_LINEAR',
  'hash': '-DPOOL_LOOKUP_STRATEGY=POOL_LOOKUP_HASH'
}[lookup_strategy]

inc = include_directories('include')

lib_src = files(
  'src/pool.c'
)

pool_lib = static_library('pool', lib_src,
  include_directories: inc,
  c_args: [merge_method]
)

if build_tests
  exe = executable('test_pool',
    'tests/test_pool.c',
    link_with: pool_lib,
    include_directories: inc
  )

  test('test_pool', exe)
endif