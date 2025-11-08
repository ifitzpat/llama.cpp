# Tests

env = environment()
env.set('GST_PLUGIN_PATH_1_0', meson.current_build_dir() / '..')
env.set('GST_REGISTRY', meson.current_build_dir() / 'registry.bin')

test_plugin = executable('test_plugin',
  'test_plugin.c',
  dependencies : [gst_dep],
  include_directories : include_directories('..'),
)

test('plugin_registration', test_plugin,
  env : env,
  depends : [gstllama])
