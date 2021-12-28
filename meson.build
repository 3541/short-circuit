project(
  'short-circuit',
  ['c', 'cpp'],
  version: '0.1.0-alpha',
  default_options: [
    'c_std=c11',

    'cpp_std=c++14',
    'cpp_rtti=false',
    'cpp_eh=none',

    'warning_level=2',
    'buildtype=debug',
    'b_ndebug=if-release',
    'default_library=static'
  ]
)

# TODO: Library rework and tests.

c = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')

sc_common_flags = [
  '-DSC_VERSION="' + meson.project_version() + '"',
  '-Dtypeof=__typeof__',
  '-D_XOPEN_SOURCE=700'
]
sc_c_flags = []
sc_cxx_flags = []

if get_option('profile')
  sc_common_flags += '-DPROFILE'
endif

# NOTE: If this ever goes cross-platform, this will need to support other syntaxes.
sc_flags_wanted = ['-fstack-protector', '-fstack-clash-protection']
add_project_arguments(c.get_supported_arguments(sc_flags_wanted), language: 'c')
add_project_arguments(cxx.get_supported_arguments(sc_flags_wanted), language: 'cpp')

sc_warnings_wanted = [
    '-Wdisabled-optimization', '-Wduplicated-branches', '-Wduplicated-cond', '-Wfloat-equal',
    '-Wformat-nonliteral', '-Wformat-security', '-Wlogical-op', '-Wmissing-declarations',
    '-Wmissing-include-dirs', '-Wnull-dereference', '-Wpacked', '-Wshadow', '-Wstack-protector',
    '-Wundef', '-Wcast-align', '-Wbad-function-cast', '-Wimplicit', '-Wmissing-prototypes',
    '-Wnested-externs', '-Wstrict-prototypes', '-Wctor-dtor-privacy', '-Wdelete-non-virtual-dtor',
    '-Weffc++', '-Wno-invalid-offsetof'
]

sc_c_flags += c.get_supported_arguments(sc_warnings_wanted)
sc_cxx_flags += cxx.get_supported_arguments(sc_warnings_wanted)

if (c.get_id() != 'gcc' or not c.version().startswith('9')) and (cxx.get_id() != 'gcc' or not cxx.version().startswith('9'))
  # -Wconversion is too aggressive on GCC <= 9.
  sc_c_flags += c.get_supported_arguments(['-Wconversion'])
  sc_cxx_flags += cxx.get_supported_arguments(['-Wconversion'])
endif

sc_include = include_directories('src')
sc_src = files(
  [
    'src/main.c',

    'src/event/init.c',
    'src/event/mod.cc',
    'src/event/handle.cc',
    'src/file.c',
    'src/connection.c',
    'src/http/connection.c',
    'src/http/headers.c',
    'src/http/parse.c',
    'src/http/request.c',
    'src/http/response.c',
    'src/http/types.c',
    'src/listen.c',
    'src/timeout.c',
    'src/uri.c'
  ]
)

liburing = dependency('liburing')
a3 = dependency('a3', fallback: ['a3', 'a3_dep'])
a3_hash = dependency('a3_hash', fallback: ['a3', 'a3_hash_dep'])

sc = executable(
  'sc',
  sc_src,
  include_directories: sc_include,
  dependencies: [liburing, a3, a3_hash],
  c_args: sc_c_flags + sc_common_flags,
  cpp_args: sc_cxx_flags + sc_common_flags,
  gnu_symbol_visibility: 'hidden',
  build_by_default: true
)
