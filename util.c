/*
 * util.c - 2016 Jani Tammi <janitammi@gmail.com>
 */
#include <stdio.h>
#include <stdint.h>         // uint64_t
#include <unistd.h>		    // access(), euidaccess()
#include <stdlib.h>         // calloc(), realpath()
#include <stdbool.h>        // true, false
#include <string.h>         // memset()
#include <malloc.h>
#include <errno.h>
#include <ctype.h>          // isspace()
#include <limits.h>         // PATH_MAX
#include <sys/stat.h>       // struct stat 
#include <sys/timerfd.h>    // timerfd_settime()
#include <arpa/inet.h>      // inet_ntop()

#include "util.h"
#include "config.h"
#include "logwrite.h"
#include "user.h"

/*****************************************************************************/
#ifdef _UNITTEST
#include <stdarg.h>         // va_start ...
#include <string.h>         // memset()
#include <inttypes.h>       // PRId64, PRIu64, PRIx64
#endif
/*****************************************************************************/

/*
 * Made into a function to avoid out of bounds indexing
 */
#define ARRAY_SIZE(x) ( sizeof(x) / sizeof((x)[0]) ) 
char *getsignalname(int signum)
{
    static char *signalname[] =
    {
        "(null)",
        "SIGHUP",
        "SIGINT",
        "SIGQUIT",
        "SIGILL",
        "SIGTRAP",
        "SIGABRT",
        "SIGBUS",
        "SIGFPE",
        "SIGKILL",
        "SIGUSR1",
        "SIGSEGV",
        "SIGUSR2",
        "SIGPIPE",
        "SIGALRM",
        "SIGTERM",
        "SIGSTKFLT",
        "SIGCHLD",
        "SIGCONT",
        "SIGSTOP",
        "SIGTSTP",
        "SIGTTIN",
        "SIGTTOU",
        "SIGURG",
        "SIGXCPU",
        "SIGXFSZ",
        "SIGVTALRM",
        "SIGPROF",
        "SIGWINCH",
        "SIGPOLL",
        "SIGPWR",
        "SIGSYS",
        "(UNKNOWN)"
    };
    if (signum >= 0 && signum < ARRAY_SIZE(signalname) - 1)
        return signalname[signum];
    return signalname[ARRAY_SIZE(signalname) - 1];
}

/*****************************************************************************/
/* XTIMER IMPLEMENTATION *****************************************************/
/*****************************************************************************/
xtmr_t *xtmr()
{
    xtmr_t *t = calloc(1, sizeof(xtmr_t));
    clock_gettime(CLOCK_REALTIME, &t->start);
    return t;
}
double xtmrlap(xtmr_t *t)
{
    struct timespec now;
    // Record current time
    clock_gettime(CLOCK_REALTIME, &now);
    // calculate elapsed_total timespec
    if (t->start.tv_nsec > now.tv_nsec)
    {
        t->elapsed_total.tv_sec  = now.tv_sec - t->start.tv_sec - 1;
        t->elapsed_total.tv_nsec = 1000000000 - (t->start.tv_nsec - now.tv_nsec);
    }
    else
    {
        t->elapsed_total.tv_sec  = now.tv_sec  - t->start.tv_sec;
        t->elapsed_total.tv_nsec = now.tv_nsec - t->start.tv_nsec;
    }
    // calculate elapsed_lap2lap timespecs
    if (t->lap.tv_sec == 0 && t->lap.tv_nsec == 0)
    {
        // there is no previous lap time, therefor elapsed is the same as elapsed_total
        t->elapsed_lap2lap.tv_sec  = t->elapsed_total.tv_sec;
        t->elapsed_lap2lap.tv_nsec = t->elapsed_total.tv_nsec;
    }
    else
    {
        if (t->lap.tv_nsec > now.tv_nsec)
        {
            t->elapsed_lap2lap.tv_sec  = now.tv_sec - t->lap.tv_sec - 1;
            t->elapsed_lap2lap.tv_nsec = 1000000000 - (t->lap.tv_nsec - now.tv_nsec);
        }
        else
        {
            t->elapsed_lap2lap.tv_sec  = now.tv_sec  - t->lap.tv_sec;
            t->elapsed_lap2lap.tv_nsec = now.tv_nsec - t->lap.tv_nsec;
        }
    }
    t->lap.tv_sec  = now.tv_sec;
    t->lap.tv_nsec = now.tv_nsec;

#if   XTMR_RETURN_ELAPSED == XTMR_LAP2LAP
    return xtmr_getlap2lap(t);
#elif XTMR_RETURN_ELAPSED == XTMR_TOTAL
    return xtmr_gettotal(t);
#else
#error "Please specify XTMR_RETURN_ELAPSED in util.h"
#endif
}
void xtmrreset(xtmr_t *t)
{
    memset(t, 0, sizeof(xtmr_t));
    clock_gettime(CLOCK_REALTIME, &t->start);
}
/*****************************************************************************/
/* END OF XTIMER *************************************************************/
/*****************************************************************************/


/*
 * Nullify timer fd by reading it
 *
 * int flags = fcntl(fd, F_GETFL, 0);
 * fcntl(fd, F_SETFL, flags | O_NONBLOCK);
 *
 * The code snippet above will configure such a descriptor for non-blocking
 * access. If data is not available when you call read, then the system call
 * will fail with a return value of -1 and errno is set to EAGAIN.
 * See the fnctl man pages for more information.
 *
 */
void timerfd_acknowledge(int fd)
{
	uint64_t timerticks;
	int      read_result = 0;
    struct itimerspec timer_status;

    if (timerfd_gettime(fd, &timer_status))
    {
        logerr("timerfd_gettime() error");
        timerticks = 0;
        return;
    }
/* This was not a good idea after all... to be removed.
    if (timer_status.it_value.tv_sec == 0 && timer_status.it_value.tv_nsec == 0)
    {
        logdev("Attempted to read disarmed timerfd %d\n", fd);
        timerticks = 0;
        return;
    }
*/
    int ntimes = 0;
    do
    {
        read_result = read(fd, &timerticks, sizeof(timerticks));
        if (read_result == -1)
        {
            if (errno == EINTR || errno == EAGAIN)
            {
                continue;
            }
            else
            {
                logerr("read() error");
                break;
            }
        }
        ntimes++;
    } while (read_result != sizeof(timerticks));
    if (ntimes > 1)
        logdev("read() loop executed %d", ntimes);
}


/*
 * Disable timer by setting values to zero
 */
int timerfd_disarm(int fd)
{
    struct itimerspec tspec;
    tspec.it_value.tv_sec     = 0;
    tspec.it_value.tv_nsec    = 0;
    tspec.it_interval.tv_sec  = 0;
    tspec.it_interval.tv_nsec = 0;
    if (timerfd_settime(fd, 0, &tspec, NULL) == -1)
    {
        int savederrno = errno;
        logerr("timerfd_settime()");
        return (errno = savederrno, EXIT_FAILURE);
    }
    return (errno = 0, EXIT_SUCCESS);
}

static int __timerfd_start(int fd, int flags, struct itimerspec *tspec)
{
    /* Arm/Start timer
     * tspec.tv_sec -seconds from now (relative timer)
     * Stupid as it may be, there really is no macro for realative time.
     * To use relative timer, you pass "magic number" 0 as arg2.
     * (Absolute timer would be TFD_TIMER_ABSTIME.)
     */
    if (timerfd_settime(fd, flags, tspec, NULL) == -1)
    {
        logerr("timerfd_settime()");
        return(EXIT_FAILURE);
    }
    return(EXIT_SUCCESS);
}

/*
 * Enable/start timer
 */
int timerfd_start_rel(int fd, struct itimerspec *tspec)
{
    return __timerfd_start(fd, 0, tspec);
}
int timerfd_start_abs(int fd, struct itimerspec *tspec)
{
    return __timerfd_start(fd, TFD_TIMER_ABSTIME, tspec);
}

/*****************************************************************************/
// LIST AND ARRAY
/*****************************************************************************/
#define UTIL_LIST_DELIMITERS    ",;"
#define isdelim(c)  strchr(UTIL_LIST_DELIMITERS, (c))
char **str2arr(char *list)
{
    if (!list)
        return (errno = EINVAL, NULL);
    int   n = 0;
    char *ptr = (char *)list;
    // inspect every character in the string
    // n = number of delimiters
    while (*ptr)
    {
        if(isdelim(*ptr))
            n++;
        ptr++;
    }
    // number of list items is always the number of delimiters plus one
    // Now n = number of values
    n++;

    // calculate buffersize
    //      Pointer array : Pointer to each value PLUS null pointer
    //                      to terminate the list of pointers.
    //      Payload :       strlen(list) + 1 (null termination)
    char **buffer = calloc(1, (n + 1) * sizeof(char *) + strlen(list) + 1);
    memcpy(buffer + n + 1, list, strlen(list)); //  + ptrtbl

    // Point to the start of the payload in buffer;
    ptr = (char *)(buffer + n + 1);
    int idx = 0;                // pointer list index
    buffer[idx] = ptr;
    int valstrbegun = false;
    while (*ptr)
    {
        if (isdelim(*ptr))
        {
            // Big things happen
            *ptr = '\0';
            idx++;
            buffer[idx] = ptr; // initially pointer to null
            valstrbegun = false;
        }
        else if (!valstrbegun)
        {
            buffer[idx] = ptr;
            if (isspace(*ptr))
                *ptr = '\0';
            else
                valstrbegun = true;
        }
        ptr++;
    }

    // Final pass, trim the ends of the values
    for (idx = 0; buffer[idx]; idx++)
    {
        ptr = buffer[idx] + strlen(buffer[idx]) - 1;
        while(ptr > buffer[idx] && isspace(*ptr))
        {
            *ptr = '\0';
            ptr--;
        }
    }

//dump_mem(buffer, malloc_usable_size(buffer));

    return (errno = 0, buffer);
}

char *arr2str(char **array)
{
    if (!array)
        return (errno = EINVAL, NULL);
        
    // calculate necessary space
    int idx, len;
    for (idx = 0, len = 0; array[idx]; idx++)
    {
        len += strlen(array[idx]) + 1;
    }
    // reserve room for null termination
    len++;

    char *buffer = calloc(1, len);
    char *ptr = buffer;
    // compile list string
    for (idx = 0; array[idx]; idx++)
    {
        if (idx)
            ptr += sprintf(ptr, "%c", *UTIL_LIST_DELIMITERS);
        ptr += sprintf(ptr, "%s", array[idx]);
    }
//dump_mem(buffer, malloc_usable_size(buffer));

    return (errno = 0, buffer);
}

int arrlen(char **array)
{
    if (!array)
        return (errno = EINVAL, -1);
    int idx;
    for (idx = 0; array[idx]; idx++);
    return (errno = 0, idx);
}

void arrlogdev(char **array)
{
    int idx;
    for (idx = 0; array[idx]; idx++)
        logdev("[%02d] \"%s\"", idx, array[idx]);
}

int arrfindnocase(char **array, char *value)
{
    if (!array || !*array || !value)
        return (errno = EINVAL, -1);
    int index;
    // Loop until array is exhausted or match is found
    for (
        index = 0;
        array[index] && strcasecmp(value, array[index]);
        index++
        );
    if (array[index])
        return (errno = 0, index);
    return (errno = 0, -1);
}

int arrfind(char **array, char *value)
{
    if (!array || !*array || !value)
        return (errno = EINVAL, -1);
    int index;
    // Loop until array is exhausted or match is found
    for (
        index = 0;
        array[index] && strcmp(value, array[index]);
        index++
        );

    if (array[index])
        return (errno = 0, index);
    return (errno = 0, -1);
}

char **arrcollapse(char **array)
{
    int valueidx;
    int writeidx;
    if (!array)
        return (errno = EINVAL, NULL);

    for (valueidx = 0, writeidx = 0; array[valueidx]; valueidx++)
    {
        if(strlen(array[valueidx]))
        {
            if (valueidx != writeidx)
            {
                array[writeidx] = array[valueidx];
                array[valueidx] = NULL;
            }
            writeidx++;
        }
        else
            array[valueidx] = NULL;
    }
    return (errno = 0, array);
}
/*****************************************************************************/
// FILE ROUTINES
/*****************************************************************************/

/*
 * access() or similar DO NOT work when attempting to assetain the existance
 * of a file to which we do not have any access to. stat() will reveal this.
 */
int file_exist(const char *filename)
{
    struct stat s;
    if (stat(filename, &s) == 0)
        return (errno = 0, true);
    return false;
}

/*
 * RETURNS
 *      true        if "username" has "accessflags" to "filename"
 *      false       if not
 * ERRNO
 *      0           Successful test
 *      EPERM       Failed to assume effective UI (returns always "false")
 */
int file_useraccess(const char *filename, char *username, int accessflags)
{
    // If neither 'root' nor already effective user, change to it
    if (geteuid() != 0 && geteuid() != user_get_uid(username))
    {/*
        logdev(
              "Running as \"%s\". Changing to \"%s\"\n",
              user_get_ename(),
              username
              ); */
        if (user_set_eugid(username))
        {
            logerr("Failed to set effective UID (\"%s\")", username);
            return (errno = EPERM, false);
        }
    }
    // plain access() gives false positives trying to test access for
    // specified user. This is due to the way it has been defined, checking
    // against saved UID... exactly the thing we do NOT want.
    int acc = euidaccess(filename, accessflags);
//logdev("File \"%s\" for user \"%s\" : %s", filename, username, acc == 0 ? "OK" : "NOT OK");
    user_restore_eugid();
    if (acc == -1)
        return (errno = 0, false);
    return (errno = 0, true);
}

/*
 * Create path recursively
 *
 */
int mkdir_recursive(const char *path)
{
    char buffer[PATH_MAX];
    char *ptr; 

    if (strlen(path) > sizeof(buffer) - 1)
        return (errno = ENAMETOOLONG, -1);

    // Copy for local usage
    strcpy(buffer, path);

    // traverse buffer
    for (ptr = buffer + 1; *ptr; ptr++)
    {
        if (*ptr == '/')
        {
            // Temporarily truncate
            *ptr = '\0';
            if (mkdir(buffer, S_IRWXU) != 0)
                if (errno != EEXIST)
                    return -1; 
            *ptr = '/';
        }
    }

    // Final directory, one that was not identified by
    // '/' character, as above
    if (mkdir(buffer, S_IRWXU) != 0)
        if (errno != EEXIST)
            return -1; 

    return (errno = 0, 0);
}


/*****************************************************************************/
// DATA PRESENTATION
/*****************************************************************************/

/*
 * Obviously not intended for storaged conversions...
 * Use strdup(int2binstr(value)) if you need to keep the conversion.
 */
char *int2binstr(const int x)
{
    static char buffer[sizeof(int)];
	int c;
	for(c = 0; c < sizeof(int); c++)
        buffer[c] = (x & (1 << (sizeof(int) - 1 - c))) ? '1' : '0';
	return buffer;
}

unsigned int getbits(const unsigned int x, const int p, const int n)
{
	return (x >> (p + 1 - n)) & ~(~0 << n);
};

/*****************************************************************************/
// BufferString Print Formatted
// bsprintf()
// vbsprintf()
// bsprint_mem()
// bsprint_heap()
//
// bsfree();
/*****************************************************************************/
void bsfree(char **buffer)
{
    if (!buffer) // NULL
        return;
    free(*buffer);
    *buffer = NULL;
}

#define UTIL_BSPRINTF_ALLOCATION_INCREMENT     256
int vbsprintf(char **buffer, const char const *fmtstr, va_list args)
{
    // Just to get started
    if (!*buffer)
        if (!(*buffer = calloc(1, UTIL_BSPRINTF_ALLOCATION_INCREMENT)))
            return (errno = ENOMEM, -1);

    // get necessary size - C99 return gives needed chars, see notes above
    int printlen = vsnprintf(NULL, 0, fmtstr, args);

//printf(":: %d free\n:: %d + NULL to write\n", malloc_usable_size(*buffer) - strlen(*buffer), printlen);

    // Enough available or need to expand?
    if (printlen >= malloc_usable_size(*buffer) - strlen(*buffer))
        if (!(*buffer = realloc(
                                *buffer,
                                malloc_usable_size(*buffer) +
                                printlen +
                                UTIL_BSPRINTF_ALLOCATION_INCREMENT
                                )))
            return (errno = ENOMEM, -1);

    // Actual write
    vsprintf(*buffer + strlen(*buffer), fmtstr, args);
    return printlen;
}
int bsprintf(char **buffer, const char const *fmtstr, ...)
{
    va_list args;
    va_start(args, fmtstr);
    int printlen = vbsprintf(buffer, fmtstr, args);
    va_end(args);
    return printlen;
}

/*****************************************************************************/
// Buffered String Printing
/*****************************************************************************/

char *bsprint_tm(char **buffer, struct tm *tm)
{
    if (!tm)
    {
        bsprintf(buffer, "printtm(buffer, NULL)\n");
        return (errno = EINVAL, *buffer);
    }
    bsprintf(
            buffer,
            "%02d:%02d:%02d %02d.%02d.%04d%s",
            tm->tm_hour,
            tm->tm_min,
            tm->tm_sec,
            tm->tm_mday,
            tm->tm_mon + 1,
            tm->tm_year + 1900,
            tm->tm_isdst > 0 ? " DST in effect" : " DST not in effect"
            );
    return *buffer;
}

char *bsprint_time(char **buffer, const time_t t)
{
    struct tm tm;
    bsprintf(buffer, "[%10ld] local: ", t);
    localtime_r(&t, &tm);
    bsprint_tm(buffer, &tm);
    bsprintf(buffer, " system: ");
    gmtime_r(&t, &tm);
    bsprint_tm(buffer, &tm);
    return (errno = 0, *buffer);
}

char *bsprint_mem(char **buffer, void * src, int len)
{
/*       10        20        30        40        50        60        70        80
         |         |         |         |         |         |         |         |
0000 0000  68 65 6c 6c 6f 20 62 69   67 20 77 6f 72 6c 64 21   hello.bi g.world!
 * ("0F99 1023  ", total 11 characters)
 *  4   upper part of address
 *  1   space
 *  4   lower part of address
 *  2   two spaces ("  ")
 *
 * ("68 65 6c 6c 6f 20 62 69" = "hello bi", total 23 characters)
 *  2   Byte 0 in hex
 *  1   space
 *  2   Byte 1 in hex
 *  1   space
 *  2   Byte 2 in hex
 *  1   space
 *  2   Byte 3 in hex
 *  1   space
 *  2   Byte 4 in hex
 *  1   space
 *  2   Byte 5 in hex
 *  1   space
 *  2   Byte 6 in hex
 *  1   space
 *  2   Byte 7 in hex
 *
 *  3   spaces ("   ", total 3 characters)
 *
 * ("67 20 77 6f 72 6c 64 21" = "g world!", total 23 characters)
 *  2   Byte 0 in hex
 *  1   space
 *  2   Byte 1 in hex
 *  1   space
 *  2   Byte 2 in hex
 *  1   space
 *  2   Byte 3 in hex
 *  1   space
 *  2   Byte 4 in hex
 *  1   space
 *  2   Byte 5 in hex
 *  1   space
 *  2   Byte 6 in hex
 *  1   space
 *  2   Byte 7 in hex
 *
 * So far total: 11 + 23 + 3 + 23 = 60 characters (20 left)
 *  3   spaces ("   ", total of 3 characters)
 *
 * ("hello.bi g.world!", total of 17 characters)
 *  8   lower stringyfied content ("hello.bi")
 *  1   space (" ")
 *  8   upper stringyfied content ("g.world!")
 *
 * Grand total of 80 characters - just the width of standard console.
 */
#define LINE_WIDTH	80 + 1  // +1 for null termination
#define HEX_START   11      // offset where hex data 1 begins
#define STR_START   60      // offset where hex data 2 begins

    /*
     * i        stepping calculator, to process 16 bytes per
     *          displayable line.
     * ptr      traversing source and incrementing.
     * end      pre-calculated end pointer for the memory space.
     *          Used to compare to ptr, detect the end.
     * line     Buffer where output is written, one line at the time.
     */
	unsigned int     i;
	char            *ptr, *end;
	char             line[LINE_WIDTH];

    /*
     * Return if null pointer was given.
     */
	if(!(ptr = (char *)src))
    {
        logdev("NULL pointer received. Returning...");
        bsprintf(buffer, "bsprint_mem(0x%08x, (null), %d)\n", (unsigned int)buffer, len);
        return (errno = EINVAL, *buffer);
    }

	memset(&line, ' ', LINE_WIDTH);     /* fill with space ...          */
	line[LINE_WIDTH] = '\0';            /* ...and set null termination  */
	end = ptr + len;                    /* calculate end pointer        */

	/*
	 * "Rewind" ptr into closest 16byte aligned addr (less than src)
	 */
// printf("src: 0x%.8x, src mod 16: 0x%.8x\n", (unsigned int)src, (unsigned int)src % 16);
	ptr = (char *)((unsigned int)src - (unsigned int)src % 16);
// printf("src: 0x%.8x, aligned ptr: 0x%.8x\n", (unsigned int)src, (unsigned int)ptr);

	do {
        /* print address in format: "0000 0000  " */
        sprintf(
               (char *)&line,
               "%.4x %.4x  ",
               ((unsigned int)ptr >> 16) & 0xffff,
               (unsigned int)ptr & 0xffff
               );
/*        sprintf((char *)&line, "%.8X: ", ptr); // address part */
        /* 16 bytes per display line */
        for (i = 0; i < 16; i++)
        {
            /* If ptr + i is in given source range */
			if (
                (unsigned int)ptr + i >= (unsigned int)src &&
                (unsigned int)ptr + i <  (unsigned int)end
               )
            {
                /* hex entry */
				sprintf(
                       (char *)((unsigned int)&line + HEX_START + i * 3),
                       "%.2X ",
                       (unsigned char)*(ptr + i)
                       );
                /* char entry */
				sprintf(
                       (char *)((unsigned int)&line + STR_START + i),
                       "%c ",
                       isprint((int)*(ptr + i)) ? (unsigned char)*(ptr + i) : '.'
                       );
			}
            else
            /* not in displayable range */
            {
				sprintf((char *)((unsigned int)&line + HEX_START + i * 3), "   ");
				sprintf((char *)((unsigned int)&line + STR_START + i), "  ");
			}
		}
		line[STR_START - 1] = ' '; /*  clear sprintf's NULL termination */
		ptr += 16;
//		fprintf(stderr, "%s\n", (char *)&line);
//        logdev(line);
        bsprintf(buffer, "%s\n", (char *)&line);
	} while((unsigned int)ptr < (unsigned int)end);
    return *buffer;
}

/*
 * Convenience wrapper for allocated heap pointers
 */
char *bsprint_heap(char **buffer, void *address)
{
    if (!address)
    {
        logdev("NULL pointer received. Returning...");
        bsprintf(buffer, "bsprint_heap(0x%08x, (null))\n", (unsigned int)buffer);
        return (errno = EINVAL, *buffer);
    }
    return bsprint_mem(buffer, address, malloc_usable_size(address));
}

/*****************************************************************************/
#ifdef _UNITTEST
/*****************************************************************************/
/* #include <stdlib.h>
 * realpath()
 *
 * The realpath() function shall derive, from the pathname pointed to by
 * file_name, an absolute pathname that names the same file, whose resolution
 * does not involve '.', '..', or symbolic links. The generated pathname shall
 * be stored as a null-terminated string, up to a maximum of {PATH_MAX} bytes,
 * in the buffer pointed to by resolved_name.
 *
 * If resolved_name is a null pointer, the behavior of realpath() is
 * implementation-defined.
 *
 * NOTE:
 * Does not work for non-existent files, which is a challenge when one
 * wishes to create a new file.
 */

// tested to see files that effective user has no permissions for
// if it exists, do we have specified access
// if it does not exist, can we create it

int file_isreadable(const char *filename)
{
    // euidaccess(): 0 on access, -1 no specified access
    if (euidaccess(filename, R_OK))
        return false;
    return true;
}
/*
struct stat {
    dev_t     st_dev;     // ID of device containing file
    ino_t     st_ino;     // inode number
    mode_t    st_mode;    // protection
    nlink_t   st_nlink;   // number of hard links
    uid_t     st_uid;     // user ID of owner
    gid_t     st_gid;     // group ID of owner
    dev_t     st_rdev;    // device ID (if special file)
    off_t     st_size;    // total size, in bytes
    blksize_t st_blksize; // blocksize for file system I/O
    blkcnt_t  st_blocks;  // number of 512B blocks allocated
    time_t    st_atime;   // time of last access
    time_t    st_mtime;   // time of last modification
    time_t    st_ctime;   // time of last status change
};
*/
/*
 * non-existing files are accepted ONLY if create commands are issues for
 * the corresponding file.
 * 
 */
int main()
{
/*
//    char *str = "Without this datalogger process messages are not delivered to syslo";
//    char *str  = "www;sonera;com, www.utu.fi, www.posti.fi";
    char *str = "; . .   ;   .-.       ; ..... ; , , , ,";
//    char *str = ";";
//    char *str = "";
//    char *str = NULL;
    char **arr;
    if (!(arr = str2arr(str)))
    {
        printf("error parsing string!\n");
        exit(-1);
    }
    char **tmp = arr;
    while (*tmp)
    {
        printf("\"%s\"\n", *tmp);
        tmp++;
    }

    // return to list string
    char *liststring = arr2str(arr);
    printf("re-parsed list string : \"%s\"\n", liststring);
*/
    if(file_useraccess("root_only.txt", "daemon", R_OK))
    {
        printf("All OK!\n");
    }
    else
        printf("Not OK!\n");
/*
    printf("daemon UID : %d\n", user_get_uid("daemon"));
    printf("pi UID     : %d\n", user_get_uid("pi"));
    char *filename = "root_only.txt_";
    char absolute_path[CFG_MAX_FILENAME_LEN + 1];
//    if (!access(filename, R_OK))
    if (file_exist(filename))
    {
        printf("File \"%s\" exits\n", filename);
        if (!realpath(filename, absolute_path))
        {
            printf("Unable to resolve realpath()!\n");
            exit (EXIT_FAILURE);
        }
        else
        {
            printf("realpath(\"%s\"): \"%s\"\n", filename, absolute_path);
        }
    }
    else
    {
        printf("File \"%s\" does not exist\n", filename);
        if (!realpath(filename, absolute_path))
        {
            printf("Unable to resolve realpath()!\n");
            exit (EXIT_FAILURE);
        }
        else
        {
            printf("realpath(\"%s\"): \"%s\"\n", filename, absolute_path);
        }
    }
*/
    return 0;
}
#endif /* _UNITTEST */

/* EOF util.c */
