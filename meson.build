project(
  'skippy-xd', 'c',
  version: files('version.txt'),
  default_options: [
    'buildtype=debugoptimized',
    'b_ndebug=if-release',
    'optimization=3',
  ],
)

skippy_sources = [
  'src/aabb.c',
  'src/clientwin.c',
  'src/config.c',
  'src/dlist.c',
  'src/focus.c',
  'src/img-xlib.c',
  'src/img.c',
  'src/layout.c',
  'src/mainwin.c',
  'src/skippy.c',
  'src/tooltip.c',
  'src/wm.c',
]

# dependencies
cc = meson.get_compiler('c')
m_dep = cc.find_library('m')

x11_dep = dependency('x11')
xcomposite_dep = dependency('xcomposite')
xdamage_dep = dependency('xdamage')
xext_dep = dependency('xext')
xfixes_dep = dependency('xfixes')
xft_dep = dependency('xft')
xrender_dep = dependency('xrender')

skippy_deps = [
  m_dep,
  x11_dep,
  xcomposite_dep,
  xdamage_dep,
  xext_dep,
  xfixes_dep,
  xft_dep,
  xrender_dep,
]

# optional
giflib_dep = cc.find_library('libgif', required: get_option('skippy_gif'))
if giflib_dep.found()
  add_project_arguments('-DCFG_GIFLIB', language: 'c')
  add_project_dependencies(giflib_dep, language: 'c')
  skippy_sources += ['src/img-gif.c']
endif

libjpeg_dep = dependency('libjpeg', required: get_option('skippy_jpeg'))
if libjpeg_dep.found()
  add_project_arguments('-DCFG_JPEG', language: 'c')
  add_project_dependencies(libjpeg_dep, language: 'c')
  skippy_sources += ['src/img-jpeg.c']
endif

libpng_dep = dependency('libpng', required: get_option('skippy_png'))
zlib_dep = dependency('zlib', required: get_option('skippy_png'))
if libpng_dep.found() and zlib_dep.found()
  add_project_arguments('-DCFG_LIBPNG', language: 'c')
  add_project_dependencies([libpng_dep, zlib_dep], language: 'c')
  skippy_sources += ['src/img-png.c']
endif

xrandr_dep = dependency('xrandr', version: '>=1.5', required: get_option('skippy_xrandr'))
if xrandr_dep.found()
  add_project_arguments('-DCFG_XRANDR', language: 'c')
  add_project_dependencies(xrandr_dep, language: 'c')
endif

xinerama_dep = dependency('xinerama', required: get_option('skippy_xinerama'))
if xinerama_dep.found()
  add_project_arguments('-DCFG_XINERAMA', language: 'c')
  add_project_dependencies(xinerama_dep, language: 'c')
endif

chipmunk_dep = disabler()
chipmunk_opt = get_option('skippy_chipmunk')
chipmunk_lib = cc.find_library('chipmunk', required: false)
if chipmunk_lib.found() and chipmunk_opt.allowed()
  chipmunk_dep = declare_dependency(dependencies: chipmunk_lib)
elif chipmunk_opt.enabled()
  cmake = import('cmake')
  chipmunk_cmake_opts = cmake.subproject_options()
  chipmunk_cmake_opts.set_install(false)
  chipmunk_cmake_opts.add_cmake_defines({
    'BUILD_DEMOS': 'OFF',
    'BUILD_SHARED': 'OFF',
    'CMAKE_C_FLAGS': '-DCHIPMUNK_FFI',
  })
  chipmunk_cmake_proj = cmake.subproject('chipmunk2d', options: chipmunk_cmake_opts)
  chipmunk_cmake_dep = chipmunk_cmake_proj.dependency('chipmunk_static')
  meson.override_dependency('chipmunk', chipmunk_cmake_dep)
  chipmunk_dep = dependency('chipmunk',
    allow_fallback: true,
    required: chipmunk_opt,
  )
endif

if chipmunk_dep.found()
  skippy_deps += [chipmunk_dep]
  add_project_arguments('-DCFG_CHIPMUNK', language: 'c')
endif

executable(
  meson.project_name(),
  sources: skippy_sources,
  dependencies: skippy_deps,
  c_args: [ '-DSKIPPYXD_VERSION="' + meson.project_version() + '"' ],
  install: true,
)

install_data(
  sources: ['skippy-xd.rc'],
  rename: ['skippy-xd.rc'],
  install_dir: get_option('sysconfdir') / 'xdg',
)

configure_file(
  input: 'skippy-xd.1.in',
  output: 'skippy-xd.1',
  configuration: {
    'VERSION': meson.project_version(),
  },
  install: true,
  install_dir: get_option('mandir') / 'man1',
)
