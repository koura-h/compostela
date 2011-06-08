TESTS = azbuffer_tests malloc_tests supports_tests message_tests

INCLUDES = -I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/common/

noinst_PROGRAMS = azbuffer_tests malloc_tests supports_tests message_tests
AM_LDFLAGS = -L$(libdir)
LDADD = -lcunit ../common/libaz.la

azbuffer_tests_SOURCES = \
	azbuffer_tests.c

supports_tests_SOURCES = \
	supports_tests.c ../common/supports.c

malloc_tests_SOURCES = \
	malloc_tests.c \
	../common/azmalloc.c ../common/azmalloc.h

message_tests_SOURCES = \
	message_tests.c \
	../common/message.c ../common/message.h
