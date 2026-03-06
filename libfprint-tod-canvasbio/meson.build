project('libfprint-tod-canvasbio', 'c',
  version: '0.1.0',
  default_options: ['c_std=c11', 'warning_level=2'],
)

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required: true)
glib_dep = dependency('glib-2.0')
gusb_dep = dependency('gusb')
tod_dep = dependency('libfprint-2-tod-1')

tod_driversdir = tod_dep.get_variable(pkgconfig: 'tod_driversdir')

c_args = []
if get_option('debug_images')
  c_args += '-DCB_DEBUG_IMAGES'
endif

shared_module('libfprint-tod-canvasbio',
  'canvasbio.c',
  c_args: c_args,
  dependencies: [glib_dep, gusb_dep, tod_dep, m_dep],
  install: true,
  install_dir: tod_driversdir,
)

install_data('60-libfprint-canvasbio.rules',
  install_dir: '/usr/lib/udev/rules.d',
)
