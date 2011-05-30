TESTS = azbuffer_tests malloc_tests supports_tests

INCLUDES = -I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/common/

noinst_PROGRAMS = azbuffer_tests malloc_tests supports_tests

azbuffer_tests_SOURCES = \
	azbuffer_tests.c \
	../common/azbuffer.c ../common/azbuffer.h \
	../common/sclog.c ../common/sclog.h
azbuffer_tests_LDFLAGS = -L$(libdir)
azbuffer_tests_LDADD = -lcunit

supports_tests_SOURCES = \
	supports_tests.c ../common/supports.c \
	../common/sclog.c ../common/sclog.h
supports_tests_LDFLAGS = -L$(libdir)
supports_tests_LDADD = -lcunit

malloc_tests_SOURCES = \
	malloc_tests.c \
	../common/azmalloc.c ../common/azmalloc.h \
	../common/sclog.c ../common/sclog.h
malloc_tests_LDFLAGS = -L$(libdir)
malloc_tests_LDADD = -lcunit
