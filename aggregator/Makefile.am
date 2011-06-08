sbin_PROGRAMS = compostela

INCLUDES = -I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/common/

compostela_SOURCES = main.c \
	appconfig.c appconfig.h \
	../common/message.c ../common/message.h ../common/supports.h ../common/supports.c
compostela_LDADD = ../common/libaz.la
