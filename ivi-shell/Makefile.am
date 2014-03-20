moduledir = $(libdir)/weston

module_LTLIBRARIES = 				\
	$(libweston_layout)

AM_CPPFLAGS =					\
	-I$(top_srcdir)/shared			\
	-I$(top_srcdir)/src			\
	-I$(top_builddir)/src			\
	-DDATADIR='"$(datadir)"'		\
	-DMODULEDIR='"$(moduledir)"'		\
	-DLIBEXECDIR='"$(libexecdir)"'		\
	-DIN_WESTON

westonincludedir = $(includedir)/weston
westoninclude_HEADERS =

if ENABLE_IVI_SHELL
westoninclude_HEADERS +=			\
	weston-layout.h

libweston_layout = libweston-layout.la
libweston_layout_la_LDFLAGS = -avoid-version
libweston_layout_la_LIBADD = $(IVI_SHELL_LIBS) ../shared/libshared.la
libweston_layout_la_CFLAGS = $(GCC_CFLAGS) $(IVI_SHELL_CFLAGS)
libweston_layout_la_SOURCES =			\
	weston-layout.c				\
	weston-layout.h

endif
