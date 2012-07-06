## Process this file with automake to produce Makefile.in

COMPATNUM=@LIBTOOL_VERSION_INFO@

lib_LTLIBRARIES = libfslvpuwrap.la

libfslvpuwrap_la_SOURCES = vpu_wrapper.c

libfslvpuwrapincludedir = $(includedir)/imx-mm/vpu

libfslvpuwrapinclude_HEADERS = vpu_wrapper.h

libfslvpuwrap_la_LIBADD = $(IMX_VPU_LIB)

libfslvpuwrap_la_CFLAGS =

libfslvpuwrap_la_OBJCFLAGS =

libfslvpuwrap_la_LDFLAGS = -version-info $(COMPATNUM)

libfslvpuwrap_la_LIBTOOLFLAGS = --tag=CC

libfslvpuwrapdocdir = $(docdir)/imx-mm/vpu
libfslvpuwrapdoc_DATA = \
    docs/vpu/vpu_dec_datasheet.pdf    \
    docs/vpu/vpu_enc_datasheet.pdf

pcverfile = libfslvpuwrap.pc

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pcverfile)

EXTRA_DIST = autogen.sh $(libfslvpuwrapdoc_DATA)
