# PerfectSense userspace (Meson)

project(
  'perfectsense',
  'c',
  version: '0.0.19',
  license: 'AGPL-3.0-or-later',
  meson_version: '>=1.1.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'werror=false',
  ],
)

inc = include_directories('lib')
libsystemd = dependency('libsystemd')

libperfectsense = library(
  'perfectsense',
  [
    'lib/perfectsense.c',
    'lib/cpu_power.c',
    'lib/temps.c',
    'lib/fan_aggressor.c',
    'lib/gpu_nvidia.c',
  ],
  include_directories: inc,
  install: true,
  version: meson.project_version(),
  soversion: '0',
)

install_headers('lib/perfectsense.h')

cli_exe = executable(
  'perfectsense',
  'cli/main.c',
  include_directories: inc,
  link_with: libperfectsense,
  install: true,
)

daemon_exe = executable(
  'perfectsensed',
  ['daemon/main.c', 'daemon/fan_aggressor_runtime.c'],
  include_directories: [inc, include_directories('daemon')],
  link_with: libperfectsense,
  dependencies: [libsystemd],
  install: true,
)

install_data(
  [
    'packaging/systemd/perfectsensed.service',
    'packaging/systemd/perfectsensed.path',
  ],
  install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system',
)

install_man('man/perfectsense.1', 'man/perfectsensed.8')

install_data(
  'packaging/modprobe.d/perfectsense.conf',
  install_dir: get_option('prefix') / 'lib' / 'modprobe.d',
)

install_data(
  'packaging/modules-load.d/perfectsense.conf',
  install_dir: get_option('prefix') / 'lib' / 'modules-load.d',
)

pkg = import('pkgconfig')
pkg.generate(
  libperfectsense,
  description: 'PerfectSense sysfs client library',
  filebase: 'perfectsense',
)

summary({
  'version': meson.project_version(),
  'cli': cli_exe.name(),
  'daemon': daemon_exe.name(),
  'lib': libperfectsense.name(),
  'note': 'sysfs + daemon (no D-Bus)',
}, section: 'PerfectSense')
