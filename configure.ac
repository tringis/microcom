AC_PREREQ([2.63])
AC_INIT([microcom], [1.0], [tobias@ringis.se])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
dnl stop configure from automagically running automake
AM_MAINTAINER_MODE
AC_PROG_CC
AC_PROG_INSTALL
AC_CHECK_HEADERS([fcntl.h locale.h stdlib.h string.h sys/time.h termios.h unistd.h])
AC_CHECK_FUNCS([dup2 setlocale strcasecmp strerror])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
