dnl Process this file with autoconf to produce a configure script.
dnl Created by Anjuta application wizard.

AC_INIT(libfslvpuwrap, 1.0.44)
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

AC_CHECK_HEADERS([vpu_lib.h], [HAVE_VPU_LIB=yes], [HAVE_VPU_LIB=no])
if test "x$HAVE_VPU_LIB" = "xno"; then
    AC_MSG_ERROR([lib_vpu is required, consider installing imx-lib])
else
    IMX_VPU_LIB="-lvpu"
fi

AC_SUBST(LIBTOOL_VERSION_INFO)
AC_SUBST(IMX_VPU_LIB)

AC_OUTPUT([
Makefile
libfslvpuwrap.pc
])
