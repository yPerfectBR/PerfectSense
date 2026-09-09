# PerfectSense userspace (Meson)

project(
  'perfectsense',
  'c',
  version: '0.0.6',
  license: 'AGPL-3.0-or-later',
  meson_version: '>=1.1.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'werror=false',
  ],
)

inc = include_directories('lib')

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

pkg = import('pkgconfig')
pkg.generate(
  libperfectsense,
  description: 'PerfectSense sysfs client library',
  filebase: 'perfectsense',
)

summary({
  'version': meson.project_version(),
  'cli': cli_exe.name(),
  'lib': libperfectsense.name(),
  'note': 'Phase 6 — RGB KB + lid logo',
}, section: 'PerfectSense')
