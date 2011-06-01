sbin_PROGRAMS = comfollower
bin_PROGRAMS = comlogger

INCLUDES = -I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/common/

comfollower_SOURCES = main.c \
	appconfig.c appconfig.h \
	connection.c connection.h \
	follow_context.c follow_context.h \
	../common/message.c ../common/message.h ../common/supports.h ../common/supports.c
comfollower_LDADD = ../common/libaz.la

comlogger_SOURCES = logger.c

comlogger_LDADD = ../common/libaz.la
