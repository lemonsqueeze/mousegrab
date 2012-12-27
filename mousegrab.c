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
// FIXME: remove unused unclutter code!
#if 0
    pexit("usage:\n\
	-display <display>\n\
	-idle <seconds>		time between polls to detect idleness.\n\
	-keystroke		wait for keystroke before idling.\n\
	-jitter <pixels>	pixels mouse can twitch without moving\n\
	-grab			use grabpointer method not createwindow\n\
	-reset			reset the timer whenever cursor becomes\n\
					visible even if it hasn't moved\n\
 	-root	       		apply to cursor on root window too\n\
	-onescreen		apply only to given screen of display\n\
 	-visible       		ignore visibility events\n\
 	-noevents      		don't send pseudo events\n\
	-regex			name or class below is a regular expression\n\
	-not names...		don't apply to windows whose wm-name begins.\n\
				(must be last argument)\n\
	-notname names...	same as -not names...\n\
	-notclass classes...    don't apply to windows whose wm-class begins.\n\
				(must be last argument, cannot be used with\n\
				-not or -notname)");
#endif
}

static void dsleep(float t)
{
    struct timeval tv;
    assert(t >= 0);
    tv.tv_sec = (int) t;
    tv.tv_usec = (t - tv.tv_sec) * 1000000;
    select(0, NULL, NULL, NULL, &tv);
}

#define ALMOSTEQUAL(a,b) (abs(a-b)<=jitter)
#define ANYBUTTON (Button1Mask|Button2Mask|Button3Mask|Button4Mask|Button5Mask)

/* Since the small window we create is a child of the window the pointer is
 * in, it can be destroyed by its adoptive parent.  Hence our destroywindow()
 * can return an error, saying it no longer exists.  Similarly, the parent
 * window can disappear while we are trying to create the child. Trap and
 * ignore these errors.
 */
int (*defaulthandler)();
int errorhandler(display,error)
Display *display;
XErrorEvent *error;
{
    if(error->error_code!=BadWindow)
	(*defaulthandler)(display,error);
    return 0;
}

char **names = 0;	/* -> argv list of names to avoid */
char **classes = 0;     /* -> argv list of classes to avoid */
regex_t *nc_re = 0;     /* regex for list of classes/names to avoid */

/*
 * return true if window has a wm_name (class) and the start of it matches
 * one of the given names (classes) to avoid
 */
int nameinlist(display,window)
Display *display;
Window window;
{
    char **cpp;
    char *name = 0;

    if(names)
	XFetchName (display, window, &name);
    else if(classes){
	XClassHint *xch = XAllocClassHint();
	if(XGetClassHint (display, window, xch))
	    name = strdup(xch->res_class);
	if(xch)
	    XFree(xch);
    }else
	return 0;

    if(name){
	if(nc_re){
	    if(!regexec(nc_re, name, 0, 0, 0)) {
		XFree(name);
		return 1;
	    }
	}else{
	    for(cpp = names!=0 ? names : classes;*cpp!=0;cpp++){
		if(strncmp(*cpp,name,strlen(*cpp))==0)
		    break;
	    }
	    XFree(name);
	    return(*cpp!=0);
	}
    }
    return 0;
}	
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

int main(int ac, char **av)
{
    Display *display;
    int screen,oldx = -99,oldy = -99,numscreens;
    int doroot = 0, jitter = 0, usegrabmethod = 1, waitagain = 0,
        dovisible = 1, doevents = 1, onescreen = 0;
    float idletime = 0.0;
    Cursor *cursor;
    Window *realroot;
    Window root;
    char *displayname = 0;
    
    progname = *av;
    for (ac--, av++;  ac;  ac--, av++)
    {
	if (!strcmp(*av, "-display") || !strcmp(*av, "-d"))
	{
	    ac--, av++;
	    if (ac < 0)
		usage();
	    displayname = *av;
	}
	else if (!strcmp(*av, "--version"))
	{
	    printf(VERSION_MSG);
	    exit(0);
	}
	else
	    usage();
#if 0    	
	if(strcmp(*av,"-idle")==0){
	    ac--,av++;
	    if(ac<0)usage();
	    idletime = atof(*av);
	}else if(strcmp(*av,"-keystroke")==0){
	    idletime = -1;
	}else if(strcmp(*av,"-jitter")==0){
	    ac--,av++;
	    if(ac<0)usage();
	    jitter = atoi(*av);
	}else if(strcmp(*av,"-noevents")==0){
	    doevents = 0;
	}else if(strcmp(*av,"-root")==0){
	    doroot = 1;
	}else if(strcmp(*av,"-grab")==0){
	    usegrabmethod = 1;
	}else if(strcmp(*av,"-reset")==0){
	    waitagain = 1;
	}else if(strcmp(*av,"-onescreen")==0){
	    onescreen = 1;
	}else if(strcmp(*av,"-visible")==0){
	    dovisible = 0;
	}else if(strcmp(*av,"-regex")==0){
	    nc_re = (regex_t *)malloc(sizeof(regex_t));
	}else if(strcmp(*av,"-not")==0 || strcmp(*av,"-notname")==0){
	    /* take rest of srg list */
	    names = ++av;
	    if(*names==0)names = 0;	/* no args follow */
	    ac = 0;
	}else if(strcmp(*av,"-notclass")==0){
	    /* take rest of arg list */
	    classes = ++av;
	    if(*classes==0)classes = 0;	/* no args follow */
	    ac = 0;
	}else 
#endif
    }
    /* compile a regex from the first name or class */
    if(nc_re){
	if(names || classes){
	    if (regcomp(nc_re, (names != 0 ? *names : *classes),
			REG_EXTENDED | REG_NOSUB)) { /* error */
		free(nc_re);
		names = classes = 0;
		nc_re = 0;
	    }
	}else{ /* -regex without -not... ... */
	    free(nc_re);
	    nc_re = 0;
	}
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
	    if(idletime<0)
		XSelectInput(display,realroot[screen],KeyReleaseMask);
	}
    screen = DefaultScreen(display);
    root = VirtualRootWindow(display,screen);

    if(!usegrabmethod)
	defaulthandler = XSetErrorHandler(errorhandler);
    /*
     * create a small unmapped window on a screen just so xdm can use
     * it as a handle on which to killclient() us.
     */
    XCreateWindow(display, realroot[screen], 0,0,1,1, 0, CopyFromParent,
		 InputOutput, CopyFromParent, 0, (XSetWindowAttributes*)0);

    printf("Grabbing mouse. Right click to ungrab.\n");
    
    while(1){
	Window dummywin,windowin,topwin,newroot;
	int rootx,rooty,winx,winy, res;
	unsigned int modifs;
	Window lastwindowavoided = None;
	
	/*
	 * wait for pointer to not move and no buttons down
	 * or if triggered by keystroke check no buttons down
	 */
	while(1){
	    if(idletime<0){		/* wait for keystroke trigger */
		XEvent event;
		do{
		    XNextEvent(display,&event);
		}while(event.type != KeyRelease ||
		       (event.xkey.state & ANYBUTTON));
		oldx = event.xkey.x_root;
		oldy = event.xkey.y_root;
	    }
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
	    }else if((!doroot && windowin==None) || (modifs & ANYBUTTON) ||
		     !(ALMOSTEQUAL(rootx,oldx) && ALMOSTEQUAL(rooty,oldy))){
		oldx = rootx, oldy = rooty;
	    }else if(windowin==None){
		windowin = root;
		break;
	    }else if(windowin!=lastwindowavoided){
		/* descend tree of windows under cursor to bottommost */
		Window childin;
		int toavoid = xFalse;
		lastwindowavoided = childin = windowin;
		do{
		    windowin = childin;
		    if(nameinlist (display, windowin)){
			toavoid = xTrue;
			break;
		    }
		}while(XQueryPointer(display, windowin, &dummywin,
		     &childin, &rootx, &rooty, &winx, &winy, &modifs)
		       && childin!=None);
		if(!toavoid){
		    lastwindowavoided = None;
		    break;
		}
	    }
	    if(idletime>=0)
		dsleep(idletime);
	}
	// printf("topwin: %x windowin: %x\n", (int)topwin, (int) windowin);
	
	/* wait again next time */
	if(waitagain)
	    oldx = -1-jitter;
	if(usegrabmethod){
	    if(XGrabPointer(display, root, 0,
		    PointerMotionMask|ButtonPressMask|ButtonReleaseMask,
		    GrabModeAsync, GrabModeAsync, None, cursor[screen],
		    CurrentTime)==GrabSuccess){
		XEvent event;
		do{
		    //Window target = topwin;    // lock on first topwin
		    Window target = windowin;
		    //Window target = event.xbutton.subwindow;

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

		}while(!(event.type == ButtonRelease &&
			     event.xbutton.button == 3));
		/* keep grabbing the mouse until right click */

		XUngrabPointer(display, CurrentTime);
		exit(0);

	    }else{
		/* go to sleep to prevent tight loops */
		if(idletime>=0)
			dsleep(idletime);
	    }
	}else{
	    XSetWindowAttributes attributes;
	    XEvent event;
	    Window cursorwindow;
	    
	    /* create small input-only window under cursor
	     * as a sub window of the window currently under the cursor
	     */
	    attributes.event_mask = LeaveWindowMask |
			EnterWindowMask |
			StructureNotifyMask |
			FocusChangeMask;
	    if(dovisible)
		attributes.event_mask |= VisibilityChangeMask;
	    attributes.override_redirect = True;
	    attributes.cursor = cursor[screen];
	    cursorwindow = XCreateWindow
		(display, windowin,
		 winx-jitter, winy-jitter,
		 jitter*2+1, jitter*2+1, 0, CopyFromParent,
		 InputOnly, CopyFromParent, 
		 CWOverrideRedirect | CWEventMask | CWCursor,
		 &attributes);
	    /* discard old events for previously created windows */
	    XSync(display,True);
	    XMapWindow(display,cursorwindow);
	    /*
	     * Dont wait for expose/map cos override and inputonly(?).
	     * Check that created window captured the pointer by looking
	     * for inevitable EnterNotify event that must follow MapNotify.
	     * [Bug fix thanks to Charles Hannum <mycroft@ai.mit.edu>]
	     */
	    XSync(display,False);
	    if(!XCheckTypedWindowEvent(display, cursorwindow, EnterNotify,
				      &event))
		oldx = -1-jitter;	/* slow down retry */
	    else{
		if(doevents){
		    /*
		     * send a pseudo EnterNotify event to the parent window
		     * to try to convince application that we didnt really leave it
		     */
		    event.xcrossing.type = EnterNotify;
		    event.xcrossing.display = display;
		    event.xcrossing.window = windowin;
		    event.xcrossing.root = root;
		    event.xcrossing.subwindow = None;
		    event.xcrossing.time = CurrentTime;
		    event.xcrossing.x = winx;
		    event.xcrossing.y = winy;
		    event.xcrossing.x_root = rootx;
		    event.xcrossing.y_root = rooty;
		    event.xcrossing.mode = NotifyNormal;
		    event.xcrossing.same_screen = True;
		    event.xcrossing.focus = True;
		    event.xcrossing.state = modifs;
		    (void)XSendEvent(display,windowin,
				     True/*propagate*/,EnterWindowMask,&event);
		}
		/* wait till pointer leaves window */
		do{
		    XNextEvent(display,&event);
		}while(event.type!=LeaveNotify &&
		       event.type!=FocusOut &&
		       event.type!=UnmapNotify &&
		       event.type!=ConfigureNotify &&
		       event.type!=CirculateNotify &&
		       event.type!=ReparentNotify &&
		       event.type!=DestroyNotify &&
		       (event.type!=VisibilityNotify ||
			event.xvisibility.state==VisibilityUnobscured)
		       );
		/* check if a second unclutter is running cos they thrash */
		if(event.type==LeaveNotify &&
		   event.xcrossing.window==cursorwindow &&
		   event.xcrossing.detail==NotifyInferior)
		    pexit("someone created a sub-window to my sub-window! giving up");
	    }
	    XDestroyWindow(display, cursorwindow);
	}
    }
}
