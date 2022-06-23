gmock_main = dependency('gmock_main', fallback: ['gtest', 'gmock_main_dep'])

sc_test_src = files(
  [
    'coroutine.cc',
    'io.cc',
    'timeout.cc',
    'uri.cc',
  ]
)

sc_test_args = ['-DGTEST_HAS_EXCEPTIONS=0']
if backend == 'uring'
  sc_test_args += '-DSC_IO_BACKEND_URING'
endif

sc_test = executable(
  'sc_test',
  sc_test_src,
  cpp_args: sc_test_args,
  dependencies: [gmock_main, sc],
  build_by_default: false
)

test('sc_gtest', sc_test, protocol: 'gtest')
