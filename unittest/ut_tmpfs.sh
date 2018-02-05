#!/bin/bash

gcc -D_GNU_SOURCE -D_DEBUG -I../ -g -Wall -c ut_tmpfs.c         -o ut_tmpfs.o
gcc -D_GNU_SOURCE -D_DEBUG -I../ -g -Wall -c ../tmpfs.c         -o tmpfs.o
gcc -D_GNU_SOURCE -D_DEBUG -I../ -g -Wall -c ../logwrite.c      -o logwrite.o
gcc -D_GNU_SOURCE -D_DEBUG -I../ -g -Wall -c ../util.c          -o util.o
gcc -D_GNU_SOURCE -D_DEBUG -I../ -g -Wall -c ../user.c          -o user.o


gcc -g -Wall -lm -lrt -o tmpfs ut_tmpfs.o tmpfs.o logwrite.o user.o util.o
