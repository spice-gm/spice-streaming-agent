ACLOCAL_AMFLAGS = -I m4
NULL =
SUBDIRS = \
	include/spice-streaming-agent \
	subprojects/spice-common \
	src \
	$(NULL)

# this will start the program for each user session
xdgautostartdir = $(sysconfdir)/xdg/autostart
xdgautostart_DATA = $(builddir)/data/spice-streaming.desktop

# this will start the program for the login session
gdmautostartdir = $(datadir)/gdm/greeter/autostart
gdmautostart_DATA = $(builddir)/data/spice-streaming.desktop

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = spice-streaming-agent.pc

udevrulesdir = $(UDEVRULESDIR)
udevrules_DATA = $(srcdir)/data/90-spice-guest-streaming.rules

EXTRA_DIST = \
	spice-streaming-agent.spec \
	spice-streaming-agent.pc \
	data/90-spice-guest-streaming.rules \
	data/spice-streaming.desktop \
	$(NULL)

dist_man_MANS = spice-streaming-agent.1
