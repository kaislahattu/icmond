#########################################################################
#       icmond unit tests v0.8
#  Internet Connection Monitor Daemon / Development version
#       (c) 2016, Jani Tammi
#
#########################################################################
CC       = gcc
CFLAGS   = -D_GNU_SOURCE
LIBS     = -lm -lrt -lcap -lsqlite3
INCLUDES = ../
VPATH    = ../

event:SOURCES = ut_event.c \
	event.c \
	daemon.c \
	config.c \
	logwrite.c \
	database.c \
	util.c \
	power.c \
	eventheap.c
pidfile:SOURCES = ut_pidfile.c \
	logwrite.c \
	user.c \
	pidfile.c \
	util.c

# This uses Suffix Replacement within a macro:
#   $(name:string1=string2)
OBJECTS = $(SOURCES:.c=.o)

# this is a suffix replacement rule for building .o's from .c's
# it uses automatic variables $<: the name of the prerequisite of
# the rule(a .c file) and $@: the name of the target of the rule (a .o file) 
# (see the gnu make manual section about automatic variables)
.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $<  -o $@

.PHONY: all clean

all: event pidfile

# Add debug switches
debug: CFLAGS += -D_DEBUG -g -Wall
debug: event

$(TARGET):  $(OBJECTS)
	$(CC) $(LIBS) $(CFLAGS) -o $(TARGET) $(OBJECTS)

main.o: main.c main.h
	$(CC) $(CFLAGS) -c ../main.c

config.o: config.c config.h
	$(CC) $(CFLAGS) -c ../config.c

logwrite.o: logwrite.c logwrite.h
	$(CC) $(CFLAGS) -c ../logwrite.c

daemon.o: daemon.c daemon.h
	$(CC) $(CFLAGS) -c ../daemon.c

database.o: database.c database.h
	$(CC) $(CFLAGS) -c ../database.c

user.o: user.c user.h
	$(CC) $(CFLAGS) -c ../user.c

pidfile.o: pidfile.c pidfile.h
	$(CC) $(CFLAGS) -c ../pidfile.c

ttyinput.o: ttyinput.c ttyinput.h
	$(CC) $(CFLAGS) -c ../ttyinput.c

keyval.o: keyval.c keyval.h
	$(CC) $(CFLAGS) -c ../keyval.c

datalogger.o: datalogger.c datalogger.h
	$(CC) $(CFLAGS) -c ../datalogger.c

icmpecho.o: icmpecho.c icmpecho.h
	$(CC) $(CFLAGS) -c ../icmpecho.c

capability.o: capability.c capability.h
	$(CC) $(CFLAGS) -c ../capability.c

util.o: util.c util.h
	$(CC) $(CFLAGS) -c ../util.c

event.o: event.c event.h
	$(CC) $(CFLAGS) -c ../event.c

power.o: power.c power.h
	$(CC) $(CFLAGS) -c ../power.c

tmpfs.o: tmpfs.c tmpfs.h
	$(CC) $(CFLAGS) -c ../tmpfs.c

eventheap.o: eventheap.c eventheap.h
	$(CC) $(CFLAGS) -c ../eventheap.c

version.c:
	@echo '#include "version.h"' > $@
	@echo 'const char *daemon_build = '`date +'"%Y%m%d' | tr -d '\n'`'.$(BUILDNUM)";' >> $@

clean:
	rm -f $(TARGET) *.o *~
