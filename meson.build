project(
  'urs',
  ['c'],
  version: '0.1.0',
  default_options: [
    'warning_level=3',
    'werror=true',
    'buildtype=debug',
    'b_sanitize=address,undefined'
  ]
)

add_project_arguments('-D_GNU_SOURCE', language: 'c')

liburing = dependency('liburing')

src = files([
             'src/main.c',

             'src/buffer.c',
             'src/connection.c',
             'src/event.c',
             'src/http.c',
             'src/log.c',
             'src/socket.c'
           ])

executable(
  'urs',
  src,
  dependencies: liburing,
  build_by_default: true
)
