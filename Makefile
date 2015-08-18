CFLAGS=-g -Wall
GST_CFLAGS=$(shell pkg-config --cflags gstreamer-1.0 gstreamer-base-1.0 gstreamer-video-1.0)
GST_LIBS=$(shell pkg-config --libs gstreamer-1.0 gstreamer-base-1.0 gstreamer-video-1.0)

PLUGIN = libgstchecksumsink.so

$(PLUGIN) : plugin.o gstchecksumsink.o
	gcc $(CFLAGS) $(GST_CFLAGS) -shared -fPIC -o $@ $^ $(GST_LIBS)

%.o : %.c gstchecksumsink.h
	gcc $(CFLAGS) $(GST_CFLAGS) -fPIC -c $< -o $@

.PHONY : clean

clean :
	rm -rf  plugin.o gstchecksumsink.o libgstchecksumsink.so
