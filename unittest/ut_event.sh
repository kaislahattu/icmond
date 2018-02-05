#!/bin/bash

gcc -D_GNU_SOURCE -D_DEBUG -D_UNITTEST -I../ -g -Wall -c ut_event.c         -o ut_event.o
gcc -D_GNU_SOURCE -D_DEBUG -D_UNITTEST -I../ -g -Wall -c ../event.c         -o event.o
gcc -D_GNU_SOURCE -D_DEBUG -I../ -g -Wall -c ../eventheap.c     -o eventheap.o
gcc -D_GNU_SOURCE -D_DEBUG -I../ -g -Wall -c ../daemon.c        -o daemon.o
gcc -D_GNU_SOURCE -D_DEBUG -I../ -g -Wall -c ../keyval.c        -o keyval.o
gcc -D_GNU_SOURCE -D_DEBUG -I../ -g -Wall -c ../config.c        -o config.o
gcc -D_GNU_SOURCE -D_DEBUG -I../ -g -Wall -c ../logwrite.c      -o logwrite.o
gcc -D_GNU_SOURCE -D_DEBUG -I../ -g -Wall -c ../database.c      -o database.o
gcc -D_GNU_SOURCE -D_DEBUG -I../ -g -Wall -c ../util.c          -o util.o
gcc -D_GNU_SOURCE -D_DEBUG -I../ -g -Wall -c ../power.c         -o power.o
gcc -D_GNU_SOURCE -D_DEBUG -I../ -g -Wall -c ../pidfile.c       -o pidfile.o
gcc -D_GNU_SOURCE -D_DEBUG -I../ -g -Wall -c ../user.c          -o user.o
gcc -D_GNU_SOURCE -D_DEBUG -I../ -g -Wall -c ../capability.c    -o capability.o
gcc -D_GNU_SOURCE -D_DEBUG -I../ -g -Wall -c ../datalogger.c    -o datalogger.o
gcc -D_GNU_SOURCE -D_DEBUG -I../ -g -Wall -c ../icmpecho.c      -o icmpecho.o
gcc -D_GNU_SOURCE -D_DEBUG -I../ -g -Wall -c ../version.c       -o version.o


gcc -g -Wall -lm -lrt -lcap -lsqlite3 -o event ut_event.o event.o eventheap.o \
	daemon.o config.o logwrite.o database.o util.o power.o keyval.o pidfile.o \
	user.o capability.o datalogger.o icmpecho.o version.o
