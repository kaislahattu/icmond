/*
 * pidfile.c - 2016, Jani Tammi <janitammi@gmail.com>
 *
 *
 * IMPORTANT
 *
 *      Child processes never inherit file locks!
 *      Actual daemon process needs to call these functions.
 *
 *
 * Due to the nature of this daemon, to monitor a physical device
 * (extremely unlikely that multiple of Cisco DOCSIS modems are
 * in operation in one location!), this program DOES NOT ALLOW
 * multiple instances at all, even under different users!
 *
 * user who runs the daemon need not be root (and then "daemon"),
 * but whoever runs it, is going to run the single one allowed.
 */
#include <stdio.h>          /* sprintf()                            */
#include <stdlib.h>         /* EXIT_FAILURE, EXIT_SUCCESS           */
#include <string.h>         /* strlen()                             */
#include <sys/stat.h>       /* S_IRUSR, S_IWUSR                     */
#include <unistd.h>         /* lockf()                              */
#include <fcntl.h>          /* open()                               */
#include <limits.h>

#include "pidfile.h"
#include "config.h"
#include "logwrite.h"

static struct
{
    int   handle;
    char  name[NAME_MAX];    /* NAME_MAX in limits.h         */
    int   buffer_size;
} pidfile = {
    .handle      = 0,
    .buffer_size = NAME_MAX
};

/*
 * pidfile_lock()
 *
 *      Standard process lock/pid file implementation.
 *      Function will return and let the caller terminate
 *      execution.
 *
 * RETURN VALUES
 *
 *      EXIT_SUCCESS    on success
 *      EXIT_FAILURE    on failure
 */
// RESOLVE HOW THIS WILL WORK FOR USER INVOCATION!
// icmond.user.pid ?
int pidfile_lock(const char *filename)
{
    char pidstr[256];   /* FAR more buffer than conceivably ever needed */

    if (!filename)
        return(EXIT_FAILURE);

    /*
     * Save filename for pidfile_unlock()'s unlink() function
     */
    strcpy(pidfile.name, filename);

    /*
     * Create pid file
     *
     * Any locks do not yet show, this will succeed even if
     * there is another instance already running.
     * When we try to assert our own locks, we will notice.
     */
    pidfile.handle = open(
                         pidfile.name,
                         O_RDWR | O_CREAT,     /* read&write, created if absent    */
                         S_IRUSR | S_IWUSR     /* "0600": owner can read and write */
                         );
    if (pidfile.handle == -1)
    {
        logerr("Could not open PID lock file \"%s\"!", pidfile.name);
        return(EXIT_FAILURE);
    }
#ifdef _DEBUG
    else { syslog(LOG_DEBUG, "PID lock file \"%s\" opened ... [OK]", pidfile.name); }
#endif

    /*
     * Try to assertt a lock file to the pid file
     */
    if (lockf(pidfile.handle, F_TLOCK, 0) == -1)
    {
        logerr("Could not lock PID lock file \"%s\"!", pidfile.name);
        return(EXIT_FAILURE);
    }
#ifdef _DEBUG
    else { syslog(LOG_DEBUG, "lockf() on PID lock file \"%s\" ... [OK]", pidfile.name); }
#endif

    /*
     * Write PID into the lockfile
     */
    sprintf(pidstr, "%d\n", getpid());
    if (write(pidfile.handle, pidstr, strlen(pidstr)) != strlen(pidstr))
    {
        logerr("Writing pid file \"%s\" failed!", pidfile.name);
    }
#ifdef _DEBUG
    else { syslog(LOG_DEBUG, "Daemon PID written into \"%s\" ... [OK]", pidfile.name); }
#endif

    return(EXIT_SUCCESS);
}

/*
 * pidfile_unlock()
 *
 *      No checks are considered necessary as this function is not
 *      called unless the deamon is already shutting down.
 *      Even if close() or unlink() would fail, we would still just
 *      terminate the daemon anyway.
 */
void pidfile_unlock()
{
    if (pidfile.handle)
    {
        close(pidfile.handle);
        unlink(pidfile.name);
        pidfile.handle = 0;
    }
}

/* EOF pidfile.c */
