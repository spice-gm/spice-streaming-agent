# Makefile configuration for SPICE streaming agent
#
# \copyright
# Copyright 2016-2017 Red Hat Inc. All rights reserved.

NULL =

if ENABLE_TESTS
SUBDIRS = . unittests
endif

plugin_LTLIBRARIES =
plugindir = $(pkglibdir)/plugins

AM_CPPFLAGS = \
	-DSPICE_STREAMING_AGENT_PROGRAM \
	-I$(top_srcdir)/include \
	-DPLUGINSDIR=\"$(pkglibdir)/plugins\" \
	$(SPICE_COMMON_CFLAGS) \
	$(DRM_CFLAGS) \
	$(X11_CFLAGS) \
	$(XFIXES_CFLAGS) \
	$(XRANDR_CFLAGS) \
	$(NULL)

AM_CFLAGS = \
	-fvisibility=hidden \
	$(WARN_CFLAGS) \
	$(NULL)

AM_CXXFLAGS = \
	-fvisibility=hidden \
	-fvisibility-inlines-hidden \
	$(WARN_CXXFLAGS) \
	$(NULL)

bin_PROGRAMS = spice-streaming-agent
noinst_LIBRARIES = libstreaming-utils.a

libstreaming_utils_a_SOURCES = \
	hexdump.c \
	hexdump.h \
	$(NULL)

spice_streaming_agent_LDFLAGS = \
	-export-dynamic \
	$(RELRO_LDFLAGS) \
	$(NO_INDIRECT_LDFLAGS) \
	$(NULL)

spice_streaming_agent_LDADD = \
	-ldl \
	-lpthread \
	libstreaming-utils.a \
	$(SPICE_COMMON_DIR)/common/libspice-common.la \
	$(DRM_LIBS) \
	$(X11_LIBS) \
	$(XFIXES_LIBS) \
	$(XRANDR_LIBS) \
	$(JPEG_LIBS) \
	$(NULL)

spice_streaming_agent_SOURCES = \
	spice-streaming-agent.cpp \
	concrete-agent.cpp \
	concrete-agent.hpp \
	cursor-updater.cpp \
	cursor-updater.hpp \
	display-info.cpp \
	frame-log.cpp \
	frame-log.hpp \
	mjpeg-fallback.cpp \
	mjpeg-fallback.hpp \
	jpeg.cpp \
	jpeg.hpp \
	stream-port.cpp \
	stream-port.hpp \
	utils.cpp \
	utils.hpp \
	x11-display-info.cpp \
	$(NULL)

if HAVE_GST
plugin_LTLIBRARIES += gst-plugin.la

gst_plugin_la_LDFLAGS = \
	-module -avoid-version \
	$(RELRO_LDFLAGS) \
	$(NO_INDIRECT_LDFLAGS) \
	$(NULL)

gst_plugin_la_LIBADD = \
	$(GST_LIBS) \
	$(SPICE_COMMON_DIR)/common/libspice-common.la \
	$(NULL)

gst_plugin_la_SOURCES = \
	gst-plugin.cpp \
	$(NULL)

gst_plugin_la_CPPFLAGS = \
	-I$(top_srcdir)/include \
	$(SPICE_COMMON_CFLAGS) \
	$(GST_CFLAGS) \
	$(NULL)
endif
