/*
 * logwrite.c - (c) 2012, 2016 Jani Tammi <janitammi@gmail.com>
 *
 *      See logwrite.h for details.
 *
 *      15.09.2016  errno logging functions now reset errno on return.
 *      22.09.2016  Added __daemon_pid to enable datalogger process message
 *                  delivery to syslog. Previously only getppid() == 1 were...
 *      01.10.2016  Added vlogmsg().
 *
 *
 *      LOG_ERR      error condition.       (defined as 3)
 *      LOG_INFO     informational message  (defined as 6)
 *      LOG_DEBUG    debug-level message    (defined as 7)
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>             // malloc()
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <time.h>               // clock_gettime()
#include <sys/types.h>

#include "logwrite.h"

/*
 * Size of the logerr message buffer.
 * Used only internally and thus not in the header file
 */
#define MSGSTR_LEN 512

// Default priority filter, changed by logwrite_init()
static int      __logwrite_priority_filter = LOG_INFO;
// Use syslog if parent pid is 1 or whatever is stored here for daemon
// Reason is that without this, datalogger process messages are not
// delivered to syslog...
static pid_t    __daemon_pid = 0;

/*
 * Set priority filter
 */
void logwrite_set_logmsg_filter(int priority_filter)
{
    __logwrite_priority_filter = priority_filter;
}
/*
 * Register daemon PID
 */
void logwrite_register_daemon_pid(pid_t daemonpid)
{
    __daemon_pid = daemonpid;
}

/*
 * Initialize logging facility
 *
 *      Syslog will always be opened, even if it is never used in the end.
 *
 * (no checks... sorry)
 *
 *  name                If NULL, Linux uses program name
 *  priority_filter     Discard messages that are of higher
 *                      numeric value than this. (In syslog,
 *                      lower value = higher priority)
 *                      USER SYSLOG.H MACROS! (LOG_ERR,
 *                      LOG_INFO, LOG_DEBUG, ...)
 */
void logwrite_init(const char *name, int priority_filter)
{
    __logwrite_priority_filter = priority_filter;
    openlog(name, LOG_PID, LOG_DAEMON); // Used to have LOG_LOCAL5 facility...
}

/*
 * _POSIX_C_SOURCE 200809L  (clock_gettime())
 * Basically the same as logerr, but prefixes the message with time
 */
static struct timespec *__devlogstarttime = NULL;
void __logdev(const char *pos, const char *fnc, const char *fmtstr, ...)
{
    int             errno_saved = errno;
    char            msgstr[MSGSTR_LEN];
    char *          writeptr;
    struct timespec now, elapsed;

    va_list args;
    va_start(args, fmtstr);

    /* Relative time will be established on fist call to this function */
    if (!__devlogstarttime)
    {
        __devlogstarttime = malloc(sizeof(struct timespec));
        clock_gettime(CLOCK_MONOTONIC, __devlogstarttime);
    }
    clock_gettime(CLOCK_MONOTONIC, &now);

    /*
     * calculate elapsed time
     */
    if (__devlogstarttime->tv_nsec > now.tv_nsec)
    {
        elapsed.tv_sec  = (now.tv_sec  - __devlogstarttime->tv_sec) - 1;
        elapsed.tv_nsec = 1000000000 - (__devlogstarttime->tv_nsec - now.tv_nsec);
    }
    else
    {
        elapsed.tv_sec  = (now.tv_sec  - __devlogstarttime->tv_sec);
        elapsed.tv_nsec = (now.tv_nsec - __devlogstarttime->tv_nsec);
    }

    writeptr  = msgstr;
    writeptr += snprintf(
                        writeptr,
                        MSGSTR_LEN,
                        "[%3ld.%03ld] ",
                        elapsed.tv_sec,
                        (elapsed.tv_nsec / 1000000)
                        );
    writeptr += snprintf(
                        writeptr,
                        MSGSTR_LEN - (writeptr - msgstr),
                        pos,            /* defined to include %s and thus a valid format string */
                        fnc
                        );
    writeptr += vsnprintf(
                        writeptr,
                        MSGSTR_LEN - (writeptr - msgstr),
                        fmtstr,
                        args
                        );

    if (errno_saved)
    {
        snprintf(
                writeptr,
                MSGSTR_LEN - (writeptr - msgstr),
                ": errno(%d) \"%s\"",
                errno_saved,
                strerror(errno_saved)
                );
    }

    /*
     * Ouput to syslog if running under init (aka. if daemon)
     */
    if (getppid() == 1 || getppid() == __daemon_pid)
        syslog(LOG_ERR, msgstr);
    else
    {
        fprintf(stderr, msgstr);
        fprintf(stderr, "\n");
    }

    va_end(args);

    errno = 0;
}

/*
 * arg1 pos     Filename and line number ("main.c:100", as parsed in header)
 * arg2 fnc     function name (where logerr() was called; "main()")
 * arg3 fmtstr  printf format string
 * ...          arguments for format string, if any
 */
void __logerr(const char *pos, const char *fnc, const char *fmtstr, ...)
{
    int     msgstr_index = 0;
    int     errno_saved;
    char    msgstr[MSGSTR_LEN];

    /* Save errno, so it won't get overrun
     */
    errno_saved = errno;

    va_list args;
    va_start(args, fmtstr);

    /*
     * (pre)Process the string
     */
    memset(msgstr, 0, MSGSTR_LEN);
    msgstr_index  = snprintf(msgstr, MSGSTR_LEN, "ERROR: ");
    msgstr_index += snprintf(msgstr + msgstr_index, MSGSTR_LEN - msgstr_index, pos, fnc);
    msgstr_index += vsnprintf(msgstr + msgstr_index, MSGSTR_LEN - msgstr_index, fmtstr, args);

    if (errno_saved)
    {
        snprintf(
                msgstr + msgstr_index,
                MSGSTR_LEN - msgstr_index,
                ": errno(%d) \"%s\"",
                errno_saved,
                strerror(errno_saved)
                );
    }

    /*
     * Ouput to syslog if running under init (aka. if daemon)
     */
    if (getppid() == 1 || getppid() == __daemon_pid)
        syslog(LOG_ERR, msgstr);
    else
    {
        fprintf(stderr, msgstr);
        fprintf(stderr, "\n");
    }

    va_end(args);
    errno = 0;
}


/*
 * Convenience function that handles log messages both in daemon mode
 * and in console mode.
 *
 * This will also observe cfg->log_level and filter out any messages
 * that have lower priority than defined by it.
 *
 * arg1 msglvl  logging priority (as in syslog.h)
 * arg2 fmtstr  printf format string
 * ...          arguments for format string, if any
 */
void logmsg(int msglvl, const char *fmtstr, ...)
{
    va_list	 args;
    int savederrno = errno;
    va_start(args, fmtstr);
    vlogmsg(msglvl, fmtstr, args);
    va_end(args);
    errno = savederrno;
    /*
    FILE    *stream;
    va_list	 args;
    int savederrno = errno;

    //
    // Log only if priority (msglvl) greater or equal to configured limit
    // NOTE: In syslog.h higher priority macros have smaller value.
    //
    if (msglvl > __logwrite_priority_filter)
        return;

    va_start(args, fmtstr);
    if (getppid() == 1 || getppid() == __daemon_pid)
    {
        vsyslog(msglvl, fmtstr, args);
    }
    else
    {
        stream = stderr;
        vfprintf(stream, fmtstr, args);
        fprintf(stream, "\n");
    }
    va_end(args);
    errno = savederrno;
    */
}

void vlogmsg(int msglvl, const char *fmtstr, va_list ap)
{
    FILE    *stream;
//    va_list	 args;
    int savederrno = errno;

    /*
     * Log only if priority (msglvl) greater or equal to configured limit
     * NOTE: In syslog.h higher priority macros have smaller value.
     */
    if (msglvl > __logwrite_priority_filter)
        return;

//    va_start(args, fmtstr);
    if (getppid() == 1 || getppid() == __daemon_pid)
    {
        vsyslog(msglvl, fmtstr, ap);
    }
    else
    {
        stream = stderr;
        vfprintf(stream, fmtstr, ap);
        fprintf(stream, "\n");
    }
//    va_end(args);
    errno = savederrno;
}

/* EOF logwrite.c */
