#
# project definition
#
project('spice-streaming-agent', 'c', 'cpp',
        version : '0.3',
        license : 'LGPLv2.1',
        meson_version : '>= 0.49',
        default_options : [
          'buildtype=debugoptimized',
          'warning_level=2',
          'cpp_std=gnu++11',
        ])

meson.add_dist_script('build-aux/meson-dist', meson.source_root())


#
# check for dependencies
#
spice_protocol_version='0.12.16'


#
# global defines
#
include_dirs = [include_directories('include', '.')]
cc = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')
plugins_dir_relative = get_option('libdir') / meson.project_name() / 'plugins'

global_cflags = [
  '-Wno-unused-parameter'
]
add_project_arguments(cc.get_supported_arguments(global_cflags), language : 'c')
add_project_arguments(cxx.get_supported_arguments(global_cflags), language : 'cpp')

global_link_args = [
  '-Wl,-z,relro',
  '-Wl,-z,now',
  '-Wl,--no-copy-dt-needed-entries',
]
global_link_args = cxx.get_supported_link_arguments(global_link_args)

config_data = configuration_data()


#
# set up subprojects
#
spice_common = subproject('spice-common',
                          default_options : [
                            'generate-code=all',
                            'smartcard=disabled',
                          ])
spice_common_deps = [spice_common.get_variable('spice_common_client_dep')]
config_data.merge_from(spice_common.get_variable('spice_common_config_data'))


#
# check for options
#
compile_gst_plugin = false
gst_deps = []
if not get_option('gst-plugin').disabled()
  deps = ['gstreamer-1.0', 'gstreamer-app-1.0']
  compile_gst_plugin = true
  foreach dep : deps
    dep = dependency(dep, version: '>= 1.10', required : get_option('gst-plugin'))
    compile_gst_plugin = compile_gst_plugin and dep.found()
    gst_deps += dep
  endforeach
endif

compile_tests = false
if not get_option('unittests').disabled()
  has_catch = false
  if not has_catch and cxx.has_header('catch/catch.hpp')
    config_data.set('HAVE_CATCH_CATCH_HPP', '1')
    has_catch = true
  endif
  if not has_catch and cxx.has_header('catch2/catch.hpp')
    config_data.set('HAVE_CATCH2_CATCH_HPP', '1')
    has_catch = true
  endif
  if not has_catch and get_option('unittests').enabled()
    error('Could not find Catch dependency header (catch.hpp)')
  endif
  compile_tests = has_catch
endif


#
# subdirectories
#
subdir('include/spice-streaming-agent')
subdir('src')


#
# write config.h
#
configure_file(output : 'config.h',
               configuration : config_data)


#
# write:
# - spice-streaming-agent.pc
# - spice-streaming-agent.spec
# - spice-streaming.desktop
#
config_data = configuration_data()
config_data.set('PACKAGE', meson.project_name())
config_data.set('VERSION', meson.project_version())
config_data.set('SPICE_PROTOCOL_MIN_VER', spice_protocol_version)
config_data.set('BINDIR', get_option('prefix') / get_option('bindir'))

pkgconfig = import('pkgconfig')
plugins_dir = '${exec_prefix}' / plugins_dir_relative
pkgconfig.generate(name: meson.project_name(),
                   description : 'SPICE streaming agent headers',
                   version : meson.project_version(),
                   requires : 'spice-protocol >= @0@'.format(spice_protocol_version),
                   variables : [
                     'exec_prefix=${prefix}',
                     'plugindir=@0@'.format(plugins_dir),
                   ])

configure_file(input : 'spice-streaming-agent.spec.in',
               output : 'spice-streaming-agent.spec',
               configuration : config_data)

# this will start the program for the login session
desktop = configure_file(input : 'data/spice-streaming.desktop.in',
               output : 'spice-streaming.desktop',
               install_dir : get_option('datadir') / 'gdm/greeter/autostart',
               configuration : config_data)

# this will start the program for each user session
install_data(desktop,
             install_dir : get_option('sysconfdir') / 'xdg/autostart')

install_man('spice-streaming-agent.1')


#
# instal 90-spice-guest-streaming.rules
#
udevrulesdir = get_option('udevrulesdir')
if udevrulesdir == ''
  udevrulesdir = get_option('prefix') / 'lib/udev/rules.d'
endif
install_data('data/90-spice-guest-streaming.rules',
             install_dir : udevrulesdir)
