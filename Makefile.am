## Process this file with automake to produce Makefile.in

COMPATNUM=@LIBTOOL_VERSION_INFO@

lib_LTLIBRARIES = libvpu_wrapper.la

libvpu_wrapper_la_SOURCES = vpu_wrapper.c

libvpu_wrapperincludedir = $(includedir)/imx-mm/vpu

libvpu_wrapperinclude_HEADERS = vpu_wrapper.h

libvpu_wrapper_la_LIBADD = $(IMX_VPU_LIB)

libvpu_wrapper_la_CFLAGS =

libvpu_wrapper_la_OBJCFLAGS =

libvpu_wrapper_la_LDFLAGS = -version-info $(COMPATNUM)

libvpu_wrapper_la_LIBTOOLFLAGS = --tag=CC

pcverfile = libvpu_wrapper.pc

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pcverfile)

EXTRA_DIST = autogen.sh

