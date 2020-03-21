test_include_dirs = include_dirs + [include_directories('..', '.')]
tests = [
  {
    'name' : 'hexdump',
    'is_test' : false,
    'sources' : 'hexdump.c',
    'link_with' : utils_lib,
  },
  {
    'name' : 'test-mjpeg-fallback',
    'sources' : [
      'test-mjpeg-fallback.cpp',
      '../display-info.cpp',
      '../jpeg.cpp',
      '../mjpeg-fallback.cpp',
      '../utils.cpp',
      '../x11-display-info.cpp',
      'spice-catch.hpp',
    ],
    'dependencies' : agent_deps,
  },
  {
    'name' : 'test-stream-port',
    'sources' : [
      'test-stream-port.cpp',
      '../stream-port.cpp',
      'spice-catch.hpp',
    ],
    'dependencies' : spice_common_deps,
  }
]

foreach t : tests
  test_name = t.get('name')
  is_test = t.get('is_test', true)
  exe = executable(test_name,
                   sources : t.get('sources'),
                   include_directories : test_include_dirs,
                   link_with : t.get('link_with', []),
                   dependencies : t.get('dependencies', []),
                   install : false,
                   gnu_symbol_visibility : 'hidden')

  if is_test
    test(test_name, exe)
  endif
endforeach

# test-hexdump.sh as test
test('test-hexdump',
     find_program('test-hexdump.sh'),
     workdir : meson.current_build_dir())
