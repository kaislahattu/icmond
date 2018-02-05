/*
 * util.h - 2016 Jani Tammi <janitammi@gmail.com>
 */
#include <stdio.h>      // printf()
#include <time.h>       // clock_gettime(), struct timerspec

#ifndef __UTIL_H__
#define __UTIL_H__

/*
 * Provide human readable text for signal number
 */
char *getsignalname(int signum);

/******************************************************************************
 * BufferString Print Formatted
 *
 *      Consider this the next step from asprintf() which automatically
 *      (re)allocates the necessary space for the parsed string.
 *
 *      If the previous was "a", then this is "b", as the next step,
 *      in addition to the fact that it prints into a _B_uffer.
 *
 *      Only real difference is that where as you cannot continue to print
 *      more into the asprintf() allocated string, this function will
 *      always append to the end.
 *
 *      Buffer is normal allocated heap storage and can be free()'d with
 *      normal free(). bsfree() is offered for convenience (also sets pointer
 *      to a null).
 *
 * SUPER IMPORTANT NOTE!!!!
 *
 *      When using bsprint* -functions, REMEMBER TO EITHER ALLOCATE THE INITIAL
 *      BUFFER OR SET THE NEW VARIABLE AS NULL!!!
 *
 *      char *buffer = NULL;
 */
int     vbsprintf(char **buffer, const char const *fmtstr, va_list args);
int     bsprintf(char **buffer, const char const *fmtstr, ...);
void    bsfree(char **buffer);

/******************************************************************************
 *
 * Data presentation functions
 *
 *      This set of functions will be used primarily when developing new
 *      code, to help visualise and check results and input values.
 *
 *      logdev* -functions rely on logwrite.h:logdev() function, which
 *      means that they generate output only when the binary is compiled
 *      with -D_DEBUG option.
 *
 * int2bintr()      Return a a pointer to static buffer containing and ASCII
 *                  representation of provided integer value.
 *
 * getbits()        Fetches n bits from position p within integer x.
 *                  Returns a right-aligned integer consisting of desired bits.
 *
 * logdevmem()      Old-school 4 Bytes-in-a-row representation of specified
 *                  memory pointer. Caller is responsible that specified
 *                  length of printout will not cause segmentation fault.
 *
 * logdeheap()      Same as above, but automatically fetches the size of the
 *                  allocated heap.
 *
 * USAGE
 *
 *      printf("val in binary: %s\n", int2binstr(value));
 *      getbits(0xffffabcd, 11, 8) = 0xbc
 *      logdevheap(kv);
 *
 */
char *  int2binstr(const int);
unsigned int getbits(const unsigned int x, const int p, const int n);

char *  bsprint_mem(char **buffer, void *address, int length);
char *  bsprint_heap(char **buffer, void *address);
char *  bsprint_tm(char **buffer, struct tm *tm);
char *  bsprint_time(char **buffer, const time_t t);


/******************************************************************************
 *
 * XTimer - Execution timer (ms level)
 *
 *      Simple and reasonably accurate (millisecond level) timer to measure
 *      program execution times from start to any number of "lap time" instances.
 *      Primary purpose for this timer is to serve as a development tool,
 *      helping to identify segments of code that take unreasonably long time.
 *      This solution is not suitable for high-accuracy performance measurements.
 *
 *      Functionality is based on recorded timespec data and there are no code
 *      running / consuming CPU cycles. Therefore this "stopwatch" does not
 *      need to be stopped (it's never actually running).
 *
 *      Recorded time is CLOCK_REALTIME, which means that the .tv_sec part is
 *      standard unix epoc seconds value. This can be converted into a
 *      (struct tm *) with localtime(time_t .tv_sec) or gmtime(time_t .tv_sec)
 *      functions.
 *
 *      NOTE: Elapsed time should not be displayed using struct tm.
 *      The values will likely be incorrect as soon as the elapsed time exceeds
 *      28 days - you may execute during February, you see, and the C library
 *      will calculated based on the number of days in Janauary, 1970.
 *
 * USAGE:
 *
 *      // Create new timer and mark start time 
 *      xtmr_t *dbtimer = xtmr();
 *
 *      // Take a lap time
 *      printf("%.2f milliseconds have elapsed\n", xtmrlap(dbtimer));
 *
 *      // Restart timer:
 *      xtmrreset(dbtimer);
 *
 *      // Free unwanted timer
 *      free(dbtimer);
 *
 *      // When was the timer started:
 *      struct tm *tmstart = localtime(&t->start.tv_sec);
 *      printf(
 *            "%02d.%02d.%04d %02d:%02d:%02d.%03d\n",
 *            tmstart->tm_mday,
 *            tmstart->tm_mon + 1,
 *            tmstart->tm_year + 1900,
 *            tmstart->tm_hour,
 *            tmstart->tm_min,
 *            tmstart->tm_sec,
 *            t->start.tv_nsec / 1000000
 *            );
 *
 *
 * For convenience:
 *      <time.h>:
 *      struct timespec {
 *          time_t   tv_sec;    // seconds
 *          long     tv_nsec;   // nanoseconds
 *      };
 *
 * XTMR_RETURN_ELAPSED
 *
 *      Below you can define what the xtmrlap() function returns.
 *      You can choose between the total elapsed time since the creation
 *      of the timer (XTMR_TOTAL) and the elapsed time since last lap time
 *      (XTMR_LAP2LAP).
 */
#define XTMR_RETURN_ELAPSED     XTMR_LAP2LAP // XTMR_LAP2LAP or XTMR_TOTAL

#define xtmr_getlap2lap(t) \
    ({ (t)->elapsed_lap2lap.tv_sec * 1.e3 + ((t)->elapsed_lap2lap.tv_nsec / 10000) / 1.0e2; })
#define xtmr_gettotal(t) \
    ({ t->elapsed_total.tv_sec * 1.e3 + (t->elapsed_total.tv_nsec / 10000) / 1.0e2; })
#define xtrmreport(t) \
    ({ xtmrlap((t)); printf("XTmr: %5.2f/%5.2f ms\n", xtmr_getlap2lap((t)), xtmr_gettotal((t))); })

typedef struct
{
    struct timespec start;
    struct timespec lap;
    struct timespec elapsed_lap2lap;
    struct timespec elapsed_total;
} xtmr_t;

/*
 * xtmr() - Create / start timer
 *
 *      Allocates xtmr_t and sets .start.
 *
 * USAGE:
 *          xtmr_t *t = xtmr();
 */
xtmr_t *xtmr();

/*
 * xtmrlap() - Set .lap time and calculate .elapsed* values.
 *
 *      Sets .lap and calculates .elapsed_lap2lap and .elapsed_total.
 *      and returns the lap time (in milliseconds) for total timer or
 *      since the last lap time, according to XTMR_RETURN_ELAPSED define.
 *
 * NOTE: Calculates elapsed time between this and last lap time
 *       (or between start and this lap time, if this is the first
 *       lap time taken).
 *
 * NOTE2: Please remember to use %f for printf formatting...
 *
 * USAGE:
 *          printf("%5.2f ms elapsed\n", xtmrlap(t));
 */
double xtmrlap();

/*
 * xtrmreport() - set lap time and report
 *
 *      This is a macro that can be modified to suit your personal reporting
 *      preferences (printf(), syslog() or whatever).
 *
 * USAGE:
 *          xtmrreport(t);
 */
// SEE ABOVE DEFINES FOR xtmrreport() MACRO

/*
 * xtmr_getlap2lap() - Return double value for elapsed time (in ms)
 *                     since previous lap time (or from start time,
 *                     if only one lap time has been taken).
 *
 *      This function does NOT set .lap, but merely reads the calculated
 *      .elapsed_lap2lap member and returns a double value containing the
 *      value in milliseconds.
 *
 * USAGE:
 *          printf("%5.2f / %5.2f ms elapsed\n", xtmr_getlap2lap(t), xtmr_gettotal(t));
 */

/*
 * xtmr_gettotal() - Return double value for elapsed time (in ms)
 *                   since the start time of this timer.
 *
 *      This function does NOT set .lap, but merely reads the calculated
 *      .elapsed_total -member and returns a double value containing the
 *      value in milliseconds.
 *
 * USAGE:
 *          printf("%5.2f / %5.2f ms elapsed\n", xtmr_getlap2lap(t), xtmr_gettotal(t));
 */

/*
 * xtmrreset() - Reset timer data
 *
 *      Zero out xtmr_t memory and records a new start time.
 *
 * USAGE:
 *          xtmrreset(t);
 */
void xtmrreset();

/** END OF XTIMER ************************************************************/

/******************************************************************************
 * Timer fd utilities
 *
 */

/*
 * timerfd_nullify()
 *
 *      In order to avoid immediately re-triggering pselect() for a timer,
 *      the fd needs to be read "empty".
 *
 * RETURN
 *          EXIT_SUCCESS
 *          EXIT_FAILURE
 */
void timerfd_acknowledge(int fd);

/*
 * timerfd_disable()
 *
 *      To stop a timer from triggering, a new struct itimerspec needs to be
 *      given, with all zero values. This convenience function will do that.
 *
 * RETURN
 *          EXIT_SUCCESS
 *          EXIT_FAILURE
 */
int timerfd_disarm(int fd);

/*
 * timerfd_start()
 *
 *      Sets the specified itimerspec (arg2) for the timer fd, thereby starting
 *      the timer IN RELATIVE MODE. Ie. "start no, run for .it_value -duration"
 *
 * RETURN
 *          EXIT_SUCCESS
 *          EXIT_FAILURE
 */
int timerfd_start_rel(int fd, struct itimerspec *tspec);
int timerfd_start_abs(int fd, struct itimerspec *tspec);

/******************************************************************************
 * eqlstr
 *
 *      Convenience wrappers for strcmp() and strcasecmp() -functions, allowing
 *      shorthand:
 *
 *      if (eqlstrnocase(str1, str2))
 */
#define eqlstr(a, b) \
    ( strcmp((a), (b)) ? false : true )
#define eqlstrnocase(a, b) \
    ( strcasecmp((a), (b)) ? false : true )


/******************************************************************************
 * Array functions 
 *
 *      Functions that convert delimited list strings into arrays and
 *      vice-versa plus few utility functions.
 *
 *      NOTE:   Array is single allocated buffer and unsuited for liberal
 *              editing. These functionalities are intended for relatively
 *              static data, which can be completely reparsed if need arises.
 *
 *  arrcollapse()   function will remove all emptry strings from the array
 *  arrlen()        number of items in array
 *  arrsize()       number of bytes the array takes
 */
char ** str2arr(char *list);
char *  arr2str(char **array);
int     arrlen(char **array);
int     arrfindnocase(char **array, char *value);
int     arrfind(char **array, char *value);
char ** arrcollapse(char **array);
void    arrlogdev(char **array);

/******************************************************************************
 * File routines
 *
 *      File access flags: R_OK, W_OK, X_OK and F_OK
 */
int file_exist(const char *filename);
int file_useraccess(const char *filename, char *username, int accessflags);

/*
 * Create path recursively. Relative and absolute both OK.
 *
 */
int mkdir_recursive(const char *path);


#endif /* __UTIL_H__ */

/* EOF util.h */
