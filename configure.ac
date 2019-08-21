AC_PREREQ([2.57])

AC_INIT(spice-streaming-agent, 0.3,
        [spice-devel@lists.freedesktop.org])

AM_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])

AM_INIT_AUTOMAKE([dist-xz no-dist-gzip subdir-objects])

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_PROG_CC
AC_PROG_CC_C99
if test x"$ac_cv_prog_cc_c99" = xno; then
    AC_MSG_ERROR([C99 compiler is required.])
fi
AC_PROG_CXX
AC_LANG(C++)
m4_ifndef([AX_CXX_COMPILE_STDCXX_11], [m4_fatal([Missing AX_CXX_COMPILE_STDCXX_11, install autoconf-archive])])
AX_CXX_COMPILE_STDCXX_11
AC_PROG_INSTALL
AC_CANONICAL_HOST
LT_INIT([disable-static])
AM_PROG_CC_C_O
AC_C_BIGENDIAN
PKG_PROG_PKG_CONFIG

dnl =========================================================================
dnl Check deps

m4_define([SPICE_PROTOCOL_MIN_VER],[0.12.16])
m4_include([subprojects/spice-common/m4/common.m4])

PKG_CHECK_MODULES(DRM, libdrm)
PKG_CHECK_MODULES(X11, x11)
PKG_CHECK_MODULES(XFIXES, xfixes)
PKG_CHECK_MODULES(XRANDR, xrandr)

PKG_CHECK_MODULES(JPEG, libjpeg, , [
    AC_CHECK_LIB(jpeg, jpeg_destroy_decompress,
        AC_MSG_CHECKING([for jpeglib.h])
        AC_TRY_CPP(
[#include <stdio.h>
#undef PACKAGE
#undef VERSION
#undef HAVE_STDLIB_H
#include <jpeglib.h>],
            JPEG_LIBS='-ljpeg'
            AC_MSG_RESULT($jpeg_ok),
        AC_MSG_ERROR([jpeglib.h not found])),
        AC_MSG_ERROR([libjpeg not found]))
])
AC_SUBST(JPEG_LIBS)

AC_ARG_WITH(udevrulesdir,
    [AS_HELP_STRING([--with-udevrulesdir=DIR], [udev rules.d directory])],
    [UDEVRULESDIR="$withval"],
    [UDEVRULESDIR='${prefix}/lib/udev/rules.d']
)
AC_SUBST(UDEVRULESDIR)

AC_ARG_ENABLE([gst-plugin],
              AS_HELP_STRING([--enable-gst-plugin=@<:@auto/yes/no@:>@],
                             [Enable GStreamer based plugin]),,
              [enable_gst_plugin="yes"])
if test "$enable_gst_plugin" != "no"; then
    PKG_CHECK_MODULES(GST, [gstreamer-1.0 gstreamer-app-1.0], [enable_gst_plugin=yes],
        [if test "$enable_gst_plugin" = "yes"; then
             AC_MSG_ERROR([Gstreamer libs are missing])
         fi
         enable_gst_plugin=no
    ])
fi
AM_CONDITIONAL([HAVE_GST],[test "$enable_gst_plugin" = "yes"])

dnl ===========================================================================
dnl check compiler flags

SPICE_COMPILE_WARNINGS
LIBVIRT_LINKER_RELRO
LIBVIRT_LINKER_NO_INDIRECT

AC_SUBST(WARN_CFLAGS)
AC_SUBST(WARN_CXXFLAGS)

dnl =========================================================================
dnl -fvisibility stuff

have_gcc4=no
AC_MSG_CHECKING(for -fvisibility)
AC_COMPILE_IFELSE([AC_LANG_SOURCE([
#if defined(__GNUC__) && (__GNUC__ >= 4)
#else
error Need GCC 4.0 for visibility
#endif
int main () { return 0; }
])], have_gcc4=yes)

if test "x$have_gcc4" = "xyes" && test ! "$os_win32" = "yes" ; then
   VISIBILITY_HIDDEN_CFLAGS="-fvisibility=hidden"
fi
AC_MSG_RESULT($have_gcc4)
AC_SUBST(VISIBILITY_HIDDEN_CFLAGS)

dnl =========================================================================
dnl tests
AC_ARG_ENABLE([tests],
              AS_HELP_STRING([--enable-tests=@<:@yes/no@:>@],
                             [Enable tests (they require 'catch' to be installed) @<:@default=auto@:>@]),
                             [],
                             [enable_tests="auto"])
case "$enable_tests" in
  0|no) enable_tests="no" ;;
  1|yes) enable_tests="yes" ;;
  auto) enable_tests="auto" ;;
  *) AC_MSG_ERROR([bad value ${enable_tests} for enable-tests option]) ;;
esac
AS_IF([test "x$enable_tests" != "xno"],
      [AC_CHECK_HEADERS([catch/catch.hpp],  have_check="yes")
       AC_CHECK_HEADERS([catch2/catch.hpp], have_check="yes")])

AS_IF([test "x$enable_tests" = "xyes" && test "x$have_check" != "xyes"],
      [AC_MSG_ERROR([Could not find Catch dependency header (catch.hpp)])])
AM_CONDITIONAL([ENABLE_TESTS], [test "x$have_check" = "xyes"])

AC_DEFINE_DIR([BINDIR], [bindir], [Where binaries are installed.])

AC_OUTPUT([
spice-streaming-agent.spec
data/spice-streaming.desktop
Makefile
src/Makefile
src/unittests/Makefile
include/spice-streaming-agent/Makefile
spice-streaming-agent.pc
])

dnl ==========================================================================
AC_MSG_NOTICE([

        SPICE streaming agent $VERSION
        ==============================

        prefix:                   ${prefix}
        C compiler:               ${CC}
        C++ compiler:             ${CXX}
        Gstreamer plugin:         ${enable_gst_plugin}

        Now type 'make' to build $PACKAGE
])
