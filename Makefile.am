## Process this file with automake to produce Makefile.in

COMPATNUM=@LIBTOOL_VERSION_INFO@

lib_LTLIBRARIES = libfslvpuwrap.la

libfslvpuwrap_la_SOURCES = utils.c
if USE_VPU_LIB
libfslvpuwrap_la_SOURCES += vpu_wrapper.c
endif
if USE_HANTRO_LIB
libfslvpuwrap_la_SOURCES += vpu_wrapper_hantro.c
endif

libfslvpuwrapincludedir = $(includedir)/imx-mm/vpu

libfslvpuwrapinclude_HEADERS = vpu_wrapper.h

if USE_VPU_LIB
libfslvpuwrap_la_LIBADD = $(IMX_VPU_LIB)
endif
if USE_HANTRO_LIB
libfslvpuwrap_la_LIBADD = $(IMX_HANTRO_LIB)
endif

libfslvpuwrap_la_OBJCFLAGS =

libfslvpuwrap_la_LDFLAGS = -version-info $(COMPATNUM)

libfslvpuwrap_la_LIBTOOLFLAGS = --tag=CC

libfslvpuwrapdocdir = $(docdir)/imx-mm/vpu
libfslvpuwrapdoc_DATA = 

pcverfile = libfslvpuwrap.pc

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pcverfile)

libfslvpuwraptestdir = $(datadir)/imx-mm/video-codec/examples/vpu
libfslvpuwraptest_DATA = \
    Makefile.unit   \
    decode_stream.h \
    decode_stream.c \
    encode_stream.h \
    encode_stream.c \
    fb_render.h     \
    fb_render.c     \
    sqlite_wrapper.h    \
    sqlite_wrapper.c    \
    vpu_wrapper_timer.h \
    vpu_wrapper_timer.c \
    test_dec_arm_elinux.c   \
    test_enc_arm_elinux.c   \
    enc_auto_test.c

libfslvpuwraptestlibdir = $(datadir)/imx-mm/video-codec/examples/vpu/sqlite
libfslvpuwraptestlib_DATA = \
    sqlite/sqlite3.h    \
    sqlite/libsqlite3.a

EXTRA_DIST = autogen.sh $(libfslvpuwrapdoc_DATA) $(libfslvpuwraptest_DATA) $(libfslvpuwraptestlib_DATA)
