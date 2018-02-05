#!/bin/bash
#
#	icmond will be executed with superuser privileges and it does NOT need
#	capabilities set in it's extended attributes. It will simply retain the
#	CAP_NET_RAW privilege while dropping other privileges.
#
#	This script supports separate development task to create ICMP echo mode.
#	Instead of dropping privileges and changing UID, the a.out binary will be
#	run with user privileges ("pi" in my development platform) and thus we
#	need the file Xattr CAP_NET_RAW privilege for it.
#

# Because of setcap command at the end
if [ "$(id -u)" != "0" ]; then
   echo "This script must be run as root" 1>&2
   exit 1
fi


 gcc -D_UNITTEST -D_DEBUG -D_GNU_SOURCE -g -Wall datalogger.c -c -o ut_datalogger.o
 gcc             -D_DEBUG -D_GNU_SOURCE -g -Wall icmpecho.c   -c -o icmpecho.o
 gcc             -D_DEBUG -D_GNU_SOURCE -g -Wall database.c   -c -o database.o
 gcc             -D_DEBUG -D_GNU_SOURCE -g -Wall keyval.c     -c -o keyval.o
 gcc             -D_DEBUG -D_GNU_SOURCE -g -Wall binary.c     -c -o binary.o
 gcc             -D_DEBUG -D_GNU_SOURCE -g -Wall logwrite.c   -c -o logwrite.o
 gcc -lm -lcap -lrt -lsqlite3 -D_DEBUG -D_GNU_SOURCE -o ut_datalogger \
	datalogger.o database.o keyval.o logwrite.o binary.o icmpecho.o

 # This is the command that requires superuser privileges
setcap cap_net_raw+epi ut_datalogger

# EOF
