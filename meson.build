project(
  'skippy-xd', 'c',
  version: '2024.12.26'
)

cc = meson.get_compiler('c')
giflib_dep = cc.find_library('libgif')
m_dep = cc.find_library('m')

libjpeg_dep = dependency('libjpeg')
libpng_dep = dependency('libpng')
x11_dep = dependency('x11')
xcomposite_dep = dependency('xcomposite')
xdamage_dep = dependency('xdamage')
xext_dep = dependency('xext')
xfixes_dep = dependency('xfixes')
xft_dep = dependency('xft')
xinerama_dep = dependency('xinerama')
xrender_dep = dependency('xrender')
zlib_dep = dependency('zlib')

executable(
  meson.project_name(),
  sources: [
    'src/clientwin.c',
    'src/config.c',
    'src/dlist.c',
    'src/focus.c',
    'src/img-gif.c',
    'src/img-jpeg.c',
    'src/img-png.c',
    'src/img-xlib.c',
    'src/img.c',
    'src/layout.c',
    'src/mainwin.c',
    'src/skippy.c',
    'src/tooltip.c',
    'src/wm.c',
  ],
  dependencies: [
    giflib_dep,
    m_dep,
    libjpeg_dep,
    libpng_dep,
    x11_dep,
    xcomposite_dep,
    xdamage_dep,
    xext_dep,
    xfixes_dep,
    xft_dep,
    xinerama_dep,
    xrender_dep,
    zlib_dep,
  ],
  c_args: [
    '-DSKIPPYXD_VERSION="' + meson.project_version() + '"',
    '-DCFG_GIFLIB',
    '-DCFG_JPEG',
    '-DCFG_LIBPNG',
    '-DCFG_XINERAMA',
  ],
  install: true,
)

install_data(
  sources: ['skippy-xd.sample.rc'],
  rename: ['skippy-xd.rc'],
  install_dir: get_option('sysconfdir') / 'xdg',
)
