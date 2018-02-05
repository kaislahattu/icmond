#########################################################################
#       icmond v0.6
#  Internet Connection Monitor Daemon / Development version
#       (c) 2012 - 2016, Jani Tammi
#
# KNOWN ISSUE:
# make will happily link existing debug objects into
# release binary and vice versa. No idea how to fix it.
#
#########################################################################
ID="@(#)Makefile:0.3 -- 2012.12.04 12:42:15";
CFLAGS = -D_GNU_SOURCE
CC     = gcc
TARGET = icmond
# enter your "keyword" ('daemon') here (whatever you use for the variable)
BUILDNUM := $(shell grep -s 'daemon' version.c | sed -r 's/^.*([0-9]{8})\.([0-9]*).*\";/\2/' )
ifeq ("$(BUILDNUM)","")
	BUILDNUM := 1
else
	BUILDNUM := $(shell echo -n `expr $(BUILDNUM) + 1`)
endif

# Header directories, other than default /usr/include
# example: -I../include -I/home/pi/include
INCLUDES =

# Library directories, other than default /usr/lib
# example: -L../lib -L/home/pi/lib
LFLAGS   =

# Libraries to link into the executable
# example: -lrt -lmylib (librt.so and libmylib.so will be linked)
LIBS = -lm -lrt -lcap -lsqlite3

SOURCES = main.c config.c logwrite.c daemon.c version.c database.c user.c pidfile.c ttyinput.c keyval.c datalogger.c icmpecho.c capability.c util.c event.c power.c tmpfs.c eventheap.c

# This uses Suffix Replacement within a macro:
#   $(name:string1=string2)
OBJECTS = $(SOURCES:.c=.o)

# this is a suffix replacement rule for building .o's from .c's
# it uses automatic variables $<: the name of the prerequisite of
# the rule(a .c file) and $@: the name of the target of the rule (a .o file) 
# (see the gnu make manual section about automatic variables)
.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $<  -o $@


.PHONY: install clean version.c

all: executable

# Add debug switches
debug: CFLAGS += -D_DEBUG -g -Wall
debug: executable
debug: BUILDNUM := $(shell echo -n $(BUILDNUM) \(debugging build\) )

executable: $(OBJECTS)
	$(info Build number: $(BUILDNUM) )
	$(CC) $(LIBS) $(CFLAGS) -o $(TARGET) $(OBJECTS)

main.o: main.c main.h
	$(CC) $(CFLAGS) -c main.c

config.o: config.c config.h
	$(CC) $(CFLAGS) -c config.c

logwrite.o: logwrite.c logwrite.h
	$(CC) $(CFLAGS) -c logwrite.c

daemon.o: daemon.c daemon.h
	$(CC) $(CFLAGS) -c daemon.c

database.o: database.c database.h
	$(CC) $(CFLAGS) -c database.c

user.o: user.c user.h
	$(CC) $(CFLAGS) -c user.c

pidfile.o: pidfile.c pidfile.h
	$(CC) $(CFLAGS) -c pidfile.c

ttyinput.o: ttyinput.c ttyinput.h
	$(CC) $(CFLAGS) -c ttyinput.c

keyval.o: keyval.c keyval.h
	$(CC) $(CFLAGS) -c keyval.c

datalogger.o: datalogger.c datalogger.h
	$(CC) $(CFLAGS) -c datalogger.c

icmpecho.o: icmpecho.c icmpecho.h
	$(CC) $(CFLAGS) -c icmpecho.c

capability.o: capability.c capability.h
	$(CC) $(CFLAGS) -c capability.c

util.o: util.c util.h
	$(CC) $(CFLAGS) -c util.c

event.o: event.c event.h
	$(CC) $(CFLAGS) -c event.c

power.o: power.c power.h
	$(CC) $(CFLAGS) -c power.c

tmpfs.o: tmpfs.c tmpfs.h
	$(CC) $(CFLAGS) -c tmpfs.c

eventheap.o: eventheap.c eventheap.h
	$(CC) $(CFLAGS) -c eventheap.c

version.c:
	@echo '#include "version.h"' > $@
	@echo 'const char *daemon_build = '`date +'"%Y%m%d' | tr -d '\n'`'.$(BUILDNUM)";' >> $@

clean:
	rm -f $(TARGET) *.o *~
