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
#include "xlockmoreI.h"
// #include "xmu.h"
#include "version.h"
// #include "vroot.h"
#include "fps.h"
#include "jwxyzI.h"

#ifdef HAVE_RECORD_ANIM
# include "recanim.h"
#endif

#include <wayland-egl.h>
#include <GL/gl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <string.h>
#include <errno.h>

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

struct output_hack;

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
      struct output_hack *rh;
      int last_mouse_x, last_mouse_y;
    } window;
    struct {
      int depth;
    } pixmap;
  };
};


// screenhack_state + output_hack together are like running_hack in android
struct output_hack {
  struct wl_list link;
  struct screenhack_state *state;

  /* Wayland details */
  struct wl_output *output;
  uint32_t output_name;

  struct wl_surface *surface;
  struct zwlr_layer_surface_v1 *layer_surface;
  /* if frame_callback is non-NULL, compositor has not yet asked for a new frame */
  struct wl_callback *frame_callback;
  Bool needs_ack_configure;
  uint32_t configure_serial;
  int width;
  int height;

  /* EGL details */
  struct wl_egl_window *egl_window;
  EGLContext egl_context;
  EGLSurface egl_surface;
  GLuint frameBuffer;
  GLuint texColorBuffer;
  GLuint rboDepthStencil;

  /* Screenhack data */
  struct jwxyz_Drawable window;
  Display *display;
  /* the state for the hack running on this output */
  void *closure;
  fps_state *fpst;

};

struct screenhack_state {
  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_compositor *compositor;
  struct zwlr_layer_shell_v1 *shell;
  EGLDisplay egl_dpy;
  EGLConfig egl_cfg;

  char *target_output_name;
  struct wl_list outputs; /* of `struct output_hack` */

  Bool running;
};

static struct screenhack_state state = {0};

/* Helper functions; maybe move into 'jwxyz-wayland.c' */

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
  fprintf(stderr, "Call to unimplemented XCreatePixmap\n");

  /* For debugging. */
# if 0
  jwxyz_bind_drawable (dpy, win, p);
  glClearColor (frand(1), frand(1), frand(1), 0);
  glClear (GL_COLOR_BUFFER_BIT);
# endif

  return p;
}

int
XFreePixmap (Display *dpy, Pixmap p)
{
  fprintf(stderr, "Call to unimplemented XFreePixmap\n");
  abort();
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
  
  /* note: this intercepts fprintf somehow, so must use 'write' directly */
  vsnprintf(buf, sizeof(buf), fmt, args);
  write(STDERR_FILENO, buf, strlen(buf));
}


void
jwxyz_render_text (Display *dpy, void *native_font,
                   const char *str, size_t len, Bool utf8, Bool antialias_p,
                   XCharStruct *cs, char **pixmap_ret)
{
  fprintf(stderr, "Call to unimplemented jwxyz_render_text\n");
}


void *
jwxyz_load_native_font (Window window,
                        int traits_jwxyz, int mask_jwxyz,
                        const char *font_name_ptr, size_t font_name_length,
                        int font_name_type, float size,
                        char **family_name_ret,
                        int *ascent_ret, int *descent_ret)
{
  fprintf(stderr, "Call to unimplemented jwxyz_load_native_font\n");
}
char *
jwxyz_unicode_character_name (Display *, Font, unsigned long uc) {
  fprintf(stderr, "Call to unimplemented jwxyz_unicode_character_name\n");
}


double
current_device_rotation (void)
{
  return 0.0;
}

Bool
ignore_rotation_p (Display *dpy)
{
  return True;
}

void
jwxyz_release_native_font (Display *dpy, void *native_font)
{
  fprintf(stderr, "Call to unimplemented jwxyz_release_native_font\n");
}

void
jwxyz_bind_drawable (Display *dpy, Window w, Drawable d)
{
  jwxyz_assert_gl ();

  glViewport (0, 0, d->frame.width, d->frame.height);
  jwxyz_set_matrices (dpy, d->frame.width, d->frame.height, False);

  // TODO: put framebuffer binding here
}


Bool
validate_gl_visual (FILE *out, Screen *screen, const char *window_desc,
                    Visual *visual)
{
  return True;
}


Visual *
get_gl_visual (Screen *screen)
{
  fprintf(stderr, "Call to unimplemented get_gl_visual\n");
  // todo; define a visual based on what EGL provides
  return NULL;
}

/* Called by OpenGL savers using the XLockmore API.
 */
GLXContext
*
init_GL (ModeInfo *mi)
{
  /* The X11 version of this function is in hacks/glx/xlock-gl-utils.c
     That version:
       - Does the GLX or EGL initialization;
       - Does glDrawBuffer GL_BACK/GL_FRONT depending on GL_DOUBLEBUFFER;
       - Parses the "background" resource rather than assuming black.
   */
  glClearColor (0, 0, 0, 1);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glClearColor(0,0,0,1);

  glClearColor(0,0,0,1);

  static GLXContext ctx;
  return &ctx; //mi->glx_context;
}

/* used by the OpenGL screen savers
 */

/* Does nothing - prepareContext already did the work.
 */
void
glXMakeCurrent (Display *dpy, Window window, GLXContext context)
{
}

// needs to be implemented in Android...
/* Copy the back buffer to the front buffer.
 */
void
glXSwapBuffers (Display *dpy, Window window)
{
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
  return 1;
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

void
load_random_image_wayland (Screen *screen, Window window, Drawable drawable,
                           void (*callback) (Screen *, Window, Drawable,
                                             const char *name,
                                             XRectangle *geom, void *closure),
                           void *closure)
{
    fprintf(stderr, "Call to unimplemented load_random_image_wayland\n");
}

struct resource_kv {
  /* null key values signify wild card */
  char *progname;
  char *res_name;
  char *progclass;
  char *res_class;
  char *value;
};

/* append-structured list of resource (name,class)->value pairs
 * later values supersede earlier ones */
static struct resource_kv *resource_list = NULL;
static size_t resource_list_len = 0;
static size_t resource_list_capacity = 0;

static void
add_cmdline_resource(char *res_name, char *value) {
  if (resource_list_len == resource_list_capacity) {
      resource_list_capacity = resource_list_capacity > 0 ? 2 * resource_list_capacity : 4;
      /* malloc can fail */
      resource_list = realloc(resource_list, sizeof(struct resource_kv)*resource_list_capacity);
  }

  if (*res_name == '.') {
      res_name++;
  }
  /* todo: does *key ever happen ?*/
  resource_list[resource_list_len].progname = strdup(progclass);
  resource_list[resource_list_len].res_name = strdup(res_name);
  resource_list[resource_list_len].progclass = strdup(progclass);
  resource_list[resource_list_len].res_class = NULL;
  resource_list[resource_list_len].value = strdup(value);
  resource_list_len++;
}

static void
add_default_resource(char *default_line) {
  if (resource_list_len == resource_list_capacity) {
      resource_list_capacity = resource_list_capacity > 0 ? 2 * resource_list_capacity : 4;
      /* malloc can fail */
      resource_list = realloc(resource_list, sizeof(struct resource_kv)*resource_list_capacity);
  }

  char *pn, *rn;
  /* assuming well formed, no sanity checks right now */
  char *tmp = strdup(default_line);
  char *t2 = tmp;
  while (*t2 != ':') {
    if (!*t2) {
        abort();
    }
    t2++;
  }
  *t2 = 0;
  t2++;
  while (*t2 == ' ' || *t2 == '\t') {
     t2++;
  }
  char *end = t2 + strlen(t2);
  while (*(end - 1) == ' ' || *(end - 1) == '\t') {
    end--;
    *end = 0;
  }
  char *value = strdup(t2);

  if (*tmp == '*') {
    pn = NULL;
    rn = strdup(tmp + 1);
  } else {
    char *t3 = tmp;
    while (*t3 != '.') {
      if (!*t3) {
          abort();
      }
      t3++;
    }
    *t3 = 0;
    t3++;
    pn = strdup(tmp);
    rn = strdup(t3);
  }

  free(tmp);

  resource_list[resource_list_len].progname = pn;
  resource_list[resource_list_len].res_name = rn;
  resource_list[resource_list_len].progclass = strdup(progclass);
  resource_list[resource_list_len].res_class = NULL;
  resource_list[resource_list_len].value = value;
  resource_list_len++;
}

static char *
scan_resource(const char *progname, const char *res_name, const char *progclass, const char *res_class) {
  size_t j = resource_list_len;
  for (; j > 0; j--) {
    struct resource_kv entry = resource_list[j - 1];
    if (entry.progname && progname && strcmp(entry.progname, progname)) {
      continue;
    }
    if (entry.res_name && res_name && strcmp(entry.res_name, res_name)) {
      continue;
    }
    if (entry.progclass && progclass && strcmp(entry.progclass, progclass)) {
      continue;
    }
    if (entry.res_class && res_class && strcmp(entry.res_class, res_class)) {
      continue;
    }
    return strdup(entry.value);
  }
  return NULL;
}

char *
get_string_resource (Display *dpy, char *res_name, char *res_class)
{
  /* A very rough approximation of the XrmGetResource matching priority rules,
   * which should be good enough for what xscreensaver uses.
   */
  if (!strcmp(res_class, "*")) {
    res_class = NULL;
  }
  char *ret = NULL;
  ret = scan_resource(progclass, res_name, progclass, res_class);
  if (ret) {
    return ret;
  }
  ret = scan_resource(progclass, res_name, NULL, NULL);
  if (ret) {
    return ret;
  }
  ret = scan_resource(NULL, res_name, NULL, NULL);
  if (ret) {
    return ret;
  }
  return NULL;
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

static void handle_configure(void *data,
     struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1,
     uint32_t serial, uint32_t width, uint32_t height) {
  struct output_hack *output = data;
  output->needs_ack_configure = True;
  output->configure_serial = serial;
  output->width = width;
  output->height = height;
}

static void
output_hack_destroy(struct output_hack *output) {
    if (output->egl_window) {
        eglDestroyContext(state.egl_dpy, output->egl_context);
        eglDestroySurface(state.egl_dpy, output->egl_surface);
        wl_egl_window_destroy(output->egl_window);
    }
    if (output->frame_callback) {
        wl_callback_destroy(output->frame_callback);
    }
    if (output->surface) {
        wl_surface_destroy(output->surface);
    }
    if (output->layer_surface) {
        zwlr_layer_surface_v1_destroy(output->layer_surface);
    }
    if (output->closure) {
        xscreensaver_function_table->free_cb (output->display, &output->window, output->closure);
    }
    wl_list_remove(&output->link);
    free(output);
}

static void
handle_closed(void *data,
      struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1) {
  struct output_hack *output = data;
  output_hack_destroy(output);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
  handle_configure,
  handle_closed,
};

static void
noop(void) {

}


static void
handle_output_name(void *data,
             struct wl_output *wl_output,
             const char *name) {
  struct output_hack *output = data;
  if (state.target_output_name && strcmp(name, state.target_output_name)) {
     /* does not match name filter, delete output */
     output_hack_destroy(output);
     return;
  }

  /* time to initialize the output */
  if (!state.compositor || !state.shell) {
     fprintf(stderr, "Missing compositor or shell after first roundtrip\n");
     exit(EXIT_FAILURE);
  }

  output->surface = wl_compositor_create_surface(state.compositor);

  /* if `state.target_output` is NULL, this picks compositor preferred output */
  output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
    state.shell, output->surface, output->output,
    ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "wallpaper");
  // 0,0 => no size preference
  zwlr_layer_surface_v1_set_size(output->layer_surface, 0, 0);
  zwlr_layer_surface_v1_set_anchor(output->layer_surface,
    ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
    ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
  zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface, -1);
  zwlr_layer_surface_v1_add_listener(output->layer_surface,
    &layer_surface_listener, output);
  wl_surface_commit(output->surface);
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
    struct output_hack *out = calloc(1, sizeof(struct output_hack));
    wl_list_insert(&state.outputs, &out->link);
    out->state = &state;
    out->output = output;
    out->output_name = name;
    /* all other fields zerod */

    wl_output_add_listener(output, &output_listener, out);
  }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    registry_global_remove
};

static void frame_callback_done(void *data, struct wl_callback *wl_callback,
              uint32_t callback_data) {
  struct output_hack *output = data;
  Assert(output->frame_callback == wl_callback, "Frame callback did not match");
  output->frame_callback = NULL;
  wl_callback_destroy(wl_callback);
}

static const struct wl_callback_listener frame_callback_listener = {
    .done = frame_callback_done,
};

static void setup_framebuffer(struct output_hack *output) {
    glGenFramebuffers(1, &output->frameBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, output->frameBuffer);
    glGenTextures(1, &output->texColorBuffer);
    glBindTexture(GL_TEXTURE_2D, output->texColorBuffer);
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGB, output->width,  output->height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL
    );
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output->texColorBuffer, 0
    );
    glGenRenderbuffers(1, &output->rboDepthStencil);
    glBindRenderbuffer(GL_RENDERBUFFER, output->rboDepthStencil);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,  output->width,  output->height);
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, output->rboDepthStencil
    );
}

int main(int argc, char **argv) {
  char version[255];
  struct xscreensaver_function_table *ft = xscreensaver_function_table;
  progname = argv[0];   /* reset later */
  progclass = ft->progclass;

  if (ft->setup_cb)
    ft->setup_cb (ft, ft->setup_arg);

  merge_options ();

  /* Xt and xscreensaver predate the "--arg" convention, so convert
     double dashes to single. */
  {
    int i;
    for (i = 1; i < argc; i++) {
      if (argv[i][0] == '-' && argv[i][1] == '-')
        argv[i]++;
    }
  }

  /* Need to implement own command line handling here, as XtAppInitialize
   * is not available */
  int i;
  for (i = 0; merged_defaults[i]; i++) {
      add_default_resource(merged_defaults[i]);
  }
  Bool show_help = False;
  int j = 1;
  while (j < argc) {
    if (!strcmp(argv[j], "--help")) {
      show_help = True;
      break;
    }
    Bool found = False;
    int k;
    for (k = 0; k < merged_options_size; k++) {
      if (!strcmp(argv[j], merged_options[k].option)) {
        found = True;
        if (merged_options[k].argKind == XrmoptionNoArg) {
          add_cmdline_resource(merged_options[k].specifier, merged_options[k].value);
          j++;
        } else if (merged_options[k].argKind == XrmoptionSepArg) {
          if (j + 1 >= argc) {
            found = False;
            break;
          }
          add_cmdline_resource(merged_options[k].specifier, argv[j+1]);
          j += 2;
        } else {
          abort();
        }
        break;
      }
    }
    if (!found) {
      // foreach command line option
      show_help = True;
      break;
    }
  }


  {
    char *v = (char *) strdup(strchr(screensaver_id, ' '));
    char *s1, *s2, *s3, *s4;
    const char *ot = get_string_resource (NULL, "title", "Title");
    s1 = (char *) strchr(v,  ' '); s1++;
    s2 = (char *) strchr(s1, ' ');
    s3 = (char *) strchr(v,  '('); s3++;
    s4 = (char *) strchr(s3, ')');
    *s2 = 0;
    *s4 = 0;
    if (ot && !*ot) ot = 0;
    sprintf (version, "%.50s%s%s: from the XScreenSaver %s distribution (%s)",
             (ot ? ot : ""),
             (ot ? ": " : ""),
             progclass, s1, s3);
    free(v);
  }

  if (show_help)
    {
      int i;
      int x = 18;
      int end = 78;
      Bool help_p = (!strcmp(argv[1], "-help") ||
                     !strcmp(argv[1], "--help"));
      fprintf (stderr, "%s\n", version);
      fprintf (stderr, "\n\thttps://www.jwz.org/xscreensaver/\n\n");

      if (!help_p)
        fprintf(stderr, "Unrecognised option: %s\n", argv[1]);
      fprintf (stderr, "Options include: ");
      for (i = 0; i < merged_options_size; i++)
        {
          char *sw = merged_options [i].option;
          Bool argp = (merged_options [i].argKind == XrmoptionSepArg);
          int size = strlen (sw) + (argp ? 6 : 0) + 2;
          if (x + size >= end)
            {
              fprintf (stderr, "\n\t\t ");
              x = 18;
            }
          x += size;
          fprintf (stderr, "-%s", sw);  /* two dashes */
          if (argp) fprintf (stderr, " <arg>");
          if (i != merged_options_size - 1) fprintf (stderr, ", ");
        }

      fprintf (stderr, ".\n");

#if 0
      if (help_p)
        {
          fprintf (stderr, "\nResources:\n\n");
          for (i = 0; i < merged_options_size; i++)
            {
              const char *opt = merged_options [i].option;
              const char *res = merged_options [i].specifier + 1;
              const char *val = merged_options [i].value;
              char *s = get_string_resource (dpy, (char *) res, (char *) res);

              if (s)
                {
                  int L = strlen(s);
                while (L > 0 && (s[L-1] == ' ' || s[L-1] == '\t'))
                  s[--L] = 0;
                }

              fprintf (stderr, "    %-16s %-18s ", opt, res);
              if (merged_options [i].argKind == XrmoptionSepArg)
                {
                  fprintf (stderr, "[%s]", (s ? s : "?"));
                }
              else
                {
                  fprintf (stderr, "%s", (val ? val : "(null)"));
                  if (val && s && !strcasecmp (val, s))
                    fprintf (stderr, " [default]");
                }
              fprintf (stderr, "\n");
            }
          fprintf (stderr, "\n");
        }
#endif

      exit (help_p ? 0 : 1);
    }

  {
    char **s;
    for (s = merged_defaults; *s; s++)
      free(*s);
  }

  free (merged_options);
  free (merged_defaults);
  merged_options = 0;
  merged_defaults = 0;

  wl_list_init(&state.outputs);
  state.display = wl_display_connect(NULL);

  setup_egl(state.display);

  state.target_output_name = get_string_resource(NULL, "wlOutputName", "*");

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
    /* second roundtrip: receive names from the outputs; those not matching
     * will be removed from the output list */
    wl_display_roundtrip(state.display);

    if (wl_list_empty(&state.outputs)) {
       fprintf(stderr, "No output with name '%s' was found. Run `wayland-info` to see a list of outputs and their names.\n", state.target_output_name);
       return EXIT_FAILURE;
    }
  }

  state.running = True;

  /* after this roundtrip, should have received a configure event */
  wl_display_roundtrip(state.display);

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

  unsigned long delay = 1;

  int iter = 0;
  while (state.running) {
      do {
        // from old 'usleep-and-process-events'
        unsigned long quantum = 33333;  /* 30 fps */
        if (quantum > delay)
          quantum = delay;
        delay -= quantum;

    #ifdef HAVE_RECORD_ANIM
        if (anim_state) screenhack_record_anim (anim_state);
    #endif

        if (quantum > 0)
          {
            // todo: sleep with ppoll on wayland display socket
            usleep (quantum);

            // todo: apply to all outputs?
//            if (fpst) fps_slept (fpst, quantum);
    #ifdef DEBUG_PAIR
            if (fpst2) fps_slept (fpst2, quantum);
    #endif
          }

        if (wl_display_roundtrip(state.display) == -1 && errno != EINTR) {
            state.running = False;
            break;
        }
      } while (delay > 0);


    struct output_hack *output;
    wl_list_for_each(output, &state.outputs, link) {

      if (!output->egl_window) {
          if (!output->needs_ack_configure)  {
            // wait for first configure
            continue;
          }

          // setup the output
          if (output->width == 0 && output->height == 0) {
            /* (0,0) signifies client decision */
            output->width = 600;
            output->height = 400;
          }
          zwlr_layer_surface_v1_ack_configure(output->layer_surface, output->configure_serial);
          output->needs_ack_configure = False;

          output->egl_window = wl_egl_window_create(output->surface, output->width, output->height);
          output->egl_surface = eglCreateWindowSurface(state.egl_dpy, state.egl_cfg, (EGLNativeWindowType)output->egl_window, NULL);
          output->egl_context = eglCreateContext(state.egl_dpy, state.egl_cfg, EGL_NO_CONTEXT, NULL);

          if (!eglMakeCurrent(state.egl_dpy, output->egl_surface, output->egl_surface, output->egl_context)) {

            fprintf(stderr, "Failed to make a context current\n");
            return EXIT_FAILURE;
          }

	  /* Ensure that buffer swaps for output->egl_surface are not synchronized
	   * to the compositor, as this would result in blocking and round-robin
	   * updates when there are multiple outputs */
	  if (!eglSwapInterval(state.egl_dpy, 0)) {
	    fprintf(stderr, "Failed to set swap interval\n");
	    return EXIT_FAILURE;
	  }

          output->window.type = WINDOW;
          output->window.frame.x = 0;
          output->window.frame.y = 0;
          output->window.frame.width = output->width;
          output->window.frame.height = output->height;
          output->window.egl_surface = output->egl_surface;
          output->window.window.last_mouse_x = 0;
          output->window.window.last_mouse_y = 0;
          output->window.window.rh = output;

          Drawable window = &output->window;
          // this owns the EGL surface

          // this must be done after gl has been initialized
          // 'w' is a generic pointer that gets passed through
          output->display = jwxyz_gl_make_display(window);

          /* Kludge: even though the init_cb functions are declared to take 2 args,
             actually call them with 3, for the benefit of xlockmore_init() and
             xlockmore_setup().
           */
          void *(*init_cb) (Display *, Window, void *) =
            (void *(*) (Display *, Window, void *)) ft->init_cb;

          output->closure = init_cb(output->display, window, ft->setup_arg);
          output->fpst = fps_init (output->display, window);

          setup_framebuffer(output);

          glBindFramebuffer(GL_FRAMEBUFFER, output->frameBuffer);

          delay = ft->draw_cb (output->display, window, output->closure);

          jwxyz_gl_flush (output->display);
          glFinish();
          glBindFramebuffer(GL_FRAMEBUFFER, 0);

          glBlitNamedFramebuffer(output->frameBuffer,
                  0,
                    0, 0, output->window.frame.width, output->window.frame.height,
                 0, 0, output->window.frame.width, output->window.frame.height,
                  GL_COLOR_BUFFER_BIT,
                  GL_NEAREST);

          output->frame_callback = wl_surface_frame(output->surface);
          wl_callback_add_listener(output->frame_callback, &frame_callback_listener, output);

          if (!eglSwapBuffers(state.egl_dpy, output->egl_surface)) {
            fprintf(stderr, "Failed to swap buffers\n");
            return EXIT_FAILURE;
          }

          fprintf(stderr, "Have committed first buffer\n");

      } else {
          if (output->frame_callback) {
              // only redraw on this output after compositor indicated a frame is needed
              continue;
          }
          if (!eglMakeCurrent(state.egl_dpy, output->egl_surface, output->egl_surface, output->egl_context)) {
            fprintf(stderr, "Failed to make a context current\n");
            return EXIT_FAILURE;
          }

          glBindFramebuffer(GL_FRAMEBUFFER, output->frameBuffer);

          // Update hack
          if (output->needs_ack_configure) {
            // todo: other output processing
            wl_egl_window_resize(output->egl_window, output->width, output->height, 0, 0);
            output->needs_ack_configure = False;

            output->window.frame.width = output->width;
            output->window.frame.height = output->height;

            glDeleteFramebuffers(1, &output->frameBuffer);
            glDeleteTextures(1, &output->texColorBuffer);
            glDeleteRenderbuffers(1, &output->rboDepthStencil);
            setup_framebuffer(output);
            glBindFramebuffer(GL_FRAMEBUFFER, output->frameBuffer);

            ft->reshape_cb(output->display, &output->window, output->closure, output->width, output->height);
            // todo: what about framebuffer? stretch it? reset?
            fprintf(stderr, "Reshape %d %d\n", output->width, output->height);
          }

          delay = ft->draw_cb (output->display, &output->window, output->closure);
          jwxyz_gl_flush (output->display);

          glFinish();
          glBindFramebuffer(GL_FRAMEBUFFER, 0);

          // todo: combine with upscaling?
          glBlitNamedFramebuffer(output->frameBuffer,
                0,
                0, 0, output->window.frame.width, output->window.frame.height,
                0, 0, output->window.frame.width, output->window.frame.height,
                GL_COLOR_BUFFER_BIT,
                GL_NEAREST);

          output->frame_callback = wl_surface_frame(output->surface);
          wl_callback_add_listener(output->frame_callback, &frame_callback_listener, output);

            // note: swapbuffers probably moves into something called by draw_cb
          if (!eglSwapBuffers(state.egl_dpy, output->egl_surface)) {
             fprintf(stderr, "Failed to swap buffers\n");
             return EXIT_FAILURE;
          }
          // ^^ TODO: this ends up round-robin placing frames. The standard
          // fix for multimonitor is eglSwapInterval(0) and have the program
          // manage frame callbacks

      }
    }
  }

  struct output_hack *output, *tmp_output;
  wl_list_for_each_safe(output, tmp_output, &state.outputs, link) {
    output_hack_destroy(output);
  }


#ifdef HAVE_RECORD_ANIM
  if (anim_state) screenhack_record_anim_free (anim_state);
#endif

  wl_display_disconnect(state.display);
    return EXIT_SUCCESS;
}




