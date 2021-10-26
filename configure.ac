dnl Process this file with autoconf to produce a configure script.
dnl Created by Anjuta application wizard.

AC_INIT(imx-vpuwrap, 4.6.4)
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AC_CONFIG_HEADERS([config.h])
AM_MAINTAINER_MODE

PKG_PROG_PKG_CONFIG

AC_ISC_POSIX
AC_PROG_CC
AM_PROG_CC_STDC
AC_HEADER_STDC

AM_PROG_LIBTOOL

test "x$prefix" = xNONE && prefix=$ac_default_prefix

dnl *************************************************
dnl current:revision:age
dnl change (without API) = c:r+1:a
dnl change API = c+1:0:a
dnl add API = c+1:0:a+1
dnl remove API = c+1:0:0
dnl *************************************************
eval LIBTOOL_VERSION_INFO="3:0:0"

HAVE_VPU=no

AC_CHECK_HEADERS([vpu_lib.h], [HAVE_VPU_LIB=yes], [HAVE_VPU_LIB=no])
if test "x$HAVE_VPU_LIB" = "xyes"; then
    IMX_VPU_LIB="-lvpu"
    HAVE_VPU=yes
fi
AM_CONDITIONAL(USE_VPU_LIB, test "x$HAVE_VPU_LIB" = "xyes")

AC_CHECK_HEADERS([hantro_dec/dwl.h], [HAVE_HANTRO_LIB=yes], [HAVE_HANTRO_LIB=no])
if test "x$HAVE_HANTRO_LIB" = "xyes"; then
    IMX_HANTRO_LIB="-lcodec"
    HAVE_VPU=yes
fi
AM_CONDITIONAL(USE_HANTRO_LIB, test "x$HAVE_HANTRO_LIB" = "xyes")

AC_CHECK_HEADERS([hantro_enc/ewl.h], [HAVE_HANTRO_ENC_LIB=yes], [HAVE_HANTRO_ENC_LIB=no])
if test "x$HAVE_HANTRO_ENC_LIB" = "xyes"; then
    IMX_HANTRO_ENC_LIB="-lcodec_enc"
    HAVE_VPU=yes
fi
AM_CONDITIONAL(USE_HANTRO_ENC_LIB, test "x$HAVE_HANTRO_ENC_LIB" = "xyes")

AC_CHECK_HEADERS([hantro_VC8000E_enc/ewl.h], [HAVE_HANTRO_VC8000E_ENC_LIB=yes], [HAVE_HANTRO_VC8000E_ENC_LIB=no])
AC_MSG_WARN([HAVE_HANTRO_VC8000E_ENC_LIB $HAVE_HANTRO_VC8000E_ENC_LIB])
if test "x$HAVE_HANTRO_VC8000E_ENC_LIB" = "xyes"; then
   IMX_HANTRO_VC8000E_ENC_LIB="-lhantro_vc8000e"
   HAVE_VPU=yes
fi
AM_CONDITIONAL(USE_HANTRO_VC8000E_ENC_LIB, test "x$HAVE_HANTRO_VC8000E_ENC_LIB" = "xyes")

if test "x$HAVE_VPU" = "xno"; then
    AC_MSG_ERROR([no vpu headers stop building vpu_wrapper])
fi

AC_SUBST(LIBTOOL_VERSION_INFO)
AC_SUBST(IMX_VPU_LIB)
AC_SUBST(IMX_HANTRO_LIB)
AC_SUBST(IMX_AMPHION_LIB)
AC_SUBST(IMX_HANTRO_ENC_LIB)
AC_SUBST(IMX_HANTRO_VC8000E_ENC_LIB)

AC_OUTPUT([
Makefile
libfslvpuwrap.pc
])
