# Makefile for mousegrab.  Throw this away and use the Imakefile if you can.
              TOP = .
      CURRENT_DIR = .
               CC = gcc
             LKED = $(CC)
          INSTALL = install
             MAKE = make
               MV = mv
               RM = rm -f
             TAGS = ctags
           MFLAGS = -$(MAKEFLAGS)
     INSTPGMFLAGS = -c -s
     INSTMANFLAGS = -c
     TOP_INCLUDES = -I$(INCROOT)
      CDEBUGFLAGS = -Wall -O2 -g
      ALLINCLUDES = $(STD_INCLUDES) $(TOP_INCLUDES) $(INCLUDES) $(EXTRA_INCLUDES)
       ALLDEFINES = $(ALLINCLUDES) $(STD_DEFINES) $(PROTO_DEFINES) $(DEFINES) $(COMPATFLAGS)
           CFLAGS = $(CDEBUGFLAGS) $(CCOPTIONS) $(ALLDEFINES)
           LDLIBS = $(SYS_LIBRARIES) $(EXTRA_LIBRARIES)
        LDOPTIONS = $(CDEBUGFLAGS) $(CCOPTIONS)
           RM_CMD = $(RM) *.CKP *.ln *.BAK *.bak *.o core errs ,* *~ *.a .emacs_* tags TAGS make.log MakeOut
         IRULESRC = $(CONFIGDIR)
        IMAKE_CMD = $(IMAKE) -DUseInstalled -I$(IRULESRC) $(IMAKE_DEFINES)
           BINDIR = $(DESTDIR)/usr/bin
          INCROOT = $(DESTDIR)/usr/include
          MANPATH = $(DESTDIR)/usr/catman/x11_man
    MANSOURCEPATH = $(MANPATH)/man
           MANDIR = $(MANSOURCEPATH)1
            IMAKE = imake
             XLIB = $(EXTENSIONLIB) -L/usr/X11R6/lib -lX11

  LOCAL_LIBRARIES = $(XLIB)

 OBJS = mousegrab.o
 SRCS = mousegrab.c

 PROGRAM = mousegrab

all:: mousegrab

mousegrab: $(OBJS) $(DEPLIBS)
	$(RM) $@
	$(LKED) -o $@ $(OBJS) $(LDOPTIONS) $(LOCAL_LIBRARIES) $(LDLIBS) $(EXTRA_LOAD_FLAGS)

mousegrab.o: mousegrab.c messages.h
	$(CC) -c $(CFLAGS) mousegrab.c

messages.h: gen_msg
	./gen_msg > $@

install:: mousegrab
	$(INSTALL) -d $(BINDIR)
	$(INSTALL) -c $(INSTPGMFLAGS)   mousegrab $(BINDIR)
install.man:: mousegrab.man
	$(INSTALL) -c $(INSTMANFLAGS) mousegrab.man $(MANDIR)/mousegrab.1
clean::
	$(RM) $(PROGRAM)
	$(RM_CMD) \#*
Makefile::
	-@if [ -f Makefile ]; then \
	echo "	$(RM) Makefile.bak; $(MV) Makefile Makefile.bak"; \
	$(RM) Makefile.bak; $(MV) Makefile Makefile.bak; \
	else exit 0; fi
	$(IMAKE_CMD) -DTOPDIR=$(TOP) -DCURDIR=$(CURRENT_DIR)
tags::
	$(TAGS) -w *.[ch]
	$(TAGS) -xw *.[ch] > TAGS

tgz:
	debian/rules clean
	(cd .. && tar -cvzf mousegrab.tgz --exclude=.git mousegrab)
