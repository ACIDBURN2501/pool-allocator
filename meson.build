project(
  'pool_allocator',
  'c',
  version: '2.1.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
  ],
  license: 'MIT',
)

project_description = 'Static object pool allocator for safety-critical systems.'

# === Version header ===========================================================

ver = meson.project_version().split('.')

major = ver[0].to_int()
minor = ver[1].to_int()
patch = ver[2].to_int()

conf = configuration_data()
conf.set_quoted('VERSION', meson.project_version())
conf.set('MAJOR', major)
conf.set('MINOR', minor)
conf.set('PATCH', patch)

generated_version_header = configure_file(
  input: 'config' / 'pool_version.h.in',
  output: 'pool_version.h',
  configuration: conf,
  install: true,
  install_dir: get_option('includedir') / 'pool',
)

# === Build-time configuration =================================================

lookup_strategy = get_option('lookup_strategy')
pool_max_slots = get_option('pool_max_slots')
pool_item_size = get_option('pool_item_size')
atomicity = get_option('atomicity_mode')

lookup_define = {
  'linear': '-DPOOL_LOOKUP_STRATEGY=POOL_LOOKUP_LINEAR',
  'hash': '-DPOOL_LOOKUP_STRATEGY=POOL_LOOKUP_HASH',
}[lookup_strategy]

add_project_arguments(
  lookup_define,
  '-DPOOL_MAX_SLOTS=@0@'.format(pool_max_slots),
  '-DPOOL_ITEM_SIZE=@0@'.format(pool_item_size),
  language: 'c',
)

# 'auto' leaves pool_conf.h to its own detection (C11 on hosted, volatile
# where <stdatomic.h> is unavailable). The explicit modes pin the choice.
if atomicity == 'c11'
  add_project_arguments('-DPOOL_USE_C11_ATOMIC', language: 'c')
elif atomicity == 'volatile'
  add_project_arguments('-DPOOL_USE_VOLATILE_ATOMIC', language: 'c')
endif

# === Include directories ======================================================

public_headers = include_directories('include')
generated_headers = include_directories('.')

# === Library ==================================================================

library_sources = ['src/pool.c']

pool_lib = static_library(
  'pool',
  library_sources,
  include_directories: [public_headers, generated_headers],
  install: true,
  install_dir: get_option('libdir'),
)

install_headers(
  ['include/pool.h', 'include/pool_conf.h', 'include/pool_platform.h'],
  subdir: 'pool',
)

pool_dep = declare_dependency(
  include_directories: [public_headers, generated_headers],
  link_with: pool_lib,
  sources: generated_version_header,
)

meson.override_dependency('pool', pool_dep)

# === Sim library: 16-bit MAU code path on a byte-addressable host ============
# Second build of the library with -DPOOL_SIMULATE_16BIT_MAU so a host test
# binary linked against it exercises the pool_byte_t == uint16_t code path
# end-to-end (the same path the TI C2000 cross-build runs but cannot test).

if get_option('build_tests')
  pool_sim_lib = static_library(
    'pool_sim',
    library_sources,
    include_directories: [public_headers, generated_headers],
    c_args: ['-DPOOL_SIMULATE_16BIT_MAU'],
    install: false,
  )

  pool_sim_dep = declare_dependency(
    include_directories: [public_headers, generated_headers],
    link_with: pool_sim_lib,
    compile_args: ['-DPOOL_SIMULATE_16BIT_MAU'],
    sources: generated_version_header,
  )
endif

pkg = import('pkgconfig')
pkg.generate(
  name: 'pool',
  description: project_description,
  filebase: 'pool',
  version: meson.project_version(),
  libraries: pool_lib,
)

# === Tests ====================================================================

if get_option('build_tests')
  subdir('tests')
endif
