/*
 * mousegrab: lock mouse to initial window and hide it.
 * right mouse button to get it back.
 *
 * Mouse will be hidden but buttons and mouse wheel still work,
 * and app can receive keystrokes.
 *
 * useful if you have an optical mouse that jitters when off the floor
 * and you'd rather not have it wander all over the place, or you need
 * to click on something without worrying about mouse being moved ...
 * Especially nice to use with easystroke -> start with mouse gesture !
 * 
 * implementation: uses XGrabPointer to grab the mouse until released,
 * so focus etc should pretty much stay where they were when it started.
 * button events are forwarded to initial window.
 *
 * based on unclutter code, by Mark M Martin.
 *
 * lemonsqueeze <lemonsqueeze@gmx.com>
 */

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <X11/Xos.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xproto.h>
#include <stdio.h>
#include "vroot.h"
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/types.h>
#include <regex.h>
#include "mousegrab.h"
#include "messages.h"

/* Globals */
char *progname = NULL;
Display *display = NULL;
int onescreen = 0, screen = 0, numscreens = 0;
Cursor *cursor = NULL;
Window *realroot = NULL;
Window root;
int rootx, rooty, winx, winy;

int need_keyboard = 0;
mykey_t *stop_key = NULL;
int stop_button = 3; /* right click by default */

const char *button_name[] =
{
  "",
  "left",
  "middle",
  "right"
};

void pexit(char *str)
{
    fprintf(stderr,"%s: %s\n",progname,str);
    exit(1);
}

void usage()
{
    printf(USAGE_MSG);
    exit(1);
}

void dsleep(float t)
{
    struct timeval tv;
    assert(t >= 0);
    tv.tv_sec = (int)t;
    tv.tv_usec = (t - tv.tv_sec) * 1000000;
    select(0, NULL, NULL, NULL, &tv);
}

#define ANYBUTTON (Button1Mask | Button2Mask | Button3Mask | Button4Mask | Button5Mask)

/*
 * create a small 1x1 curssor with all pixels masked out on the given screen.
 */
Cursor createnullcursor(Window root)
{
    Pixmap cursormask;
    XGCValues xgc;
    GC gc;
    XColor dummycolour;
    Cursor cursor;

    cursormask = XCreatePixmap(display, root, 1, 1, 1/*depth*/);
    xgc.function = GXclear;
    gc =  XCreateGC(display, cursormask, GCFunction, &xgc);
    XFillRectangle(display, cursormask, gc, 0, 0, 1, 1);
    dummycolour.pixel = 0;
    dummycolour.red = 0;
    dummycolour.flags = 04;
    cursor = XCreatePixmapCursor(display, cursormask, cursormask,
	      &dummycolour, &dummycolour, 0,0);
    XFreePixmap(display, cursormask);
    XFreeGC(display, gc);
    return cursor;
}

/************************************************************************/
/* key parsing code from xdotool */

/* human to Keysym string mapping */
const char *symbol_map[] = {
  "alt", "Alt_L",
  "ctrl", "Control_L",
  "control", "Control_L",
  "meta", "Meta_L",
  "super", "Super_L",
  "shift", "Shift_L",
  NULL, NULL,
};

int keysequence_to_keycode_list(char *keyseq, charcodemap_t **keys, int *nkeys)
{
  char *tokctx = NULL;
  const char *tok = NULL;
  char *keyseq_copy = NULL, *strptr = NULL;
  int i;
  KeyCode shift_keycode;
  
  /* Array of keys to press, in order given by keyseq */
  int keys_size = 10;
  *nkeys = 0;

  if (strcspn(keyseq, " \t\n.-[]{}\\|") != strlen(keyseq)) {
    fprintf(stderr, "Error: Invalid key sequence '%s'\n", keyseq);
    return False;
  }

  shift_keycode = XKeysymToKeycode(display, XStringToKeysym("Shift_L"));

  *keys = malloc(keys_size * sizeof(charcodemap_t));
  keyseq_copy = strptr = strdup(keyseq);
  while ((tok = strtok_r(strptr, "+", &tokctx)) != NULL) {
    KeySym sym;
    KeyCode key;

    if (strptr != NULL)
      strptr = NULL;

    /* Check if 'tok' (string keysym) is an alias to another key */
    /* symbol_map comes from xdo.util */
    for (i = 0; symbol_map[i] != NULL; i+=2)
      if (!strcasecmp(tok, symbol_map[i]))
        tok = symbol_map[i + 1];

    sym = XStringToKeysym(tok);
    if (sym == NoSymbol) {
      fprintf(stderr, "(symbol) No such key name '%s'. Ignoring it.\n", tok);
      continue;
    }

    key = XKeysymToKeycode(display, sym);
    (*keys)[*nkeys].code = key;
    if (XKeycodeToKeysym(display, key, 0) == sym) {
      (*keys)[*nkeys].shift = 0;
    } else  {
      (*keys)[*nkeys].shift = shift_keycode;
    }

    if ((*keys)[*nkeys].code == 0) {
      fprintf(stderr, "No such key '%s'. Ignoring it.\n", tok);
      continue;
    }

    (*nkeys)++;
    if (*nkeys == keys_size) {
      keys_size *= 2;
      *keys = realloc(*keys, keys_size * sizeof(KeyCode));
    }
  }

  free(keyseq_copy);

  return True;
}

/************************************************************************/

mykey_t *parse_key(char *keyseq)
{
    mykey_t *k = malloc(sizeof(mykey_t));
    k->keys = NULL;
    k->nkeys = 0;
    keysequence_to_keycode_list(keyseq, &k->keys, &k->nkeys);

    need_keyboard = 1;    
    return k;
}

int key_matches(XEvent *event, mykey_t *k)
{
    return (k && (event->xkey.keycode == k->keys[0].code));
}

/* query pointer:
 *   sets windowin to current toplevel with focus
 *        modifs to button state
 *   also updates rootx, rooty, winx, winy
 *                and root
 */
void query_pointer(Window *windowin, unsigned int *modifs)
{
    int res;
    Window newroot;
    
    for (; 1; dsleep(0.2))
    {
	res = XQueryPointer(display, root, &newroot, windowin,
			    &rootx, &rooty, &winx, &winy, modifs);
	// topwin = windowin;
	if (!res)
	{
	    /* window manager with virtual root may have restarted
	     * or we have changed screens */
	    if (!onescreen)
	    {
		for (screen = 0; screen < numscreens; screen++)
		    if (newroot == realroot[screen])
			break;
		if (screen >= numscreens)
		    pexit("not on a known screen");
	    }
	    root = VirtualRootWindow(display, screen);
	    continue;
	}
	break;
    }        
}

Window get_focus_window(void)
{
    Window windowin, dummywin;
    unsigned int modifs;    
    Window lastwindowavoided = None;    

    query_pointer(&windowin, &modifs);

    if (windowin == None)
	windowin = root;
    else if (windowin != lastwindowavoided)
    {
	/* descend tree of windows under cursor to bottommost */
	/* FIXME okay unclutter's doing this, but isn't XQueryTree() more appropriate ? */
	Window childin;
	int toavoid = xFalse;
	lastwindowavoided = childin = windowin;
	do{
	    windowin = childin;
	}while(XQueryPointer(display, windowin, &dummywin,
			     &childin, &rootx, &rooty, &winx, &winy, &modifs)
	       && childin != None);
	if (!toavoid)
	    lastwindowavoided = None;
    }

    //printf("topwin: %#x windowin: %#x\n", (int)topwin, (int) windowin);
    return windowin;
}

void forward_event(Window target, int event_mask, XEvent *event)
{
    (void)XSendEvent(display, target, 1, event_mask, event);
}

void main_loop(void);
void doit(void);

#define NEXT_ARG    do {\
	                 ac--, av++;	\
			 if (ac < 0)	\
			     usage();   \
		       } while(0)


int main(int ac, char **av)
{
    float delay = 0.0;
    char *displayname = 0;
    char *stopkeysym = 0;
    
    progname = *av;

    for (ac--, av++;  ac;  ac--, av++)
    {
	if (!strcmp(*av, "--display"))
	{
	    NEXT_ARG;
	    displayname = *av;
	}
	else if (!strcmp(*av, "--delay") || !strcmp(*av, "-d"))
	{
	    NEXT_ARG;
	    delay = atof(*av);
	}
	else if (!strcmp(*av, "--key"))
	{
	    NEXT_ARG;
	    stopkeysym = *av;
	}	
	else if (!strcmp(*av, "--version"))
	{
	    printf(VERSION_MSG);
	    exit(0);
	}
	else
	    usage();
    }

    display = XOpenDisplay(displayname);
    if (display == 0)
	pexit("could not open display");
    numscreens = ScreenCount(display);
    cursor = (Cursor*) malloc(numscreens*sizeof(Cursor));
    realroot = (Window*) malloc(numscreens*sizeof(Window));

    /* each screen needs its own empty cursor.
     * note each real root id so can find which screen we are on
     */
    for (screen = 0; screen < numscreens; screen++)
	if (onescreen && screen != DefaultScreen(display)){
	    realroot[screen] = -1;
	    cursor[screen] = -1;
	}else{
	    realroot[screen] = XRootWindow(display, screen);
	    cursor[screen] = createnullcursor(realroot[screen]);
	}
    screen = DefaultScreen(display);
    root = VirtualRootWindow(display, screen);

    /*
     * create a small unmapped window on a screen just so xdm can use
     * it as a handle on which to killclient() us.
     */
    XCreateWindow(display, realroot[screen], 0, 0, 1, 1, 0, CopyFromParent,
		 InputOutput, CopyFromParent, 0, (XSetWindowAttributes*)0);

    if (stopkeysym)
    {
	stop_key = parse_key(stopkeysym);
	stop_button = 0;
    }
    
    if (delay != 0.0)
	dsleep(delay);

    if (stop_button)
	printf("Grabbing mouse. %s click to ungrab.\n", button_name[stop_button]);
    else
	printf("Grabbing mouse. press %s to ungrab.\n", stopkeysym);
    
    doit();
    return 0;
}

void doit(void)
{
    Window windowin;
    unsigned int modifs;

    /* wait for no buttons */
    do { query_pointer(&windowin, &modifs); }
    while (modifs & ANYBUTTON);

    if (XGrabPointer(display, root, 0,
		     PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
		     GrabModeAsync, GrabModeAsync, None, cursor[screen],
		     CurrentTime) != GrabSuccess)
	pexit("Couldn't grab pointer.");

    if (need_keyboard)
	if (XGrabKeyboard(display, root, 0,
			  GrabModeAsync, GrabModeAsync, CurrentTime) != GrabSuccess)
	    pexit("Couldn't grab keyboard.");
    
    main_loop();
}

void main_loop(void)
{
    XEvent event;
    do{
	XNextEvent(display,&event);

	// Forward button clicks to initial window
	if ((event.type == ButtonPress ||
	     event.type == ButtonRelease) &&
	    event.xbutton.button != stop_button)
	{
	    Window target = get_focus_window();	    
	    event.xbutton.window = target;
	    event.xbutton.subwindow = target;
	    event.xbutton.x = winx;
	    event.xbutton.y = winy;
	    event.xbutton.x_root = rootx;
	    event.xbutton.y_root = rooty;
	    forward_event(target, (event.type == ButtonPress ? ButtonPressMask : ButtonReleaseMask), &event);
	}

	// Forward key events to initial window
	if ((event.type == KeyPress ||
	     event.type == KeyRelease) &&
	    !key_matches(&event, stop_key))
	{
	    Window target = get_focus_window();	    
	    event.xkey.window = target;
	    event.xkey.subwindow = target;
	    event.xkey.x = winx;
	    event.xkey.y = winy;
	    event.xkey.x_root = rootx;
	    event.xkey.y_root = rooty;
	    forward_event(target, (event.type == KeyPress ? KeyPressMask : KeyReleaseMask), &event);
	}	

    } while (!(event.type == KeyRelease &&
	       key_matches(&event, stop_key)) &&
	     !(event.type == ButtonRelease &&
	       event.xbutton.button == stop_button));
    
    XUngrabPointer(display, CurrentTime);
    if (need_keyboard)
	XUngrabKeyboard(display, CurrentTime);
}
