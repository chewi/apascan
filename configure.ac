#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT(apascan, 0.1, chewi@aura-online.co.uk)
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([apascan.c])
AC_CONFIG_HEADER([config.h])

# Checks for programs.
AC_PROG_CC

# Checks for libdevmapper.
AC_CHECK_LIB(devmapper, dm_task_create, [], [have_libdm=false])
AC_CHECK_HEADERS(libdevmapper.h, [], [have_libdm=false])
if test "x${have_libdm}" == "xfalse" ; then
        AC_MSG_ERROR([libdevmapper not found. Get it from http://sourceware.org/dm.])
fi

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([fcntl.h limits.h stdlib.h string.h sys/ioctl.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_OFF_T
AC_TYPE_SSIZE_T

# Checks for library functions.
AC_PROG_GCC_TRADITIONAL
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_CHECK_FUNCS([realpath])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
