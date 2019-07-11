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
if USE_HANTRO_ENC_LIB
libfslvpuwrap_la_SOURCES += vpu_wrapper_hantro_encoder.c
endif
if USE_HANTRO_VC8000E_ENC_LIB
libfslvpuwrap_la_SOURCES += vpu_wrapper_hantro_VCencoder.c
endif
libfslvpuwrap_la_SOURCES += vpu_wrapper_hantro_encoder_all.c

libfslvpuwrapincludedir = $(includedir)/imx-mm/vpu

libfslvpuwrapinclude_HEADERS = vpu_wrapper.h utils.h

libfslvpuwrap_la_LIBADD =
if USE_VPU_LIB
libfslvpuwrap_la_LIBADD = $(IMX_VPU_LIB)
endif
if USE_HANTRO_LIB
libfslvpuwrap_la_LIBADD = $(IMX_HANTRO_LIB)
endif
if USE_HANTRO_ENC_LIB
libfslvpuwrap_la_LIBADD += $(IMX_HANTRO_ENC_LIB)
endif
if USE_HANTRO_VC8000E_ENC_LIB
libfslvpuwrap_la_LIBADD += $(IMX_HANTRO_VC8000E_ENC_LIB)
endif

libfslvpuwrap_la_CFLAGS =
if USE_HANTRO_LIB
libfslvpuwrap_la_CFLAGS += -DSET_OUTPUT_CROP_RECT -DUSE_EXTERNAL_BUFFER -DVSI_API -DENABLE_CODEC_VP8
libfslvpuwrap_la_CFLAGS += -I$(PKG_CONFIG_SYSROOT_DIR)/usr/include/hantro_dec
endif
if USE_HANTRO_ENC_LIB
libfslvpuwrap_la_CFLAGS += -DENCH1 -DOMX_ENCODER_VIDEO_DOMAIN -DENABLE_HANTRO_ENC
libfslvpuwrap_la_CFLAGS += -I$(PKG_CONFIG_SYSROOT_DIR)/usr/include/hantro_enc
endif
if USE_HANTRO_VC8000E_ENC_LIB
libfslvpuwrap_la_CFLAGS += -DENCVC8000E -DOMX_ENCODER_VIDEO_DOMAIN -DENABLE_HANTRO_ENC
libfslvpuwrap_la_CFLAGS += -I$(PKG_CONFIG_SYSROOT_DIR)/usr/include/hantro_VC8000E_enc
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
    Makefile_8mm    \
    Makefile_8mq    \
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
