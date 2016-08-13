include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_statsd

mod_LTLIBRARIES = mod_statsd.la
mod_statsd_la_SOURCES  = mod_statsd.c
mod_statsd_la_CFLAGS   = $(AM_CFLAGS)
mod_statsd_la_LIBADD   = $(switch_builddir)/libfreeswitch.la /usr/local/lib/libstatsdclient.so
mod_statsd_la_LDFLAGS  = -avoid-version -module -no-undefined -shared -lstatsdclient
