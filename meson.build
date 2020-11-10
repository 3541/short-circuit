project(
  'urs',
  ['c'],
  version: '0.1.0',
  default_options: [
    'c_std=gnu18',
    'warning_level=2',
    'werror=true',
    'buildtype=debug',
    'b_ndebug=if-release'
  ]
)

c_compiler = meson.get_compiler('c')

add_project_arguments('-D_GNU_SOURCE', language: 'c')
# Required by buffer initializers.
add_project_arguments('-Wno-override-init', language: 'c')

# Extra useful warnings.
extra_warnings = [
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

foreach warning : extra_warnings
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

src = files([
             'src/main.c',

             'src/buffer.c',
             'src/connection.c',
             'src/event.c',
             'src/http_connection.c',
             'src/http_request.c',
             'src/http_response.c',
             'src/http_parse.c',
             'src/listen.c',
             'src/log.c',
             'src/ptr.c',
             'src/socket.c',
             'src/uri.c'
           ])

executable(
  'urs',
  src,
  dependencies: liburing,
  build_by_default: true
)
