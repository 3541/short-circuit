project(
  'short-circuit',
  'c',
  version: '0.1.0-alpha',
  default_options: [
    'c_std=c11',
    'cpp_std=c++17',
    'cpp_eh=none',
    'cpp_rtti=false',

    'warning_level=2',
    'buildtype=debug',
    'b_ndebug=if-release',
    'default_library=static',
  ]
)

c = meson.get_compiler('c')

sc_cflags = [
  '-DSC_VERSION="' + meson.project_version() + '"',
  '-Dtypeof=__typeof__',
]

if get_option('profile')
  sc_cflags += '-DPROFILE'
endif

if get_option('trace')
  sc_cflags += '-DA3_LOG_LEVEL=A3_LOG_TRACE'
endif

# NOTE: If this ever goes cross-platform, this will need to support other syntaxes.
sc_sec_cflags_wanted = ['-fstack-protector', '-fstack-clash-protection']
add_project_arguments(c.get_supported_arguments(sc_sec_cflags_wanted), language: 'c')

sc_warnings_wanted = [
    '-Wdisabled-optimization', '-Wduplicated-branches', '-Wduplicated-cond', '-Wfloat-equal',
    '-Wformat-nonliteral', '-Wformat-security', '-Wlogical-op', '-Wmissing-declarations',
    '-Wmissing-include-dirs', '-Wnull-dereference', '-Wpacked', '-Wshadow', '-Wstack-protector',
    '-Wundef', '-Wcast-align', '-Wbad-function-cast', '-Wimplicit', '-Wmissing-prototypes',
    '-Wnested-externs', '-Wstrict-prototypes', '-Werror=incompatible-pointer-types'
]
sc_cflags += c.get_supported_arguments(sc_warnings_wanted)

if (c.get_id() != 'gcc' or not c.version().startswith('9'))
  # -Wconversion is too aggressive on GCC <= 9.
  sc_cflags += c.get_supported_arguments(['-Wconversion'])
endif

a3 = dependency('a3', fallback: ['a3', 'a3_dep'])
a3_hash = dependency('a3_hash', fallback: ['a3', 'a3_hash_dep'])

sc_include = include_directories(['src', 'src/include'])
sc_src = files(
  [
    'src/mime.c',
    'src/route.c',
    'src/uri.c',
    'src/io/io.c',
    'src/io/backend/backend.c',
    'src/io/co/coroutine.c',
    'src/net/connection.c',
    'src/net/listen.c',
    'src/proto/http/connection.c',
    'src/proto/http/handle.c',
    'src/proto/http/headers.c',
    'src/proto/http/parse.c',
    'src/proto/http/response.c',
    'src/proto/http/request.c',
  ]
)
sc_deps = [a3, a3_hash]

backend = get_option('io_backend')

if host_machine.system() == 'linux'
  liburing = dependency('liburing', required: false)
endif

if backend == 'default'
  if host_machine.system() == 'linux' and liburing.found()
    backend = 'uring'
  else
    backend = 'poll'
  endif
endif

if backend == 'uring' and host_machine.system() == 'linux'
  if not liburing.found()
    error('liburing is required for the io_uring backend.')
  endif
  sc_cflags += '-DSC_IO_BACKEND_URING'
  sc_deps += liburing
elif backend == 'poll'
  sc_cflags += '-DSC_IO_BACKEND_POLL'
else
  error('Unsupported IO backend ' + backend + ' on platform '
        + host_machine.system())
endif

if get_option('ci')
  sc_cflags += '-DSC_TEST'
endif

sc_lib = library(
  'sc',
  sc_src,
  include_directories: sc_include,
  dependencies: sc_deps,
  c_args: sc_cflags,
  gnu_symbol_visibility: 'hidden',
  build_by_default: true
)
sc = declare_dependency(
  link_with: sc_lib,
  dependencies: [a3, a3_hash],
  include_directories: include_directories('src/include'),
)
sc_dep = sc

sc_main = executable(
  'sc',
  ['src/main.c'],
  dependencies: sc,
  c_args: sc_cflags,
  build_by_default: true,
)

subdir('test')
