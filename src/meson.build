#
# \copyright
# Copyright 2016-2017 Red Hat Inc. All rights reserved.
#

plugins_dir = get_option('prefix') / plugins_dir_relative


utils_sources = [
  'hexdump.c',
  'hexdump.h',
]
utils_lib = static_library('streaming-utils', utils_sources,
                           install : false,
                           include_directories : include_dirs)


agent_sources = [
  'spice-streaming-agent.cpp',
  'concrete-agent.cpp',
  'concrete-agent.hpp',
  'cursor-updater.cpp',
  'cursor-updater.hpp',
  'display-info.cpp',
  'frame-log.cpp',
  'frame-log.hpp',
  'mjpeg-fallback.cpp',
  'mjpeg-fallback.hpp',
  'jpeg.cpp',
  'jpeg.hpp',
  'stream-port.cpp',
  'stream-port.hpp',
  'utils.cpp',
  'utils.hpp',
  'x11-display-info.cpp',
]
agent_cpp_args = [
  '-DSPICE_STREAMING_AGENT_PROGRAM',
  '-DPLUGINSDIR="@0@"'.format(plugins_dir),
]
agent_link_args = global_link_args
agent_deps = spice_common_deps
foreach dep : ['libjpeg', 'libdrm', 'x11', 'xfixes', 'xrandr']
  agent_deps += dependency(dep)
endforeach
agent_deps += cc.find_library('dl', required : false)

executable('spice-streaming-agent', agent_sources,
           cpp_args : agent_cpp_args,
           include_directories : include_dirs,
           install : true,
           link_args : agent_link_args,
           export_dynamic : true,
           link_with : utils_lib,
           dependencies : [agent_deps],
           gnu_symbol_visibility : 'inlineshidden',
           pie : true)


if compile_gst_plugin
  gst_plugin_sources = [
    'gst-plugin.cpp',
  ]
  gst_plugin_cpp_args = []
  gst_plugin_link_args = global_link_args
  gst_plugin_deps = spice_common_deps + gst_deps

  shared_module('gst-plugin', gst_plugin_sources,
                name_prefix : '',
                cpp_args : gst_plugin_cpp_args,
                include_directories : include_dirs,
                install : true,
                install_dir : plugins_dir,
                link_args : gst_plugin_link_args,
                dependencies : [gst_plugin_deps],
                gnu_symbol_visibility : 'inlineshidden')
endif

if compile_tests
  subdir('unittests')
endif
