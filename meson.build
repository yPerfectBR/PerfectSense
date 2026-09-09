# PerfectSense userspace (Meson)

project(
  'perfectsense',
  'c',
  version: '0.0.7',
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
  'lib/perfectsense.c',
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
  'daemon/main.c',
  include_directories: inc,
  link_with: libperfectsense,
  dependencies: [libsystemd],
  install: true,
)

install_data(
  'packaging/systemd/perfectsensed.service',
  install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system',
)

install_data(
  'packaging/dbus/org.perfectsense.Sense1.conf',
  install_dir: get_option('prefix') / 'share' / 'dbus-1' / 'system.d',
)

install_data(
  'packaging/dbus/org.perfectsense.Sense1.xml',
  install_dir: get_option('prefix') / 'share' / 'dbus-1' / 'interfaces',
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
  'note': 'Phase 7 — perfectsensed + D-Bus Sense1',
}, section: 'PerfectSense')
