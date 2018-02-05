 gcc -D_UNITTEST -D_DEBUG -D_GNU_SOURCE -g -Wall util.c       -c -o ut_util.o
 gcc             -D_DEBUG -D_GNU_SOURCE -g -Wall user.c       -c -o user.o
 gcc             -D_DEBUG -D_GNU_SOURCE -g -Wall binary.c     -c -o binary.o
 gcc             -D_DEBUG -D_GNU_SOURCE -g -Wall logwrite.c   -c -o logwrite.o
 gcc -lm -lcap -lrt -lsqlite3 -D_DEBUG -D_GNU_SOURCE -o ut_util \
	ut_util.o logwrite.o binary.o user.o

