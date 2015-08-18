lib_LTLIBRARIES = libgstchecksumsink.la

noinst_HEADERS = gstchecksumsink.h
libgstchecksumsink_la_SOURCES = gstchecksumsink.c plugin.c
libgstchecksumsink_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_VIDEO_CFLAGS)
libgstchecksumsink_la_LIBADD = \
        $(GST_LIBS) \
        $(GST_BASE_LIBS) \
        $(GST_VIDEO_LIBS) \
        $(NULL)

libdir = ${exec_prefix}/lib/gstreamer-1.0
