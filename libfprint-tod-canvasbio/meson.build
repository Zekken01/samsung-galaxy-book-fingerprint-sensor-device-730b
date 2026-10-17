project('libfprint-tod-canvasbio', 'c', 'cpp',
  version: '0.2.0',
  default_options: ['c_std=c11', 'cpp_std=c++17', 'warning_level=2'],
)

cc = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')
m_dep = cc.find_library('m', required: true)
glib_dep = dependency('glib-2.0')
gusb_dep = dependency('gusb')
tod_dep = dependency('libfprint-2-tod-1')

# SIFT matcher (cb-match.cpp). Only core/imgproc/features2d/calib3d are
# used; fall back to the individual libraries when opencv4.pc is absent.
opencv_dep = dependency('opencv4', required: false)
if not opencv_dep.found()
  opencv_libs = []
  foreach m : ['core', 'imgproc', 'features2d', 'flann', 'calib3d']
    opencv_libs += cxx.find_library('opencv_' + m, required: true)
  endforeach
  opencv_dep = declare_dependency(
    include_directories: include_directories('/usr/include/opencv4'),
    dependencies: opencv_libs,
  )
endif

tod_driversdir = tod_dep.get_variable(pkgconfig: 'tod_driversdir')

shared_module('libfprint-tod-canvasbio',
  'canvasbio.c',
  'cb-match.cpp',
  dependencies: [glib_dep, gusb_dep, tod_dep, m_dep, opencv_dep],
  install: true,
  install_dir: tod_driversdir,
)

install_data('60-libfprint-canvasbio.rules',
  install_dir: '/usr/lib/udev/rules.d',
)
