define([tg_version], m4_normalize(m4_include([version])))
AC_CONFIG_MACRO_DIRS([m4])

AC_INIT([Tg], [tg_version], [vacaboja@gmail.com], [tg-timer], [https://github.com/vacaboja/tg])
AM_INIT_AUTOMAKE([-Wall foreign subdir-objects])

AC_PROG_CC
AC_CHECK_LIB([pthread], [pthread_mutex_init], [], [AC_MSG_ERROR([pthread not found])])
AC_CHECK_LIB([m], [sqrt], [], [AC_MSG_ERROR([libm not found])])
PKG_CHECK_MODULES([GTK], [gtk+-3.0 glib-2.0])
PKG_CHECK_MODULES([PORTAUDIO], [portaudio-2.0])
PKG_CHECK_MODULES([FFTW], [fftw3f])
PKG_CHECK_MODULES([SQLITE], [sqlite3])

GETTEXT_PACKAGE=tg
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], ["$GETTEXT_PACKAGE"], [translation domain])
LOCALEDIR=[${datadir}/locale]
AC_SUBST(LOCALEDIR)

AC_CHECK_TOOL([WINDRES], [windres])
AM_CONDITIONAL([HAVE_WINDRES], [test x$WINDRES != x])

AM_CONDITIONAL([BE_WINDOWS], [test x$OS = xWindows_NT])
AM_COND_IF([BE_WINDOWS], [AC_CONFIG_LINKS([icons/tg-timer.ico:icons/tg-timer.ico])])

CC_CHECK_LDFLAGS([-Wl,--as-needed], [AC_SUBST([AM_LDFLAGS], [-Wl,--as-needed])], [])
CC_CHECK_CFLAGS_APPEND([-Wall -Wextra], [], [])

AC_OUTPUT([Makefile icons/Makefile])

AC_MSG_RESULT([
        $PACKAGE_NAME $VERSION
        =====

        prefix:                 ${prefix}

        compiler:               ${CC}
        cflags:                 ${CFLAGS} ${AM_CFLAGS}
        ldflags:                ${LDFLAGS} ${AM_LDFLAGS}
])
