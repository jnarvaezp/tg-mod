SUBDIRS = icons
bin_PROGRAMS = tg-timer
EXTRA_PROGRAMS = tg-timer-dbg \
		 tg-timer-prf \
		 tg-timer-vlg
MOSTLYCLEANFILES = $(EXTRA_PROGRAMS)

tg_timer_SOURCES = src/algo.c \
		   src/audio.c \
		   src/computer.c \
		   src/config.c \
		   src/interface.c \
		   src/offline.c \
		   src/output_panel.c \
		   src/serializer.c \
		   src/tg.h \
		   src/wav.c \
		   src/session.c \
		   src/stats.c \
		   src/report.c

tg_timer_dbg_SOURCES = $(tg_timer_SOURCES)
tg_timer_prf_SOURCES = $(tg_timer_SOURCES)
tg_timer_vlg_SOURCES = $(tg_timer_SOURCES)

LIBS = $(GTK_LIBS) \
       $(GTHREAD_LIBS) \
       $(PORTAUDIO_LIBS) \
       $(FFTW_LIBS) \
       $(SQLITE_LIBS) \
       -lpthread \
       -lm

AM_CPPFLAGS = -DPROGRAM_NAME=\"Tg\" -DVERSION=\"$(PACKAGE_VERSION)\"
tg_timer_dbg_CPPFLAGS = $(AM_CPPFLAGS) -DDEBUG

AM_CFLAGS = $(GTK_CFLAGS) \
	    $(GTHREAD_CFLAGS) \
	    $(PORTAUDIO_CFLAGS) \
	    $(FFTW_CFLAGS) \
	    $(SQLITE_CFLAGS)
tg_timer_dbg_CFLAGS = $(AM_CFLAGS) -ggdb
tg_timer_prf_CFLAGS = $(AM_CFLAGS) -pg
tg_timer_vlg_CFLAGS = $(AM_CFLAGS) -g

tg_timer_LDFLAGS = $(AM_LDFLAGS)
tg_timer_dbg_LDFLAGS = $(AM_LDFLAGS)
tg_timer_prf_LDFLAGS = $(AM_LDFLAGS)
tg_timer_vlg_LDFLAGS = $(AM_LDFLAGS)

if BE_WINDOWS
if HAVE_WINDRES
tg_timer_SOURCES += icons/tg-timer.rc
endif
tg_timer_LDFLAGS += -mwindows
tg_timer_dbg_LDFLAGS += -mconsole
tg_timer_prf_LDFLAGS += -mconsole
tg_timer_vlg_LDFLAGS += -mconsole
endif

desktopdir = $(datadir)/applications
dist_desktop_DATA = icons/tg-timer.desktop

mimedir = $(datadir)/mime/packages
dist_mime_DATA = icons/tg-timer.xml

dist_man_MANS = docs/tg-timer.1

EXTRA_DIST = autogen.sh \
	     icons \
	     packaging \
	     LICENSE \
	     README.md

.rc.o:
	$(WINDRES) $< -O coff -o $@

test: tg-timer-dbg
	./tg-timer-dbg test
.PHONY: test

valgrind: tg-timer-vlg
	valgrind --leak-check=full -v --num-callers=99 --suppressions=.valgrind.supp ./$^
.PHONY: valgrind

check_PROGRAMS = tests/test_wav tests/test_session tests/test_dsp tests/test_stats tests/test_report tests/test_json tests/test_watchdb
tests_test_wav_SOURCES = tests/test_wav.c src/wav.c
tests_test_wav_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/src
tests_test_session_SOURCES = tests/test_session.c src/session.c
tests_test_session_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/src
tests_test_session_LDADD = -lpthread
tests_test_dsp_SOURCES = tests/test_dsp.c src/offline.c src/algo.c src/wav.c src/session.c
tests_test_dsp_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/src
tests_test_dsp_LDADD = $(FFTW_LIBS) -lpthread -lm
tests_test_stats_SOURCES = tests/test_stats.c src/stats.c
tests_test_stats_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/src
tests_test_stats_LDADD = -lpthread -lm
tests_test_report_SOURCES = tests/test_report.c src/report.c src/stats.c
tests_test_report_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/src
tests_test_report_LDADD = $(GTK_LIBS) -lpthread -lm
tests_test_json_SOURCES = tests/test_json.c src/json.c
tests_test_json_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/src
tests_test_json_LDADD = -lpthread -lm
tests_test_watchdb_SOURCES = tests/test_watchdb.c src/watchdb.c
tests_test_watchdb_CFLAGS = $(SQLITE_CFLAGS) $(AM_CFLAGS) -I$(srcdir)/src
tests_test_watchdb_LDADD = $(SQLITE_LIBS) $(GTK_LIBS) -lpthread -lm
TESTS = tests/test_wav tests/test_session tests/test_dsp tests/test_stats tests/test_report tests/test_json tests/test_watchdb
