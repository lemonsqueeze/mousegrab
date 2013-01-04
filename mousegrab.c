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
#include "messages.h"

char *progname;
void pexit(str)char *str;{
    fprintf(stderr,"%s: %s\n",progname,str);
    exit(1);
}
void usage(){
    printf(USAGE_MSG);
    exit(1);
}

void main_loop(Display *display, Window windowin,
	       int winx, int winy, int rootx, int rooty);
void doit(Display *display, Window root, Window *realroot,
	  Cursor *cursor, int screen, int onescreen, int numscreens);

static void dsleep(float t)
{
    struct timeval tv;
    assert(t >= 0);
    tv.tv_sec = (int) t;
    tv.tv_usec = (t - tv.tv_sec) * 1000000;
    select(0, NULL, NULL, NULL, &tv);
}

#define ANYBUTTON (Button1Mask|Button2Mask|Button3Mask|Button4Mask|Button5Mask)

/*
 * create a small 1x1 curssor with all pixels masked out on the given screen.
 */
Cursor createnullcursor(display,root)
Display *display;
Window root;
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
	      &dummycolour,&dummycolour, 0,0);
    XFreePixmap(display,cursormask);
    XFreeGC(display,gc);
    return cursor;
}

#define NEXT_ARG    do {\
	                 ac--, av++;	\
			 if (ac < 0)	\
			     usage();   \
		       } while(0)

int main(int ac, char **av)
{
    Display *display;
    int screen,numscreens;
    int onescreen = 0;
    float delay = 0.0;
    Cursor *cursor;
    Window *realroot;
    Window root;
    char *displayname = 0;
    
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
	else if (!strcmp(*av, "--version"))
	{
	    printf(VERSION_MSG);
	    exit(0);
	}
	else
	    usage();
    }

    display = XOpenDisplay(displayname);
    if(display==0)pexit("could not open display");
    numscreens = ScreenCount(display);
    cursor = (Cursor*) malloc(numscreens*sizeof(Cursor));
    realroot = (Window*) malloc(numscreens*sizeof(Window));

    /* each screen needs its own empty cursor.
     * note each real root id so can find which screen we are on
     */
    for(screen = 0;screen<numscreens;screen++)
	if(onescreen && screen!=DefaultScreen(display)){
	    realroot[screen] = -1;
	    cursor[screen] = -1;
	}else{
	    realroot[screen] = XRootWindow(display,screen);
	    cursor[screen] = createnullcursor(display,realroot[screen]);
	}
    screen = DefaultScreen(display);
    root = VirtualRootWindow(display,screen);

    /*
     * create a small unmapped window on a screen just so xdm can use
     * it as a handle on which to killclient() us.
     */
    XCreateWindow(display, realroot[screen], 0,0,1,1, 0, CopyFromParent,
		 InputOutput, CopyFromParent, 0, (XSetWindowAttributes*)0);

    if (delay != 0.0)
	dsleep(delay);
    printf("Grabbing mouse. Right click to ungrab.\n");
    
    doit(display, root, realroot, cursor, screen, onescreen, numscreens);
    return 0;
}

void doit(Display *display, Window root, Window *realroot,
	  Cursor *cursor, int screen, int onescreen, int numscreens)
{
    Window dummywin,windowin,topwin,newroot;
    int rootx,rooty,winx,winy, res;
    unsigned int modifs;
    Window lastwindowavoided = None;

    /* wait for no buttons down */
    for (; 1; dsleep(0.2))
    {
	res = XQueryPointer(display, root, &newroot, &windowin,
			    &rootx, &rooty, &winx, &winy, &modifs);
	topwin = windowin;
	if (!res)
	{
	    /* window manager with virtual root may have restarted
	     * or we have changed screens */
	    if(!onescreen){
		for(screen = 0;screen<numscreens;screen++)
		    if(newroot==realroot[screen])break;
		if(screen>=numscreens)
		    pexit("not on a known screen");
	    }
	    root = VirtualRootWindow(display,screen);
	    continue;
	}
	if (modifs & ANYBUTTON)
	    continue;
	break;
    }

    if (windowin==None)
	windowin = root;
    else if(windowin!=lastwindowavoided)
    {
	/* descend tree of windows under cursor to bottommost */
	Window childin;
	int toavoid = xFalse;
	lastwindowavoided = childin = windowin;
	do{
	    windowin = childin;
	}while(XQueryPointer(display, windowin, &dummywin,
			     &childin, &rootx, &rooty, &winx, &winy, &modifs)
	       && childin!=None);
	if(!toavoid)
	    lastwindowavoided = None;
    }

    // printf("topwin: %x windowin: %x\n", (int)topwin, (int) windowin);
	
    if (XGrabPointer(display, root, 0,
		     PointerMotionMask|ButtonPressMask|ButtonReleaseMask,
		     GrabModeAsync, GrabModeAsync, None, cursor[screen],
		     CurrentTime) != GrabSuccess)
	pexit("Couldn't grab pointer.");
	
    main_loop(display, windowin, winx, winy, rootx, rooty);    
}

void main_loop(Display *display, Window windowin,
	       int winx, int winy, int rootx, int rooty)
{
    XEvent event;
    do{
	Window target = windowin;
		    
	// TODO: exit if target window gets destroyed...
		    
	XNextEvent(display,&event);

	// Forward button clicks to initial window
	if ((event.type == ButtonPress ||
	     event.type == ButtonRelease) &&
	    event.xbutton.button != 3)
	{
	    event.xbutton.window = target;
	    event.xbutton.subwindow = target;
	    event.xbutton.x = winx;
	    event.xbutton.y = winy;
	    event.xbutton.x_root = rootx;
	    event.xbutton.y_root = rooty;
	    (void)XSendEvent(display, target,
			     1,
			     (event.type == ButtonPress ? ButtonPressMask : ButtonReleaseMask),
			     &event);
	}
	
    /* keep grabbing the mouse until right click */
    } while (!(event.type == ButtonRelease &&
	       event.xbutton.button == 3));

    XUngrabPointer(display, CurrentTime);
}
