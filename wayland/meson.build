# use meson for the Wayland build at the moment, because all
# the examples use this system. TODO: port to autoconf

project(
	'xscreensaver-wayland',
	'c',
	version: '0.0',
	meson_version: '>=1.0.0',
	default_options: [
		# matches android; note jwxyz.h uses // comments
		'c_std=gnu89',
	],
)

wayland_client = dependency('wayland-client')
GLES = dependency('glesv2')
GL = dependency('gl')
wayland_egl = dependency('wayland-egl')
egl = dependency('egl')
GLU = dependency('glu')
png = dependency('libpng')
gdkpixbuf = dependency('gdk-pixbuf-2.0')
cc = meson.get_compiler('c')
math = cc.find_library('m')

build_flags = [
#     '-std=c99',
    '-Wall',
    '-Wstrict-prototypes',
    '-Wmissing-prototypes',
    '-DSTANDALONE=1',
    '-DHAVE_ANDROID=1',
    '-DHAVE_WAYLAND=1',
    '-DHAVE_GL=1',
    '-DHAVE_JWXYZ=1',
    '-DJWXYZ_GL=1',
    '-DJWXYZ_IMAGE=1',
    '-DHAVE_JWZGLES=1',
    '-DHAVE_XUTF8DRAWSTRING=1',
    '-DHAVE_GLBINDTEXTURE=1',
    '-DHAVE_UNISTD_H=1',
    '-DHAVE_INTTYPES_H=1',
    '-DHAVE_UNAME=1',
    '-DHAVE_UTIL_H=1',
    '-DGETTIMEOFDAY_TWO_ARGS=1',
    '-DHAVE_ICMP=1',
    '-DHAVE_PTHREAD=1',
    '-DHAVE_GLSL=1',
    '-DHAVE_GLES3=1',
    '-DHAVE_GDK_PIXBUF=1',
]

wayland_scanner = find_program('wayland-scanner')

wayland_scanner_code = generator(
	wayland_scanner,
	output: '@BASENAME@-protocol.c',
	arguments: ['private-code', '@INPUT@', '@OUTPUT@'],
)

wayland_scanner_client = generator(
	wayland_scanner,
	output: '@BASENAME@-client-protocol.h',
	arguments: ['client-header', '@INPUT@', '@OUTPUT@'],
)

client_protos_src = []
client_protos_headers = []

client_protocols = [
	['wlr-layer-shell-unstable-v1.xml'],
	['xdg-shell.xml'],
]

foreach p : client_protocols
	xml = join_paths(p)
	client_protos_src += wayland_scanner_code.process(xml)
	client_protos_headers += wayland_scanner_client.process(xml)
endforeach

# TODO: only cover a few hacks
# if we make separate 

xft_files = ['utils/xft.c', 'utils/xftwrap.c']
anim_files = [] #'hacks/recanim.c']

# TODO: make a shim for 'utils/visual.c', 'utils/xmu.c'
hack_1_files = ['hacks/fps.c', 'utils/resources.c',
		'utils/usleep.c', 'utils/yarandom.c', 'utils/utf8wc.c',
		'utils/font-retry.c', 'utils/pow2.c', ] + xft_files + anim_files
color_files = ['utils/hsv.c', 'utils/colors.c']
jwxyz = ['jwxyz/jwxyz-common.c', 'jwxyz/jwxyz-gl.c', 'jwxyz/jwxyz-timers.c']
wayland_files = ['wayland/screenhack.c'] + jwxyz
hack_files = wayland_files + hack_1_files
xlock_files = wayland_files + ['xlockmore.c'] + color_files + hack_1_files



hacks = [
# 	['hyperball', ['hacks/hyperball.c'] + hack_files, []]
	['abstractile', ['hacks/abstractile.c'] + hack_files + color_files, []]
	# the above hacks are reused by others and should be included
]

# 

include_dirs = ['../hacks','../utils', '../jwxyz']

# for now, assume all external libraries are available -- but none which depend on X11
base_deps = [math,wayland_client,wayland_egl,GLES,GL,egl,GLU,png,gdkpixbuf]

foreach hack : hacks
	name = hack[0]
	sources = hack[1]
	deps = hack[2]
	
	mod_sources = []
	foreach f : sources
		mod_sources += '../' + f
	endforeach
	
	exec = executable(
		'xscreensaver-wayland-'+name,
		files(mod_sources) + client_protos_src + client_protos_headers,
# 		link_with: main_lib,
		dependencies: deps + base_deps,
		include_directories: include_dirs,
		c_args: build_flags,
		install : true
	)
endforeach
