project(
  'short-circuit',
  ['cpp'],
  version: '0.1.0-alpha',
  default_options: [
    'cpp_std=c++20',
    'cpp_rtti=false',
    'cpp_eh=none',

    'warning_level=2',
    'buildtype=debug',
    'b_ndebug=if-release',
    'default_library=static'
  ]
)

# TODO: Library rework and tests.

cxx = meson.get_compiler('cpp')

sc_common_flags = [
  '-DSC_VERSION="' + meson.project_version() + '"',
  '-DSC_BACKEND_URING'
]
sc_cxx_flags = []

if get_option('profile')
  sc_common_flags += '-DPROFILE'
endif

# NOTE: If this ever goes cross-platform, this will need to support other syntaxes.
sc_flags_wanted = ['-fstack-protector', '-fstack-clash-protection']
add_project_arguments(cxx.get_supported_arguments(sc_flags_wanted), language: 'cpp')
if cxx.get_id() == 'clang'
  if not cxx.has_argument('-stdlib=libc++')
    error('-stdlib support is required when building with clang.')
  endif
  add_global_arguments('-stdlib=libc++', language: 'cpp')
endif

coroutine_flag = cxx.first_supported_argument(['-fcoroutines', '-fcoroutines-ts'])
if coroutine_flag.length() == 0
  error('Coroutine support is required.')
endif
add_project_arguments(coroutine_flag, language: 'cpp')

sc_warnings_wanted = [
    '-Wdisabled-optimization', '-Wduplicated-branches', '-Wduplicated-cond', '-Wfloat-equal',
    '-Wformat-nonliteral', '-Wformat-security', '-Wlogical-op', '-Wmissing-declarations',
    '-Wmissing-include-dirs', '-Wnull-dereference', '-Wpacked', '-Wshadow', '-Wstack-protector',
    '-Wundef', '-Wcast-align', '-Wbad-function-cast', '-Wimplicit', '-Wmissing-prototypes',
    '-Wnested-externs', '-Wstrict-prototypes', '-Wctor-dtor-privacy', '-Wdelete-non-virtual-dtor',
    '-Wno-c99-designator', '-Wno-coroutine-missing-unhandled-exception'
]

sc_cxx_flags += cxx.get_supported_arguments(sc_warnings_wanted)

if (cxx.get_id() != 'gcc' or not cxx.version().startswith('9'))
  # -Wconversion is too aggressive on GCC <= 9.
  sc_cxx_flags += cxx.get_supported_arguments(['-Wconversion'])
endif

sc_include = include_directories('src')
sc_src = files(
  [
    'src/main.cc',
    'src/event/loop.cc',
    'src/event/backend/backend.cc'
  ]
)

liburing = dependency('liburing')
a3 = dependency('a3', fallback: ['a3', 'a3_dep'])

sc = executable(
  'sc',
  sc_src,
  include_directories: sc_include,
  dependencies: [liburing, a3],
  cpp_args: sc_cxx_flags + sc_common_flags,
  gnu_symbol_visibility: 'hidden',
  build_by_default: true
)
