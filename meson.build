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

add_project_arguments('-D_GNU_SOURCE', language: 'c')
add_project_arguments('-Wno-override-init', language: 'c')

liburing = dependency('liburing')

src = files([
             'src/main.c',

             'src/buffer.c',
             'src/connection.c',
             'src/event.c',
             'src/http.c',
             'src/http_parse.c',
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
