/* xscreensaver, Copyright © 1992-2022 Jamie Zawinski <jwz@jwz.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or 
 * implied warranty.
 *
 * And remember: X Windows is to graphics hacking as roman numerals are to
 * the square root of pi.
 */

/* This file contains simple code to open a window or draw on the root.
   The idea being that, when writing a graphics hack, you can just link
   with this .o to get all of the uninteresting junk out of the way.

   Create a few static global procedures and variables:

      static void *YOURNAME_init (Display *, Window);

          Return an opaque structure representing your drawing state.

      static unsigned long YOURNAME_draw (Display *, Window, void *closure);

          Draw one frame.
          The `closure' arg is your drawing state, that you created in `init'.
          Return the number of microseconds to wait until the next frame.

          This should return in some small fraction of a second. 
          Do not call `usleep' or loop excessively.  For long loops, use a
          finite state machine.

      static void YOURNAME_reshape (Display *, Window, void *closure,
				    unsigned int width, unsigned int height);

          Called when the size of the window changes with the new size.

      static Bool YOURNAME_event (Display *, Window, void *closure,
				  XEvent *event);

          Called when a keyboard or mouse event arrives.
          Return True if you handle it in some way, False otherwise.

      static void YOURNAME_free (Display *, Window, void *closure);

           Called when you are done: free everything you've allocated,
           including your private `state' structure.  

           NOTE: this is called in windowed-mode when the user typed
           'q' or clicks on the window's close box; but when
           xscreensaver terminates this screenhack, it does so by
           killing the process with SIGSTOP.  So this callback is
           mostly useless.

      static char YOURNAME_defaults [] = { "...", "...", ... , 0 };

           This variable is an array of strings, your default resources.
           Null-terminate the list.

      static XrmOptionDescRec YOURNAME_options[] = { { ... }, ... { 0,0,0,0 } }

           This variable describes your command-line options.
           Null-terminate the list.

      Finally , invoke the XSCREENSAVER_MODULE() macro to tie it all together.

   Additional caveats:

      - Make sure that all functions in your module are static (check this
        by running "nm -g" on the .o file).

      - Do not use global variables: all such info must be stored in the
        private `state' structure.

      - Do not use static function-local variables, either.  Put it in `state'.

        Assume that there are N independent runs of this code going in the
        same address space at the same time: they must not affect each other.

      - Don't forget to write an XML file to describe the user interface
        of your screen saver module.  See .../hacks/config/README for details.
 */


/* Flow of control for the "screenhack" API:

      main
      xscreensaver_function_table->setup_cb => none
      XtAppInitialize
      pick_visual
      init_window
      run_screenhack_table (loops forever)
      !  xscreensaver_function_table.init_cb => HACK_init (once)
         usleep_and_process_events
      !    xscreensaver_function_table.event_cb => HACK_event
      !  xscreensaver_function_table.draw_cb => HACK_draw

   The "xlockmore" API (which is also the OpenGL API) acts like a screenhack
   but then adds another layer underneath that, before eventually running the
   hack's methods.  Flow of control for the "xlockmore" API:

      main
      + xscreensaver_function_table->setup_cb => xlockmore_setup (opt handling)
      XtAppInitialize
      pick_visual
      init_window
      run_screenhack_table (loops forever)
      !  xscreensaver_function_table.init_cb => xlockmore_init (once)
         usleep_and_process_events
      !    xscreensaver_function_table.event_cb => xlockmore_event
      +      xlockmore_function_table.hack_handle_events => HACK_handle_event
      !  xscreensaver_function_table.draw_cb => xlockmore_draw
      +    xlockmore_function_table.hack_init => init_HACK
      +      init_GL (eglCreateContext or glXCreateContext)
      +    xlockmore_function_table.hack_draw => draw_HACK
 */


#include <stdio.h>


#include "screenhackI.h"
// #include "xmu.h"
#include "version.h"
// #include "vroot.h"
#include "fps.h"
#include "jwxyzI.h"

#ifdef HAVE_RECORD_ANIM
# include "recanim.h"
#endif

// #ifndef _XSCREENSAVER_VROOT_H_
// # error Error!  You have an old version of vroot.h!  Check -I args.
// #endif /* _XSCREENSAVER_VROOT_H_ */

#include <wayland-egl.h>
#include <GL/gl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#ifndef isupper
# define isupper(c)  ((c) >= 'A' && (c) <= 'Z')
#endif
#ifndef _tolower
# define _tolower(c)  ((c) - 'A' + 'a')
#endif


/* This is defined by the SCREENHACK_MAIN() macro via screenhack.h.
 */
extern struct xscreensaver_function_table *xscreensaver_function_table;


const char *progname;   /* used by hacks in error messages */
const char *progclass;  /* used by ../utils/resources.c */
Bool mono_p;		/* used by hacks */

#ifdef EXIT_AFTER
static time_t exit_after;	/* Exit gracefully after N seconds */
#endif


struct screenhack_state {
  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_compositor *compositor;
  struct zwlr_layer_shell_v1 *shell;
  EGLDisplay egl_dpy;
  EGLConfig egl_cfg;

  char *target_output_name;
  struct wl_output *target_output;

  struct wl_surface *surface;
  struct zwlr_layer_surface_v1 *layer_surface;
  struct wl_egl_window *egl_window;
  EGLContext egl_context;
  EGLSurface egl_surface;

  Bool needs_ack_configure;
  uint32_t configure_serial;
  int width;
  int height;

  Bool running;
};

static struct screenhack_state state = {0};

/* Helper functions; maybe move into 'jwxyz-wayland.c' */


struct jwxyz_Drawable {
  enum { WINDOW, PIXMAP } type;
  XRectangle frame;
  union {
    /* JWXYZ_GL */
    EGLSurface egl_surface;
    GLuint texture; /* If this is 0, it's the default framebuffer. */

    /* JWXYZ_IMAGE */
    void *image_data;
  };
  union {
    struct {
      struct running_hack *rh;
      int last_mouse_x, last_mouse_y;
    } window;
    struct {
      int depth;
    } pixmap;
  };
};

Pixmap
XCreatePixmap (Display *dpy, Drawable d,
               unsigned int width, unsigned int height, unsigned int depth)
{
  Window win = XRootWindow(dpy, 0);

  Pixmap p = malloc(sizeof(*p));
  p->type = PIXMAP;
  p->frame.x = 0;
  p->frame.y = 0;
  p->frame.width = width;
  p->frame.height = height;

  // Assert(depth == 1 || depth == visual_depth(NULL, NULL), "XCreatePixmap: bad depth");
  p->pixmap.depth = depth;

  // create_pixmap (win, p);

  /* For debugging. */
# if 0
  jwxyz_bind_drawable (dpy, win, p);
  glClearColor (frand(1), frand(1), frand(1), 0);
  glClear (GL_COLOR_BUFFER_BIT);
# endif

  return p;
}

void
jwxyz_abort (const char *fmt, ...)
{
  /* Send error to Android device log */
  if (!fmt || !*fmt)
    fmt = "abort";

  char buf[256];
  va_list args;
  va_start (args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  write(STDERR_FILENO, buf, strlen(buf));
  va_end (args);
  
  // calling abort() recurses here
  (*(char*)NULL)++;
}


void
jwxyz_logv (Bool error, const char *fmt, va_list args)
{
  char buf[256];
  int i;
  
  /* note: this intercepts fprintf somehow, so must use 'write' directly */
  i = sprintf(buf, "%s", error ? "[error]: " : "[info ]: ");
  i += vsprintf(buf + i, fmt, args);
  
  write(STDERR_FILENO, buf, strlen(buf));
}


void
jwxyz_render_text (Display *dpy, void *native_font,
                   const char *str, size_t len, Bool utf8, Bool antialias_p,
                   XCharStruct *cs, char **pixmap_ret)
{
  // TODO
}


void *
jwxyz_load_native_font (Window window,
                        int traits_jwxyz, int mask_jwxyz,
                        const char *font_name_ptr, size_t font_name_length,
                        int font_name_type, float size,
                        char **family_name_ret,
                        int *ascent_ret, int *descent_ret)
{
  // TODO:
}


double
current_device_rotation (void)
{
  // TODO
  return 0.0;
}

Bool
ignore_rotation_p (Display *dpy)
{
  // struct running_hack *rh = XRootWindow(dpy, 0)->window.rh;
  // return rh->ignore_rotation_p;
}

void
jwxyz_release_native_font (Display *dpy, void *native_font)
{
  // TODO:
}

void
jwxyz_bind_drawable (Display *dpy, Window w, Drawable d)
{
  // TODO
}


/* report a GL error. */
void
check_gl_error (const char *type)
{
  char buf[100];
  char buf2[200];
  GLenum i;
  const char *e;
  switch ((i = glGetError())) {
    case GL_NO_ERROR: return;
    case GL_INVALID_ENUM:          e = "invalid enum";      break;
    case GL_INVALID_VALUE:         e = "invalid value";     break;
    case GL_INVALID_OPERATION:     e = "invalid operation"; break;
    case GL_STACK_OVERFLOW:        e = "stack overflow";    break;
    case GL_STACK_UNDERFLOW:       e = "stack underflow";   break;
    case GL_OUT_OF_MEMORY:         e = "out of memory";     break;
#ifdef GL_TABLE_TOO_LARGE_EXT
    case GL_TABLE_TOO_LARGE_EXT:   e = "table too large";   break;
#endif
#ifdef GL_TEXTURE_TOO_LARGE_EXT
    case GL_TEXTURE_TOO_LARGE_EXT: e = "texture too large"; break;
#endif
    default:
      e = buf; sprintf (buf, "unknown GL error %d", (int) i); break;
  }
  sprintf (buf2, "%.50s: %.50s", type, e);
}

void
jwxyz_assert_drawable (Window main_window, Drawable d)
{
  check_gl_error("jwxyz_assert_drawable");
}


void
jwxyz_assert_gl (void)
{
  check_gl_error("jwxyz_assert_gl");
}

void
jwxyz_gl_copy_area (Display *dpy, Drawable src, Drawable dst, GC gc,
                    int src_x, int src_y,
                    unsigned int width, unsigned int height,
                    int dst_x, int dst_y)
{
  // TODO
}


float
jwxyz_scale (Window main_window)
{
  // TODO: Use the actual device resolution.
  return 2;
}

float
jwxyz_font_scale (Window main_window)
{
  return jwxyz_scale (main_window);
}

const char *
jwxyz_default_font_family (int require)
{
  /* Font families in XLFDs are totally ignored (for now). */
  return "sans-serif";
}

const XRectangle *
jwxyz_frame (Drawable d)
{
  return &d->frame;
}


unsigned int
jwxyz_drawable_depth (Drawable d)
{
  return (d->type == WINDOW
          ? visual_depth (NULL, NULL)
          : d->pixmap.depth);
}

void
jwxyz_get_pos (Window w, XPoint *xvpos, XPoint *xp)
{
  xvpos->x = 0;
  xvpos->y = 0;

  if (xp) {
    xp->x = w->window.last_mouse_x;
    xp->y = w->window.last_mouse_y;
  }
}

char *
get_string_resource (Display *dpy, char *res_name, char *res_class)
{
  // TODO: implement this
  fprintf(stderr, "TODO\n");
  abort();
  return 0;
}

Bool 
get_boolean_resource (Display *dpy, char *res_name, char *res_class)
{
  char *tmp, buf [100];
  char *s = get_string_resource (dpy, res_name, res_class);
  char *os = s;
  if (! s) return 0;
  for (tmp = buf; *s; s++)
    *tmp++ = isupper (*s) ? _tolower (*s) : *s;
  *tmp = 0;
  free (os);

  while (*buf &&
	 (buf[strlen(buf)-1] == ' ' ||
	  buf[strlen(buf)-1] == '\t'))
    buf[strlen(buf)-1] = 0;

  if (!strcmp (buf, "on") || !strcmp (buf, "true") || !strcmp (buf, "yes"))
    return 1;
  if (!strcmp (buf,"off") || !strcmp (buf, "false") || !strcmp (buf,"no"))
    return 0;
  fprintf (stderr, "%s: %s must be boolean, not %s.\n",
	   progname, res_name, buf);
  return 0;
}

int 
get_integer_resource (Display *dpy, char *res_name, char *res_class)
{
  int val;
  char c, *s = get_string_resource (dpy, res_name, res_class);
  char *ss = s;
  if (!s) return 0;

  while (*ss && *ss <= ' ') ss++;			/* skip whitespace */

  if (ss[0] == '0' && (ss[1] == 'x' || ss[1] == 'X'))	/* 0x: parse as hex */
    {
      if (1 == sscanf (ss+2, "%x %c", (unsigned int *) &val, &c))
	{
	  free (s);
	  return val;
	}
    }
  else							/* else parse as dec */
    {
      if (1 == sscanf (ss, "%d %c", &val, &c))
	{
	  free (s);
	  return val;
	}
    }

  fprintf (stderr, "%s: %s must be an integer, not %s.\n",
	   progname, res_name, s);
  free (s);
  return 0;
}

double
get_float_resource (Display *dpy, char *res_name, char *res_class)
{
  double val;
  char c, *s = get_string_resource (dpy, res_name, res_class);
  if (! s) return 0.0;
  if (1 == sscanf (s, " %lf %c", &val, &c))
    {
      free (s);
      return val;
    }
  fprintf (stderr, "%s: %s must be a float, not %s.\n",
	   progname, res_name, s);
  free (s);
  return 0.0;
}


static XrmOptionDescRec default_options [] = {
//  { "-root",	".root",		XrmoptionNoArg, "True" },
//  { "-window",	".root",		XrmoptionNoArg, "False" },
//  { "-install",	".installColormap",	XrmoptionNoArg, "True" },
//  { "-noinstall",".installColormap",	XrmoptionNoArg, "False" },
//  { "-visual",	".visualID",		XrmoptionSepArg, 0 },
//  { "-window-id", ".windowID",		XrmoptionSepArg, 0 },

  { "-output-name", ".wlOutputName",	XrmoptionSepArg, 0 },

  { "-mono",	".mono",		XrmoptionNoArg, "True" },
  { "-fps",	".doFPS",		XrmoptionNoArg, "True" },
  { "-no-fps",  ".doFPS",		XrmoptionNoArg, "False" },

# ifdef DEBUG_PAIR
  { "-pair",	".pair",		XrmoptionNoArg, "True" },
# endif
# ifdef HAVE_RECORD_ANIM
  { "-record-animation", ".recordAnim", XrmoptionSepArg, 0 },
# endif
# ifdef EXIT_AFTER
  { "-exit-after",	".exitAfter",	XrmoptionSepArg, 0 },
# endif

  { 0, 0, 0, 0 }
};

static char *default_defaults[] = {
  ".root:		false",
  "*geometry:		1280x720", /* this should be .geometry, but noooo... */
  "*mono:		false",
  "*installColormap:	false",
  "*doFPS:		false",
  "*multiSample:	false",
  "*visualID:		default",
  "*windowID:		",
  "*desktopGrabber:	xscreensaver-getimage %s",
  0
};
static XrmOptionDescRec *merged_options;
static int merged_options_size;
static char **merged_defaults;


static void
merge_options (void)
{
  struct xscreensaver_function_table *ft = xscreensaver_function_table;

  const XrmOptionDescRec *options = ft->options;
  const char * const *defaults    = ft->defaults;
  const char *progclass           = ft->progclass;

  int def_opts_size, opts_size;
  int def_defaults_size, defaults_size;

  for (def_opts_size = 0; default_options[def_opts_size].option;
       def_opts_size++)
    ;
  for (opts_size = 0; options[opts_size].option; opts_size++)
    ;

  merged_options_size = def_opts_size + opts_size;
  merged_options = (XrmOptionDescRec *)
    malloc ((merged_options_size + 1) * sizeof(*default_options));
  memcpy (merged_options, default_options,
	  (def_opts_size * sizeof(*default_options)));
  memcpy (merged_options + def_opts_size, options,
	  ((opts_size + 1) * sizeof(*default_options)));

  for (def_defaults_size = 0; default_defaults[def_defaults_size];
       def_defaults_size++)
    ;
  for (defaults_size = 0; defaults[defaults_size]; defaults_size++)
    ;
  merged_defaults = (char **)
    malloc ((def_defaults_size + defaults_size + 1) * sizeof (*defaults));;
  memcpy (merged_defaults, default_defaults,
	  def_defaults_size * sizeof(*defaults));
  memcpy (merged_defaults + def_defaults_size, defaults,
	  (defaults_size + 1) * sizeof(*defaults));

  /* This totally sucks.  Xt should behave like this by default.
     If the string in `defaults' looks like ".foo", change that
     to "Progclass.foo".
   */
  {
    char **s;
    for (s = merged_defaults; *s; s++)
      if (**s == '.')
	{
	  const char *oldr = *s;
	  char *newr = (char *) malloc(strlen(oldr) + strlen(progclass) + 3);
	  strcpy (newr, progclass);
	  strcat (newr, oldr);
	  *s = newr;
	}
      else
        *s = strdup (*s);
  }
}

static void 
usage(void) {
  // TODO: this already exists in the original screenhack.c

  fprintf(stdout, "Program: %s\n", progclass);

  unsigned i;
  for (i = 0; i < merged_options_size; i++) {
//    char *defv = merged_defaults[i];
//    while (*defv) {
//      defv++;
//      if (*(defv-1) == ':') {
//        break;
//      }
//    }
//    while (*defv == ' ' || *defv == '\t') {
//      defv++;
//    }
    // todo: scan defaults list for option? */
    fprintf(stdout, "Option: %s %s %d\n", merged_options[i].option,
            merged_options[i].specifier, merged_options[i].argKind);
  }

}


static void
setup_egl(struct wl_display *wl_dpy) {
  const char *extensions_list = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
  Bool has_ext_platform = False, has_khr_platform = False;
  if (strstr(extensions_list, "EGL_EXT_platform_wayland")) {
    has_ext_platform = True;
  }
  if (strstr(extensions_list, "EGL_KHR_platform_wayland")) {
    has_khr_platform = True;
  }
  if (!has_khr_platform && !has_ext_platform) {
    fprintf(stderr, "No egl wayland\n");
    abort();
  }

  state.egl_dpy = eglGetPlatformDisplay(
    has_ext_platform ? EGL_PLATFORM_WAYLAND_EXT : EGL_PLATFORM_WAYLAND_KHR,
    wl_dpy, NULL);
  if (state.egl_dpy == EGL_NO_DISPLAY) {
    fprintf(stderr, "Failed to get display\n");
    abort();
  }

  int major_version = -1, minor_version = -1;
  EGLBoolean ret = eglInitialize(state.egl_dpy, &major_version, &minor_version);
  if (ret == EGL_FALSE) {
    fprintf(stderr, "Failed to init display\n");
    abort();
  }

  if (!eglBindAPI(EGL_OPENGL_API)) {
    fprintf(stderr, "Failed to bind opengl api\n");
    abort();
  }

  int count= 0;
  if (!eglGetConfigs(state.egl_dpy, NULL, 0, &count) || count < 1) {
    fprintf(stderr, "Failed to get configs\n");
    abort();
  }

  EGLConfig *configs = calloc(count, sizeof(EGLConfig));
  if (!configs) {
      fprintf(stderr, "Failed to allocate config list");
      abort();
    }

  EGLint config_attrib_list[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RED_SIZE, 1,
      EGL_GREEN_SIZE, 1,
      EGL_BLUE_SIZE, 1,
      EGL_DEPTH_SIZE, 1,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_NONE
  };

  int nret = 0;
  if (!eglChooseConfig(state.egl_dpy, config_attrib_list, configs, count, &nret) || nret < 1) {
      fprintf(stderr, "Failed to get matching config\n");
      abort();
  }
  state.egl_cfg = configs[0];
  free(configs);
}

static void noop() {

}

static void
handle_output_name(void *data,
             struct wl_output *wl_output,
             const char *name) {
  if (state.target_output_name && !strcmp(name, state.target_output_name)) {
    if (state.target_output && state.target_output != wl_output) {
      fprintf(stderr, "Encountered second output with name %s, ignoring\n", state.target_output_name);
      wl_output_destroy(wl_output);
      return;
    }
    state.target_output = wl_output;
  } else {
    wl_output_destroy(wl_output);
  }
}

struct wl_output_listener output_listener = {
  noop, // geometry
  noop, // mode
  noop, // done
  noop, // scale
  handle_output_name,
  noop, // description
};

static void registry_global(void *data, struct wl_registry *registry,
     uint32_t name, const char *interface, uint32_t version) {
  if (strcmp(interface, wl_compositor_interface.name) == 0) {
       state.compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
  } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        struct wl_seat *seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
        (void)seat;
        // TODO: handle input if available
  } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        state.shell = wl_registry_bind(registry, name,
                      &zwlr_layer_shell_v1_interface, 1);
  } else if (strcmp(interface, wl_output_interface.name) == 0 && version >= 4) {
      // require version 4, which has a 'name' event
        struct wl_output *output = wl_registry_bind(registry, name,
                      &wl_output_interface, 4);
        wl_output_add_listener(output, &output_listener, NULL);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    registry_global_remove
};


static void handle_configure(void *data,
     struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1,
     uint32_t serial, uint32_t width, uint32_t height) {
  state.needs_ack_configure = True;
  state.configure_serial = serial;
  state.width = width;
  state.height = height;
}
static void
handle_closed(void *data,
      struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1) {
  state.running = False;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
  handle_configure,
  handle_closed,
};

int main(int argc, char **argv) {
  /* TODO: implement this. Also define 'get_float_resource/get_integer_resource/get_string_resource/get_boolean_resource' */
  struct xscreensaver_function_table *ft = xscreensaver_function_table;
  progname = argv[0];   /* reset later */
  progclass = ft->progclass;

  if (ft->setup_cb)
    ft->setup_cb (ft, ft->setup_arg);

  merge_options ();

  /* Need to implement own command line handling here, as XtAppInitialize
   * is not available */


  // if fail
  usage();

  state.display = wl_display_connect(NULL);

  setup_egl(state.display);

  state.target_output_name = NULL;//"WL-1"; // return to NULL

  state.registry = wl_display_get_registry(state.display);
  wl_registry_add_listener(state.registry, &registry_listener, NULL);
  wl_display_roundtrip(state.display);
  if (!state.compositor) {
    fprintf(stderr, "Wayland compositor is missing wl_compositor interface");
    return EXIT_FAILURE;
  }
  if (!state.shell) {
    fprintf(stderr, "Wayland compositor is missing layer-shell interface");
    return EXIT_FAILURE;
  }

  if (state.target_output_name) {
    /* second roundtrip: receive names from the outputs; if one matches,
     * it will set state.target_output */
    wl_display_roundtrip(state.display);

    if (!state.target_output) {
      fprintf(stderr, "No output with name '%s' was found. Run `wayland-info` to see a list of outputs and their names.\n", state.target_output_name);
      return EXIT_FAILURE;
    }
  }

  state.running = True;

  state.surface = wl_compositor_create_surface(state.compositor);

  /* if `state.target_output` is NULL, this picks compositor preferred output */
  state.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
    state.shell, state.surface, state.target_output,
    ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "wallpaper");
  // no size preference
  zwlr_layer_surface_v1_set_size(state.layer_surface, 0, 0);
  zwlr_layer_surface_v1_set_anchor(state.layer_surface,
    ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
    ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
  zwlr_layer_surface_v1_set_exclusive_zone(state.layer_surface, -1);
  zwlr_layer_surface_v1_add_listener(state.layer_surface,
    &layer_surface_listener, NULL);
  wl_surface_commit(state.surface);

  /* after this roundtrip, should have received a configure event */
  wl_display_roundtrip(state.display);
  if (!state.needs_ack_configure) {
      fprintf(stderr, "Did not received expected configure event\n");
      return EXIT_FAILURE;
  }

  if (state.width == 0 && state.height == 0) {
    /* (0,0) signifies client decision */
    state.width = 600;
    state.height = 400;
  }

  state.egl_window = wl_egl_window_create(state.surface, state.width, state.height);
  state.egl_surface = eglCreateWindowSurface(state.egl_dpy, state.egl_cfg, state.egl_window, NULL);
  state.egl_context = eglCreateContext(state.egl_dpy, state.egl_cfg, EGL_NO_CONTEXT, NULL);

  if (!eglMakeCurrent(state.egl_dpy, state.egl_surface, state.egl_surface, state.egl_context)) {

    fprintf(stderr, "Failed to make a context current\n");
    return EXIT_FAILURE;
  }

  struct jwxyz_Drawable window;
  window.type = WINDOW;
  Drawable w = &window;
  // this owns the EGL surface

  // this must be done after gl has been initialized
  // 'w' is a generic pointer that gets passed through
  Display *disp = jwxyz_gl_make_display(w);

  ft->init_cb(disp, w);


//      void *closure = init_cb (dpy, window, ft->setup_arg);
//      fps_state *fpst = fps_init (dpy, window);
  fprintf(stderr, "We are configure\n");
  // wl_egl_create()




    /* This is the one and only place that the random-number generator is
       seeded in any screenhack.  You do not need to seed the RNG again,
       it is done for you before your code is invoked. */
# undef ya_rand_init
  ya_rand_init (0);


#ifdef HAVE_RECORD_ANIM
  {
    int frames = get_integer_resource (dpy, "recordAnim", "Integer");
    if (frames > 0)
      anim_state = screenhack_record_anim_init (xgwa.screen, window, frames);
  }
#endif

  while (state.running) {
    // rate limited update loop, coalescing configure/resize ops?
      // use poll instead of usleep, due to interruptions on resize, scale change, rotation...
      // or maybe ppoll, for more accuracy?

      // also rate limit using frame callbacks
      break;
  }



#ifdef HAVE_RECORD_ANIM
  if (anim_state) screenhack_record_anim_free (anim_state);
#endif

  wl_display_disconnect(state.display);
    return EXIT_SUCCESS;
}




