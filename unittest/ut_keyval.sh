#!/bin/bash

gcc -D_GNU_SOURCE -D_DEBUG -I../ -g -Wall -c ut_keyval.c        -o ut_keyval.o
gcc -D_GNU_SOURCE -D_DEBUG -I../ -g -Wall -c ../keyval.c        -o keyval.o
gcc -D_GNU_SOURCE -D_DEBUG -I../ -g -Wall -c ../logwrite.c      -o logwrite.o
gcc -D_GNU_SOURCE -D_DEBUG -I../ -g -Wall -c ../util.c          -o util.o
gcc -D_GNU_SOURCE -D_DEBUG -I../ -g -Wall -c ../user.c          -o user.o


gcc -g -Wall -lm -lrt -o keyval ut_keyval.o keyval.o logwrite.o util.o user.o

