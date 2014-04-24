AM_CFLAGS = -Wall
AM_LDFLAGS = -rdynamic
AM_CPPFLAGS = -I$(top_srcdir)/common -I$(top_srcdir)/ppr

nobase_include_HEADERS = paf/ppr.h
INTERNAL_INCLUDES = hwcap.h \
                    common.h

man3_MANS = doc/libpaf-ppr.3

TESTS = test_ppr
test_ppr_SOURCES = tests/test_ppr.c

noinst_PROGRAMS = $(TESTS)
