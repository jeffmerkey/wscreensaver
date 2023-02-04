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

#define DEBUG_PAIR

#include <stdio.h>


#include "screenhackI.h"
// #include "xmu.h"
#include "version.h"
// #include "vroot.h"
#include "fps.h"

#ifdef HAVE_RECORD_ANIM
# include "recanim.h"
#endif

// #ifndef _XSCREENSAVER_VROOT_H_
// # error Error!  You have an old version of vroot.h!  Check -I args.
// #endif /* _XSCREENSAVER_VROOT_H_ */

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

int main(int argc, char **argv) {
    /* TODO: implement this. Also define 'get_float_resource/get_integer_resource/get_string_resource/get_boolean_resource' */
    
    
}




