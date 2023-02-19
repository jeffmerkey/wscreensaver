# use meson for the Wayland build at the moment, because all
# the examples use this system.

# TODO: port to autoconf/Makefile.in

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
gio = dependency('gio-2.0')
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
#     '-DHAVE_EGL=1', # added -- no EGL, using JWXYZ
    '-DUSE_GL=1', # added
    '-DHAVE_JWXYZ=1',
    '-DJWXYZ_GL=1',
    '-DJWXYZ_IMAGE=1',
#     '-DHAVE_JWZGLES=1',
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

# TODO: generate these only _ONCE_
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

lib = [
        'utils/aligned_malloc.c',
#         'utils/alpha.c',
        'utils/erase.c',
        'utils/hsv.c',
        'utils/colors.c',
        'utils/grabclient.c',
        'utils/xft.c',
        'utils/xftwrap.c',
        'utils/resources.c',
        'utils/usleep.c',
        'utils/yarandom.c',
        'utils/utf8wc.c',
        'utils/font-retry.c',
        'utils/pow2.c',
        'utils/spline.c',
        'utils/xshm.c',
#         'utils/xdbe.c',
#         'utils/textclient.c',
        'utils/thread_util.c',
        'utils/colorbars.c',
        'jwxyz/jwxyz-common.c',
        'jwxyz/jwxyz-gl.c',
        'jwxyz/jwxyz-timers.c',
        'wayland/screenhack.c',
        'hacks/xlockmore.c',
        'hacks/ximage-loader.c',
        'hacks/apple2.c',
        'hacks/analogtv.c',
        'hacks/fps.c',
        'hacks/glx/xlock-gl-utils.c',
        'hacks/glx/erase-gl.c',
        'hacks/glx/rotator.c',
        'hacks/glx/trackball.c',
        'hacks/glx/gltrackball.c',
        'hacks/glx/normals.c',
        'hacks/glx/involute.c',
        'hacks/glx/texfont.c',
        'hacks/glx/fps-gl.c',
        'hacks/glx/glsl-utils.c',
        'hacks/glx/pipeobjs.c',
        'hacks/glx/sphere.c',
        'hacks/glx/teapot.c',
        'hacks/glx/buildlwo.c',
        'hacks/glx/grab-ximage.c',
]
mod_lib = []
foreach f : lib
  mod_lib += '../' + f
endforeach

# for now, assume all external libraries are available -- but none which depend on X11
base_deps = [math,wayland_client,wayland_egl,GLES,GL,egl,GLU,png,gdkpixbuf,gio]
include_dirs = ['../hacks','../utils', '../jwxyz']

lib = static_library('common',
	mod_lib + client_protos_src + client_protos_headers,
	dependencies: base_deps,
	include_directories: include_dirs,
	c_args: build_flags
)

xft = ['utils/xft.c', 'utils/xftwrap.c']
anim = [] #'hacks/recanim.c']

# TODO: make a shim for 'utils/visual.c', 'utils/xmu.c', 'utils/alpha.c'
hack_1 = ['hacks/fps.c', 'utils/resources.c',
		'utils/usleep.c', 'utils/yarandom.c', 'utils/utf8wc.c',
		'utils/font-retry.c', 'utils/pow2.c', ] + xft + anim
col = ['utils/hsv.c', 'utils/colors.c']
hsv = ['utils/hsv.c']
bar = ['utils/colorbars.c']
# dbe = ['utils/xdbe.c']
erase = ['utils/erase.c']
jwxyz = ['jwxyz/jwxyz-common.c', 'jwxyz/jwxyz-gl.c', 'jwxyz/jwxyz-timers.c']
wayland = ['wayland/screenhack.c'] + jwxyz
hack = wayland + hack_1
png = ['hacks/ximage-loader.c']

# also skips 
fps = ['hacks/glx/texfont.c','hacks/fps.c', 'hacks/glx/fps-gl.c']
glsl = ['hacks/glx/glsl-utils.c']
glhack = wayland + ['hacks/xlockmore.c','hacks/glx/xlock-gl-utils.c','hacks/glx/erase-gl.c'] + hack_1 + fps + glsl + col

xlock = wayland + ['hacks/xlockmore.c'] + col + hack_1 + erase + fps + glsl + ['hacks/glx/xlock-gl-utils.c']


track = ['hacks/glx/rotator.c','hacks/glx/trackball.c','hacks/glx/gltrackball.c']
gears = ['hacks/glx/normals.c','hacks/glx/involute.c'] + track
gears_1 = ['hacks/glx/normals.c','hacks/glx/involute.c']

pipe = ['hacks/glx/pipeobjs.c','hacks/glx/sphere.c','hacks/glx/teapot.c','hacks/glx/normals.c','hacks/glx/buildlwo.c']
spl = ['utils/spline.c']
grab = ['utils/grabclient.c']
shm = ['utils/xshm.c','utils/aligned_malloc.c']
glgrab = ['utils/grabclient.c','hacks/glx/grab-ximage.c'] + shm
# text = ['utils/textclient.c']
alp = [] # needs non-X11 replacement for 'utils/alpha.c'
thro = ['utils/thread_util.c']
atv = ['hacks/analogtv.c'] + shm + thro
apple2 = ['hacks/apple2.c'] + atv

# name, specific files, files from object library
hacks = [
	# X hacks, see hacks/Makefile.in
	['attraction', ['hacks/attraction.c'], hack + col + spl],
	['binaryring', ['hacks/binaryring.c'], hack + col],
	['blitspin', ['hacks/blitspin.c'], hack + grab + png],
	['bubbles', ['hacks/bubbles.c', 'hacks/bubbles-default.c'], hack + png],
	['decayscreen', ['hacks/decayscreen.c'], hack + grab],
	['deco', ['hacks/deco.c'], hack + col],
	['flame', ['hacks/flame.c'], hack + col],
	['greynetic', ['hacks/greynetic.c'], hack],
	['halo', ['hacks/halo.c'], hack + col],
	['helix', ['hacks/helix.c'], hack + hsv + erase],
	['hypercube', ['hacks/hypercube.c'], hack],
	['imsmap', ['hacks/imsmap.c'], hack + col],
	['kaleidescope', ['hacks/kaleidescope.c'], hack],
	['lmorph', ['hacks/lmorph.c'], hack],
	['maze', ['hacks/maze.c'], hack + erase + png],
	['moire', ['hacks/moire.c'], hack + col + shm],
# 	['moire2', ['hacks/moire2.c'], hack + col + dbe],
# 	['noseguy', ['hacks/noseguy.c'], hack + png + text],
	['pedal', ['hacks/pedal.c'], hack + hsv + erase],
	['pyro', ['hacks/pyro.c'], hack + hsv],
	['qix', ['hacks/qix.c'], hack + hsv + alp],
	['rocks', ['hacks/rocks.c'], hack + col],
	['rorschach', ['hacks/rorschach.c'], hack + hsv + erase],
	['slidescreen', ['hacks/slidescreen.c'], hack + grab],
	['goop', ['hacks/goop.c'], hack + hsv + alp + spl],
	['starfish', ['hacks/starfish.c'], hack + col + spl],
	['munch', ['hacks/munch.c'], hack + col + spl],
	['rdbomb', ['hacks/rdbomb.c'], hack + col + shm],
	['coral', ['hacks/coral.c'], hack + col + erase],
	['xjack', ['hacks/xjack.c'], hack],
	['xlyap', ['hacks/xlyap.c'], hack + col],
	['cynosure', ['hacks/cynosure.c'], hack + col],
	['epicycle', ['hacks/epicycle.c'], hack + col + erase],
# 	['interference', ['hacks/interference.c'], hack + col + shm + thro + dbe],
	['truchet', ['hacks/truchet.c'], hack + col],
	['bsod', ['hacks/bsod.c'], hack + grab + apple2 + png],
# 	['apple2', ['hacks/apple2.c','hacks/apple2-main.c'], hack + atv + grab + text + png],
	['xanalogtv', ['hacks/xanalogtv.c'], hack + atv + grab + png],
	# analogtv2,analogtv-cli: skipped, complicated
	['distort', ['hacks/distort.c'], hack + grab + shm],
# 	['kumppa', ['hacks/kumppa.c'], hack + dbe],
	['t3d', ['hacks/t3d.c'], hack + col],
	['penetrate', ['hacks/penetrate.c'], hack + col],
# 	['deluxe', ['hacks/deluxe.c'], hack + alp + col + dbe],
# 	['compass', ['hacks/compass.c'], hack + dbe],
	['squiral', ['hacks/squiral.c'], hack + col],
	['xflame', ['hacks/xflame.c'], hack + shm + png],
	['wander', ['hacks/wander.c'], hack + col + erase],
	['spotlight', ['hacks/spotlight.c'], hack + grab],
	['critical', ['hacks/critical.c'], hack + col + erase],
# 	['phosphor', ['hacks/phosphor.c'], hack + text + col + png],
# 	['xmatrix', ['hacks/xmatrix.c'], hack + text + png],
	['petri', ['hacks/petri.c'], hack + col + spl],
	['shadebobs', ['hacks/shadebobs.c'], hack + col + spl],
	['ccurve', ['hacks/ccurve.c'], hack + col + spl],
	['blaster', ['hacks/blaster.c'], hack],
	['bumps', ['hacks/bumps.c'], hack + grab + shm],
	['ripples', ['hacks/ripples.c'], hack + shm + col + grab],
	['xspirograph', ['hacks/xspirograph.c'], hack + col + erase],
	['nerverot', ['hacks/nerverot.c'], hack + col],
	['xrayswarm', ['hacks/xrayswarm.c'], hack],
	['hyperball', ['hacks/hyperball.c'], hack],
	['zoom', ['hacks/zoom.c'], hack + grab],
	['whirlwindwarp', ['hacks/whirlwindwarp.c'], hack + col],
	['rotzoomer', ['hacks/rotzoomer.c'], hack + grab + shm],
# 	['whirlygig', ['hacks/whirlygig.c'], hack + dbe + col],
	['speedmine', ['hacks/speedmine.c'], hack + col],
	['vermiculate', ['hacks/vermiculate.c'], hack + col],
	['twang', ['hacks/twang.c'], hack + grab + shm],
# 	['fluidballs', ['hacks/fluidballs.c'], hack + dbe],
# 	['anemone', ['hacks/anemone.c'], hack + col + dbe],
	['halftone', ['hacks/halftone.c'], hack + col],
	['metaballs', ['hacks/metaballs.c'], hack],
	['eruption', ['hacks/eruption.c'], hack + shm],
# 	['popsquares', ['hacks/popsquares.c'], hack + dbe + col],
	['barcode', ['hacks/barcode.c'], hack + hsv],
# 	['piecewise', ['hacks/piecewise.c'], hack + col + dbe],
# 	['cloudlife', ['hacks/cloudlife.c'], hack + col + dbe],
# 	['fontglide', ['hacks/fontglide.c'], hack + dbe + text],
	['pong', ['hacks/pong.c'], hack + atv + grab + png],
	['wormhole', ['hacks/wormhole.c'], hack],
	['fuzzyflakes', ['hacks/fuzzyflakes.c'], hack],
# 	['anemotaxis', ['hacks/anemotaxis.c'], hack + col + dbe],
	['memscroller', ['hacks/memscroller.c'], hack + shm + col],
	['substrate', ['hacks/substrate.c'], hack],
	['intermomentary', ['hacks/intermomentary.c'], hack + col],
	['interaggregate', ['hacks/interaggregate.c'], hack + col],
	['fireworkx', ['hacks/fireworkx.c'], hack + col],
	['boxfit', ['hacks/boxfit.c'], hack + col + grab],
	['ifs', ['hacks/ifs.c'], hack + col],
	['celtic', ['hacks/celtic.c'], hack + col + erase],
	['cwaves', ['hacks/cwaves.c'], hack + col],
	# m6502: skipped, complicated
	['abstractile', ['hacks/abstractile.c'], hack + col],
	['lcdscrub', ['hacks/lcdscrub.c'], hack],
	['hexadrop', ['hacks/hexadrop.c'], hack + col],
	['tessellimage', ['hacks/tessellimage.c','hacks/delaunay.c'], hack + grab],
	['glitchpeg', ['hacks/glitchpeg.c'], hack + png],
	['filmleader', ['hacks/filmleader.c'], hack + atv + grab + png],
	['vfeedback', ['hacks/vfeedback.c'], hack + atv + grab + png],
	['marbling', ['hacks/marbling.c'], hack + col + shm + thro],
	['binaryhorizon', ['hacks/binaryhorizon.c'], hack + col],
	['testx11', ['hacks/testx11.c','hacks/glx/rotator.c'], hack + col + png + bar + erase],
	['bouboule', ['hacks/bouboule.c'], xlock],
	['braid', ['hacks/braid.c'], xlock],
	['drift', ['hacks/drift.c'], xlock],
	['flag', ['hacks/flag.c'], xlock + png],
	['forest', ['hacks/forest.c'], xlock],
	['vines', ['hacks/vines.c'], xlock],
	['galaxy', ['hacks/galaxy.c'], xlock],
	['grav', ['hacks/grav.c'], xlock],
	['hopalong', ['hacks/hopalong.c'], xlock],
	['julia', ['hacks/julia.c'], xlock],
	['laser', ['hacks/laser.c'], xlock],
	['lightning', ['hacks/lightning.c'], xlock],
	['lisa', ['hacks/lisa.c'], xlock],
	['lissie', ['hacks/lissie.c'], xlock],
	['penrose', ['hacks/penrose.c'], xlock],
	['sierpinski', ['hacks/sierpinski.c'], xlock],
	['slip', ['hacks/slip.c'], xlock + grab],
	['sphere', ['hacks/sphere.c'], xlock],
	['spiral', ['hacks/spiral.c'], xlock],
	['strange', ['hacks/strange.c'], xlock + shm + thro],
	['swirl', ['hacks/swirl.c'], xlock + shm],
	['fadeplot', ['hacks/fadeplot.c'], xlock],
	['mountain', ['hacks/mountain.c'], xlock],
	['triangle', ['hacks/triangle.c'], xlock],
	['worm', ['hacks/worm.c'], xlock],
	['rotor', ['hacks/rotor.c'], xlock],
	['ant', ['hacks/ant.c'], xlock],
	['demon', ['hacks/demon.c'], xlock],
	['loop', ['hacks/loop.c'], xlock],
	['flow', ['hacks/flow.c'], xlock],
	['discrete', ['hacks/discrete.c'], xlock],
	['crystal', ['hacks/crystal.c'], xlock],
	['apollonian', ['hacks/apollonian.c'], xlock],
	['euler2d', ['hacks/euler2d.c'], xlock],
	['juggle', ['hacks/juggle.c'], xlock],
	['polyominoes', ['hacks/polyominoes.c'], xlock],
	['thornbird', ['hacks/thornbird.c'], xlock],
	['pacman', ['hacks/pacman.c','hacks/pacman_ai.c', 'hacks/pacman_level.c'], xlock + png],
	['fiberlamp', ['hacks/fiberlamp.c'], xlock],
	['scooter', ['hacks/scooter.c'], xlock],
# 	['xsublim', ['hacks/xsublim.c'], hack_1],
# 	['webcollage-helper', ['hacks/webcollage-helper.c'], hack_1],

	# GL hacks, see hacks/glx/Makefile.in
	# glplanet, dymaxionmap: skipped, complicated
	['pulsar', ['hacks/glx/pulsar.c'], glhack + png],
# 	['extrusion', ['hacks/glx/extrusion.c', 'hacks/glx/extrusion-helix2.c',  'hacks/glx/extrusion-helix3.c',  'hacks/glx/extrusion-helix4.c', 'hacks/glx/extrusion-joinoffset.c', 'hacks/glx/extrusion-screw.c', 'hacks/glx/extrusion-taper.c', 'hacks/glx/extrusion-twistoid.c'], glhack + png + track],
	['sierpinski3d', ['hacks/glx/sierpinski3d.c'], glhack + track],
	['gflux', ['hacks/glx/gflux.c'], glhack + track + glgrab],
# 	['starwars', ['hacks/glx/starwars.c','hacks/glx/glut_stroke.c','hacks/glx/glut_swidth.c'], glhack + text],
# 	['gltext', ['hacks/glx/gltext.c','hacks/glx/glut_stroke.c','hacks/glx/glut_swidth.c','hacks/glx/tube.c','hacks/glx/sphere.c'], glhack + track + text],
	['dangerball', ['hacks/glx/dangerball.c','hacks/glx/sphere.c','hacks/glx/tube.c'], glhack + track],
	['circuit', ['hacks/glx/circuit.c'], glhack],
	['menger', ['hacks/glx/menger.c'], glhack + track],
	['engine', ['hacks/glx/engine.c'], glhack + track],
	['flipscreen3d', ['hacks/glx/flipscreen3d.c'], glhack + track + glgrab],
	['glsnake', ['hacks/glx/glsnake.c'], glhack],
	['boxed', ['hacks/glx/boxed.c'], glhack],
	['glforestfire', ['hacks/glx/glforestfire.c'], glhack + track + png],
	['sballs', ['hacks/glx/sballs.c'], glhack + track + png],
	['cubenetic', ['hacks/glx/cubenetic.c'], glhack + track],
	['spheremonics', ['hacks/glx/spheremonics.c','hacks/glx/normals.c'], glhack + track],
	['marching', ['hacks/glx/lavalite.c','hacks/glx/marching.c','hacks/glx/normals.c'], glhack + track + png],
	['queens', ['hacks/glx/queens.c', 'hacks/glx/chessmodels.c'], glhack + track],
	['endgame', ['hacks/glx/endgame.c', 'hacks/glx/chessmodels.c'], glhack + track],
	['glblur', ['hacks/glx/glblur.c'], glhack + track],
	# flyingtoasters: skipped, complicated
	# ['flyingtoasters', ['hacks/glx/toaster.c','hacks/glx/toaster_base.c','hacks/glx/toaster_handle.c','hacks/glx/toaster_handle2.c','hacks/glx/toaster_jet.c','hacks/glx/toaster_knob.c','hacks/glx/toaster_slots.c','hacks/glx/toaster_wing.c','hacks/glx/toast.c','hacks/glx/toast2.c','hacks/glx/flyingtoasters.c','hacks/glx/gllist.c'], glhack + track + png],
	['bouncingcow', ['hacks/glx/bouncingcow.c','hacks/glx/gllist.c','hacks/glx/cow_face.c','hacks/glx/cow_hide.c','hacks/glx/cow_hoofs.c','hacks/glx/cow_horns.c','hacks/glx/cow_tail.c','hacks/glx/cow_udder.c'], glhack + track + png],
	# winduprobot: skipped, complicated
# 	['winduprobot', ['hacks/glx/winduprobot.c','hacks/glx/gllist.c','hacks/glx/robot.c','hacks/glx/robot-wireframe.c','hacks/glx/sphere.c'], glhack + track + gears_1 + text],
	# vigilance: skipped, complicated
	# ['vigilance', ['hacks/glx/seccam.c','hacks/glx/gllist.c','hacks/glx/vigilance.c'], glhack + track],
	['glslideshow', ['hacks/glx/glslideshow.c'], glhack + glgrab],
	['jigglypuff', ['hacks/glx/jigglypuff.c'], glhack + track + png],
	['klein', ['hacks/glx/klein.c'], glhack + track],
	['surfaces', ['hacks/glx/surfaces.c'], glhack + track],
	['hypertorus', ['hacks/glx/hypertorus.c'], glhack + track],
	['projectiveplane', ['hacks/glx/projectiveplane.c'], glhack + track],
	['romanboy', ['hacks/glx/romanboy.c'], glhack + track],
	['etruscanvenus', ['hacks/glx/etruscanvenus.c'], glhack + track],
	['sphereeversion', ['hacks/glx/sphereeversion.c','hacks/glx/sphereeversion-analytic.c','hacks/glx/sphereeversion-corrugations.c'], glhack + track + png],
	['glmatrix', ['hacks/glx/glmatrix.c'], glhack + png],
	['cubestorm', ['hacks/glx/cubestorm.c'], glhack + track],
	['glknots', ['hacks/glx/glknots.c','hacks/glx/tube.c'], glhack + track],
	['blocktube', ['hacks/glx/blocktube.c'], glhack + png],
	['flipflop', ['hacks/glx/flipflop.c'], glhack + track + glgrab],
	['antspotlight', ['hacks/glx/antspotlight.c','hacks/glx/sphere.c'], glhack + track + glgrab],
	['polytopes', ['hacks/glx/polytopes.c'], glhack + track],
	['stonerview', ['hacks/glx/stonerview.c','hacks/glx/stonerview-osc.c','hacks/glx/stonerview-move.c','hacks/glx/stonerview-view.c'], glhack + track],
	# molecules: skipped,complicated
	['gleidescope', ['hacks/glx/gleidescope.c'], glhack + glgrab + png],
	['mirrorblob', ['hacks/glx/mirrorblob.c'], glhack + glgrab + track],
	['blinkbox', ['hacks/glx/blinkbox.c','hacks/glx/sphere.c'], glhack],
	['noof', ['hacks/glx/noof.c'], glhack],
	['polyhedra', ['hacks/glx/polyhedra.c','hacks/glx/polyhedra-gl.c','hacks/glx/normals.c','hacks/glx/teapot.c'], glhack + track],
	['antinspect', ['hacks/glx/antinspect.c','hacks/glx/sphere.c'], glhack + track],
	['providence', ['hacks/glx/providence.c'], glhack + track],
	['pinion', ['hacks/glx/pinion.c'], glhack + track + gears_1],
	['moebiusgears', ['hacks/glx/moebiusgears.c'], glhack + track + gears_1],
	['boing', ['hacks/glx/boing.c','hacks/glx/normals.c'], glhack + track],
	['carousel', ['hacks/glx/carousel.c'], glhack + track + glgrab],
# 	['fliptext', ['hacks/glx/fliptext.c'], glhack + text],
	['antmaze', ['hacks/glx/antmaze.c','hacks/glx/sphere.c','hacks/glx/tube.c'], glhack + track],
	['tangram', ['hacks/glx/tangram.c','hacks/glx/tangram_shapes.c'], glhack],
	['crackberg', ['hacks/glx/crackberg.c'], glhack],
	['glhanoi', ['hacks/glx/glhanoi.c'], glhack + track],
	['cube21', ['hacks/glx/cube21.c'], glhack + track],
	['timetunnel', ['hacks/glx/timetunnel.c'], glhack + track + png],
	['juggler3d', ['hacks/glx/juggler3d.c','hacks/glx/sphere.c','hacks/glx/tube.c'], glhack + track],
	['dnalogo', ['hacks/glx/dnalogo.c','hacks/glx/tube.c','hacks/glx/sphere.c','hacks/glx/normals.c'], glhack + track],
	['topblock', ['hacks/glx/topblock.c','hacks/glx/tube.c','hacks/glx/sphere.c'], glhack + track],
	['glschool', ['hacks/glx/glschool.c','hacks/glx/glschool_alg.c','hacks/glx/glschool_gl.c','hacks/glx/tube.c','hacks/glx/sphere.c','hacks/glx/normals.c'], glhack],
	['glcells', ['hacks/glx/glcells.c'], glhack],
	['voronoi', ['hacks/glx/voronoi.c'], glhack],
	['lockward', ['hacks/glx/lockward.c'], glhack],
	['cubicgrid', ['hacks/glx/cubicgrid.c'], glhack + track],
	['hypnowheel', ['hacks/glx/hypnowheel.c'], glhack + track],
	['skytentacles', ['hacks/glx/skytentacles.c','hacks/glx/normals.c'], glhack + track + png],
	# sonar: skipped, complicated
	['jigsaw', ['hacks/glx/jigsaw.c','hacks/glx/normals.c'], glhack + track + glgrab + spl],
	['photopile', ['hacks/glx/photopile.c','hacks/glx/dropshadow.c'], glhack + glgrab],
	['rubikblocks', ['hacks/glx/rubikblocks.c'], glhack + track],
	['hilbert', ['hacks/glx/hilbert.c','hacks/glx/sphere.c','hacks/glx/tube.c'], glhack + track],
	['companioncube', ['hacks/glx/companion.c','hacks/glx/companion_quad.c','hacks/glx/companion_disc.c','hacks/glx/companion_heart.c','hacks/glx/gllist.c'], glhack + track],
	['tronbit', ['hacks/glx/tronbit.c','hacks/glx/tronbit_idle1.c','hacks/glx/tronbit_idle2.c','hacks/glx/tronbit_no.c','hacks/glx/tronbit_yes.c','hacks/glx/gllist.c'], glhack + track],
	['kaleidocycle', ['hacks/glx/kaleidocycle.c','hacks/glx/normals.c'], glhack + track],
	['quasicrystal', ['hacks/glx/quasicrystal.c'], glhack + track],
	['unknownpleasures', ['hacks/glx/unknownpleasures.c'], glhack + track + png],
	['geodesic', ['hacks/glx/geodesic.c','hacks/glx/normals.c'], glhack + track],
	['geodesicgears', ['hacks/glx/geodesicgears.c'], glhack + track + gears_1],
	['cityflow', ['hacks/glx/cityflow.c'], glhack + track],
	# splitflag: skipped, complicated
	['unicrud', ['hacks/glx/unicrud.c'], glhack + track],
	['energystream', ['hacks/glx/energystream.c'], glhack + track],
	['raverhoop', ['hacks/glx/raverhoop.c'], glhack + track],
	['hydrostat', ['hacks/glx/hydrostat.c','hacks/glx/sphere.c','hacks/glx/normals.c'], glhack + track],
	['discoball', ['hacks/glx/discoball.c'], glhack + track],
	['cubetwist', ['hacks/glx/cubetwist.c'], glhack + track],
	['cubestack', ['hacks/glx/cubestack.c'], glhack + track],
	['splodesic', ['hacks/glx/splodesic.c'], glhack + track],
	['hexstrut', ['hacks/glx/hexstrut.c','hacks/glx/normals.c'], glhack + track],
	['esper', ['hacks/glx/esper.c'], glhack + glgrab],
	# razzledazzle: skipped, complicated
	['peepers', ['hacks/glx/peepers.c','hacks/glx/normals.c'], glhack + track + png],
	['crumbler', ['hacks/glx/crumbler.c','hacks/glx/quickhull.c'], glhack + track],
	['maze3d', ['hacks/glx/maze3d.c'], glhack + track + png],
	# handsy: skipped, complicated
	['gravitywell', ['hacks/glx/gravitywell.c'], glhack + track],
	['deepstars', ['hacks/glx/deepstars.c'], glhack + track],
	['gibson', ['hacks/glx/gibson.c'], glhack + track + png],
	['covid19', ['hacks/glx/covid19.c','hacks/glx/sphere.c','hacks/glx/tube.c'], glhack + track],
	# headroom: skipped, complicated
	['beats', ['hacks/glx/beats.c','hacks/glx/sphere.c'], glhack],
# 	['mapscroller', ['hacks/glx/mapscroller.c'], glhack + glgrab + png],
	['squirtorus', ['hacks/glx/squirtorus.c','hacks/glx/normals.c'], glhack + track + png + spl],
	['nakagin', ['hacks/glx/nakagin.c'], glhack + track],
	# chompytower: skipped, complicated
	['hextrail', ['hacks/glx/hextrail.c','hacks/glx/normals.c'], glhack + track],
]

# 


foreach hack : hacks
	name = hack[0]
	sources = hack[1]
	common_sources = hack[2]
	
	mod_sources = []
	foreach f : sources
		mod_sources += '../' + f
	endforeach
	
	mod_common_sources = []
	foreach f : common_sources
		mod_common_sources += '../' + f
	endforeach

	common_objs = lib.extract_objects(mod_common_sources)

	exec = executable(
		'xscreensaver-'+name,
		files(mod_sources) + client_protos_src + client_protos_headers,
		objects: common_objs,
		dependencies: base_deps,
		include_directories: include_dirs,
		c_args: build_flags,
		install : true
	)
endforeach
