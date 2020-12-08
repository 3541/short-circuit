project(
  'urs',
  ['c'],
  version: '0.1.0',
  default_options: [
    'c_std=c18',
    'warning_level=2',
    'werror=true',
    'buildtype=debug',
    'b_ndebug=if-release'
  ]
)

c_compiler = meson.get_compiler('c')

add_project_arguments('-D_XOPEN_SOURCE=700', language: 'c')
add_project_arguments('-Dtypeof=__typeof__', language: 'c')
# Required by buffer initializers.
add_project_arguments('-Wno-override-init', language: 'c')

# Extra useful warnings.
extra_c_warnings = [
  'bad-function-cast',
  'disabled-optimization',
  'duplicated-branches',
  'duplicated-cond',
  'float-equal',
  'format-nonliteral',
  'format-security',
  'format=2',
  'implicit',
  'inline',
  'logical-op',
  'missing-declarations',
  'missing-prototypes',
  'nested-externs',
  'null-dereference',
  'shadow',
  'strict-prototypes',
  'undef'
]

foreach warning : extra_c_warnings
  warning = '-W' + warning
  if c_compiler.has_argument(warning)
    add_project_arguments(warning, language: 'c')
  endif
endforeach

if get_option('debug')
  add_project_arguments('-DDEBUG_BUILD', language: 'c')
endif

if get_option('profile')
  add_project_arguments('-DPROFILE', language: 'c')
  add_project_arguments('-DNDEBUG', language: 'c')
endif

liburing = dependency('liburing')

liba3_project = subproject('liba3')
liba3 = liba3_project.get_variable('liba3_dep')

src = files([
             'src/main.c',

             'src/connection.c',
             'src/event.c',
             'src/http/connection.c',
             'src/http/parse.c',
             'src/http/request.c',
             'src/http/response.c',
             'src/http/types.c',
             'src/listen.c',
             'src/socket.c',
             'src/timeout.c',
             'src/uri.c'
           ])

headers = include_directories('src')

executable(
  'urs',
  src,
  dependencies: [liburing, liba3],
  include_directories: headers,
  build_by_default: true
)
