project('gst-llama', 'c',
  version : '1.0.0',
  meson_version : '>= 0.59',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')

cc = meson.get_compiler('c')

gst_version = meson.project_version()

api_version = '1.0'

gst_req = '>= 1.20.0'

gst_dep = dependency('gstreamer-1.0', version : gst_req,
  required : true,
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_base_dep'])
json_glib_dep = dependency('json-glib-1.0', version : '>= 1.0',
  required : true)

# llama_simple dependency
llama_simple_lib = cc.find_library('llama_simple',
  dirs : [join_paths(meson.source_root(), '..', 'ffi', 'build')],
  required : false)

if not llama_simple_lib.found()
  # Fall back to building it ourselves
  message('llama_simple not found, will need to build it first')
  llama_simple_lib = declare_dependency()
endif

plugin_c_args = ['-DHAVE_CONFIG_H']

cdata = configuration_data()
cdata.set_quoted('PACKAGE_VERSION', gst_version)
cdata.set_quoted('PACKAGE', 'gst-llama')
cdata.set_quoted('GST_LICENSE', 'LGPL')
cdata.set_quoted('GST_API_VERSION', api_version)
cdata.set_quoted('GST_PACKAGE_NAME', 'GStreamer llama.cpp Plugin')
cdata.set_quoted('GST_PACKAGE_ORIGIN', 'https://github.com/ggml-org/llama.cpp')
configure_file(output : 'config.h', configuration : cdata)

# Plugin source files
plugin_sources = [
  'gstllama.c',
]

gstllama = library('gstllama',
  plugin_sources,
  c_args: plugin_c_args,
  include_directories : [include_directories('.'), include_directories('../ffi')],
  dependencies : [gst_dep, gstbase_dep, json_glib_dep, llama_simple_lib],
  install : true,
  install_dir : plugins_install_dir,
)

# Tests
if get_option('tests').enabled()
  subdir('tests')
endif
