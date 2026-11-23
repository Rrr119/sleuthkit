bin_SCRIPTS = mactime
CLEANFILES = $(bin_SCRIPTS)
EXTRA_DIST = mactime.base .perltidyrc

AM_CPPFLAGS = -I../.. -I$(srcdir)/../..
AM_CXXFLAGS += -Wno-overloaded-virtual -Wno-unused-command-line-argument
LDADD = ../../tsk/libtsk.la
LDFLAGS += -static

bin_PROGRAMS = tsk_timeline
tsk_timeline_SOURCES = tsk_timeline.cpp

mactime: mactime.base Makefile
	@echo "#!$(PERL) -w" > mactime
	@echo "my \$$VER=\"$(VERSION)\";" >> mactime 
	@cat $(srcdir)/mactime.base >> mactime
	@chmod +x mactime 

indent:
	perltidy -b mactime.base
