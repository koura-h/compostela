TESTS = supports_tests

INCLUDES = -I$(top_srcdir) -I$(includedir)

noinst_PROGRAMS = supports_tests

supports_tests_SOURCES = \
	supports_tests.c ../supports.c

supports_tests_LDFLAGS = -L$(libdir)
supports_tests_LDADD = -lcunit
