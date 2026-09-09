# Placeholder — PerfectSense userspace (Meson)
# Phase 0 stub: targets will be filled as phases land.

project(
  'perfectsense',
  'c',
  version: '0.0.0',
  license: 'AGPL-3.0-or-later',
  meson_version: '>=1.1.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'werror=false',
  ],
)

# Phase 0: minimal CLI stub so Meson has a real target.
cli_exe = executable(
  'perfectsense',
  'cli/main.c',
  install: true,
)

# Future: subdir('lib') / subdir('daemon')

summary({
  'version': meson.project_version(),
  'cli': cli_exe.name(),
  'note': 'Phase 0 stub — no hardware backend yet',
}, section: 'PerfectSense')
