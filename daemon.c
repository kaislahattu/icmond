/*
 * daemon.c - 2016, Jani Tammi <janitammi@gmail.com>
 *
 *
 *	Multi-ping
 *
 *	Spawn one ICMP child for each ping host. Use the smallest pong milliseconds.
 *	Logically, any ping reply indicates at least some level of internet routing.
 *	Should this be percentages as well? eg. "50% successful, 19ms (best time)"?
 *
 *	Current model
 *
 *	Pick at random one of the ping hosts to ping. However, the benefit of doing
 *	this is forgotten... perhaps this should be replaced with the above multi-ping.
 */
#include <stdlib.h>             // EXIT_*, exit(), random()
#include <stdint.h>             // definition of uint64_t
#include <signal.h>             // SEGSETOPS(3), sigprogmask(), sigaction()
#include <syslog.h>             // syslog()
#include <unistd.h>             // read()
#include <stdbool.h>            // true, false
#include <string.h>             // memset()
#include <errno.h>              // errno
#include <execinfo.h>           // backtrace()
#include <sys/select.h>         // pselect()
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/timerfd.h>        // timerfd_*
#include <sys/signalfd.h>       // signalfd(), struct signalfd_siginfo
#include <sys/capability.h>     // cap_*()  link with -lcap
#include <limits.h>             // INT_MAX

#include "daemon.h"
#include "datalogger.h"
#include "config.h"
#include "event.h"
#include "pidfile.h"
#include "logwrite.h"
#include "capability.h"
#include "util.h"

/*
 * Execution statistics
 *
 *      Both keeping records of the process state (such as suspended more) and
 *      keeping statistics like when the daemon was started and how many times
 *      has it launched the datalogger child process.
 *
 * n_interval_ticks      (signed) 32-bit int is good for 340 years
 *                       even when using shortest allowed interval
 *                       of 5 seconds. Incremented each time interval
 *                       timer fires, regadless of worker succss.
 * n_datalog_actions     Incremented once for each worker process spawned.
 * n_datalog_success     Incremented once for each successful worker process.
 * suspended_by_command  When true, daemon will not spawn worker process
 *                       when interval timer fires, but will keep on
 *                       rotating. Suspended daemon can thus capture
 *                       SIGTERM, SIGHUP...
 *                       SIGUSR1 will enable suspension and (=1)
 *                       SIGUSR2 will disable suspension (=0)
 * suspended_by_schedule Works like the above, but is not set by signals.
 *                       Instead, this will flip ON and OFF based on
 *                       scheduled daily start and end times.
 * start_time            stored at the start of daemon_main()
 * end_time              recorded before exit.
 *                       time_t is "long int" in Linux:
 *                       32-bits @ 32-bit executable.
 *                       64-bits @ 64-bit executable.
 *                       time(&start_time); (seconds since epoch)
 *                       32-bit systems are prone to year 2038 problem...
 *                       Still far enough in the future, I hope...
 */
static struct execstats_t
{
    int     n_interval_ticks;
    int     n_datalog_actions;
    int     n_datalog_success;
    int     n_schduled_events;
    time_t  start_time;
    time_t  end_time;
} execstats =
{
    .n_interval_ticks       = 0,
    .n_datalog_actions      = 0,
    .n_datalog_success      = 0,
    .n_schduled_events      = 0,
    .start_time             = 0,
    .end_time               = 0
};
/*
 * Signaling and timer file descriptors
 */
typedef struct
{
    int                     fd;
    struct itimerspec       tspec;
} fdtimer_t;
typedef struct
{
    int                 pid;
    int                 fd;
    struct itimerspec   tspec;
} pidtimer_t;
static struct daemon_t
{
    fdtimer_t               signal;
    fdtimer_t               schedule;
    fdtimer_t               interval;
    pidtimer_t              collecttmpfs;
    pidtimer_t              worker;
    struct {
        int                 running;
        time_t              suspended_by_command;
        time_t              suspended_by_schedule;
    } state;
    fd_set                  readfds;
    int                     readfdrange;
} this =
{
    .signal =
    {
        .fd                         = 0
    },
    .schedule =
    {
        .fd                         = 0
    },
    .interval =
    {
        .fd                         = 0,
        .tspec.it_value.tv_sec      = 0,
        .tspec.it_value.tv_nsec     = 0,
        .tspec.it_interval.tv_sec   = 0,
        .tspec.it_interval.tv_nsec  = 0
    },
    .collecttmpfs =
    {
        .pid                        = 0,
        .fd                         = 0,
    },
    .worker =
    {
        .pid                        = 0,
        .fd                         = 0
    },
    .state =
    {
        .running                    = true, // Set to FALSE and main loop will exit
        .suspended_by_command       = 0,
        .suspended_by_schedule      = 0
    },
    .readfdrange                    = 0
};

/*
 * 
 * timerfd_gettime(int fd, struct itimerspec *curr_value)
 *
 * Returns, in curr_value, an itimerspec structure that contains the current
 * setting of the timer referred to by the file descriptor fd.
 *
 * The it_value field returns the amount of time until the timer will next
 * expire. If both fields of this structure are zero, then the timer is
 * currently disarmed. This field always contains a relative value, regardless
 * of whether the TFD_TIMER_ABSTIME flag was specified when setting the timer.
 *
 * The it_interval field returns the interval of the timer. If both fields of
 * this structure are zero, then the timer is set to expire just once, at the
 * time specified by curr_value.it_value.
 */

#define DEVREPORT_TIMERFD(str, fd) \
    ({ \
        if ((fd)) \
        { \
            struct itimerspec spec; \
            if (timerfd_gettime((fd), &spec)) \
                logerr("timerfd_gettime() failure!"); \
            else \
                logdev  ( \
                        "[%s] %s (%d): " \
                        "%d.%03d remaining " \
                        "(%d.%03d interval)", \
                        FD_ISSET((fd), &this.readfds) ? "SET" : " - ", \
                        (str), \
                        (fd), \
                        spec.it_value.tv_sec, \
                        spec.it_value.tv_nsec / 1000000, \
                        spec.it_interval.tv_sec, \
                        spec.it_interval.tv_nsec / 1000000 \
                        ); \
        } \
        else \
        { \
            logdev("[   ] %s does not exist", (str)); \
        } \
    })

/*
 * Used to log execution statistics into the syslog.
 * Intended for daemon exit routines, but if I figure out
 * something to replace non-existing SIGINFO...
 *
 *  NOTE: execstats.end_time is updated each call
 */
static void logexecstats()
{
    struct tm *data;
    time(&execstats.end_time);
    time_t elapsed = (execstats.end_time - execstats.start_time);
    data = gmtime(&elapsed);
    if (data->tm_year - 70)
    {
        logmsg(
              LOG_INFO,
              "Runtime : %d years %d days %d hours, %d minutes, %d seconds.",
              data->tm_year - 70,
              data->tm_yday,
              data->tm_hour,
              data->tm_min,
              data->tm_sec
              );
    }
    else if (data->tm_yday)
    {
        logmsg(
              LOG_INFO,
              "Runtime : %d days %d hours, %d minutes, %d seconds.",
              data->tm_yday,
              data->tm_hour,
              data->tm_min,
              data->tm_sec
              );
    }
    else if (data->tm_hour)
    {
        logmsg(
              LOG_INFO,
              "Runtime : %d hours, %d minutes, %d seconds.",
              data->tm_hour,
              data->tm_min,
              data->tm_sec
              );
    }
    else if (data->tm_min)
    {
        logmsg(
              LOG_INFO,
              "Runtime : %d minutes, %d seconds.",
              data->tm_min,
              data->tm_sec
              );
    }
    else
    {
        logmsg(
              LOG_INFO,
              "Runtime : %d seconds.",
              data->tm_sec
              );
    }
    logmsg(
          LOG_INFO,
          "Processed %d/%d datalogging actions in total of %d interval ticks.",
          execstats.n_datalog_success,
          execstats.n_datalog_actions,
          execstats.n_interval_ticks
          );
}

/*****************************************************************************/
/*
 * API for scheduled events called by event.c:event_execute()
 */
int daemon_suspend()
{
    logmsg(LOG_INFO, "Scheduled entry to suspended mode");
    this.state.suspended_by_schedule = true;
    return EXIT_SUCCESS;
}

/*
 * API for scheduled events called by event.c:event_execute()
 */
int daemon_resume()
{
    logmsg(LOG_INFO, "Scheduled resume from suspended mode");
    this.state.suspended_by_schedule = false;
    return EXIT_SUCCESS;
}

/*
 * API for scheduled events called by event.c:event_execute()
 */
int daemon_watchdog()
{
    logerr("UNIMPLEMENTED!");
    return EXIT_FAILURE;
}

/*
 * API for scheduled events called by event.c:event_execute()
 * Child process function for retrieving tmpfs datafile records.
 */
int daemon_importtmpfs()
{
//  time_t now = time(NULL);
    // set timeout timer for collecttmpfs
    // if(fork() == child) {
    //      check that the tmpfs / datafile exists
    //      database.c:database_savetmpds(time_t before);
    //      report success / failure
    //      _exit();
    // }
    //      logdev("tmpfs data collection launched");

    // Schedule timeout event for the import process
    if (event_create(
                    EVENT_ACTION_IMPORTTMPFSTIMEOUT,
                    DAEMON_IMPORTTMPFS_TIMEOUT            // config.h
                    ) < 0)
    {
        logerr(
              "event_scheduled_create() rejected values: "
              "action (%d), schedulingtype (%d), seconds (%d)",
              EVENT_ACTION_IMPORTTMPFSTIMEOUT,
              EVENT_TYPE_ONCE,
              DAEMON_IMPORTTMPFS_TIMEOUT
              );
        exit(EXIT_FAILURE); // non-recoverable and cannot be ignored
    }

    return EXIT_SUCCESS;
}

int daemon_importtmpfstimeout()
{
    logerr("UNIMPLEMENTED!");
    return EXIT_FAILURE;
}
/*****************************************************************************/

static void devreport_rescheduling(time_t now, time_t next, event_t *event)
{
    if (!event)
    {
        logdev("event_t pointer is NULL");
        return;
    }
    struct tm tm;
    localtime_r(&next, &tm);
    logdev(
          "Event \"%s\" rescheduled to "
          "%02d:%02d:%02d %02d.%02d.%04d%s, "
          "%02d:%02d:%02d from now",
          event_getactionstr(event->action),
          tm.tm_hour,
          tm.tm_min,
          tm.tm_sec,
          tm.tm_mday,
          tm.tm_mon + 1,
          tm.tm_year + 1900,
          tm.tm_isdst > 0 ? " (DST)" : "",
          GETHOURS(next - now),
          GETMINUTES(next - now),
          GETSECONDS(next - now)
          );
    event_t *e = event_next();
    localtime_r(&e->next_trigger, &tm);
    logdev(
          "Next event \"%s\" triggers at "
          "%02d:%02d:%02d %02d.%02d.%04d%s, "
          "%02d:%02d:%02d from now",
          event_getactionstr(e->action),
          tm.tm_hour,
          tm.tm_min,
          tm.tm_sec,
          tm.tm_mday,
          tm.tm_mon + 1,
          tm.tm_year + 1900,
          tm.tm_isdst > 0 ? " (DST)" : "",
          GETHOURS(e->next_trigger - now),
          GETMINUTES(e->next_trigger - now),
          GETSECONDS(e->next_trigger - now)
          );
}

/*
 * Build fd_set
 *
 *      Idea of the fd set is that you set the ones you wish to be selected
 *      and pselect removes those that did NOT trigger a return.
 *      This, by definition, means that fd set has to be rebuild before
 *      every pselect() call.
 */
static void build_fdset()
{
#define FD_ADD_IF_EXISTS(fd) \
    ({ \
        if ((fd)) { \
            FD_SET((fd), &this.readfds); \
            this.readfdrange = this.readfdrange > (fd) ? this.readfdrange : (fd); \
        } \
    })
    FD_ZERO(&this.readfds);
    FD_ADD_IF_EXISTS(this.signal.fd);
    FD_ADD_IF_EXISTS(this.schedule.fd);
    FD_ADD_IF_EXISTS(this.interval.fd);
    FD_ADD_IF_EXISTS(this.collecttmpfs.fd);
    FD_ADD_IF_EXISTS(this.worker.fd);
#undef FD_ADD_IF_EXISTS
}

/*
 * daemon_initialize()
 *
 *      Setup that is affected by config_t cfg -values.
 *      This function is also called when SIGHUP is received.
 *
 *      This function contains only "hard-errors" that are unlikely
 *      to happen, but if they do, the whole daemon needs to exit.
 */
static void daemon_initialize()
{
    logdev("Configurable options for daemon...");

    /*
     *      clock_gettime() requires LINK option (gcc -lrt source.c)
     */
    struct   timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) == -1)
    {
        // Basically impossible to fail
        logerr("clock_gettime()");
        exit(EXIT_FAILURE);
    }


    /*
     * Create fd for signals
     *
     *      Create empty signal set and add signals of interest. This will be
     *      handed to pselect(). 
     */
    // NOT affected by configuration - SIGHUP should not redo.
    if (!this.signal.fd)
    {
        sigset_t sigmask_signal_fd;
        sigemptyset(&sigmask_signal_fd);
        sigaddset(&sigmask_signal_fd, SIGHUP);      // Signal for re-read configuration file
        sigaddset(&sigmask_signal_fd, SIGTERM);     // Signal for shutting down
        sigaddset(&sigmask_signal_fd, SIGCHLD);     // Signal for worker process termination
        sigaddset(&sigmask_signal_fd, SIGUSR1);     // Signal for enter suspended mode
        sigaddset(&sigmask_signal_fd, SIGUSR2);     // Signal for exit suspended mode
        sigaddset(&sigmask_signal_fd, SIGSEGV);     // Segmentation fault!
        /*
         * Create signal file descriptor
         */
        if ((this.signal.fd = signalfd(-1, &sigmask_signal_fd, 0)) == -1)
        {
            logerr("signalfd(-1, &sigmask_signal_fd, 0)");
            exit(EXIT_FAILURE);
        }
    }


    /*
     * Datalogging Interval timer
     *
     *      This timer will periodically wake up this process and when it does,
     *      a new worker process (datalogger) is created.
     *
     *      Each wakeup happens in cfg.datalog_interval seconds, following real
     *      clock (not relative to last tick) and thus ensuring long term
     *      accuracy. (no skew due to long term execution and relative timer)
     */
    // ONLY if this value has changed (NOTE: interval is 0 at first run...)
    if (cfg.execute.interval != this.interval.tspec.it_interval.tv_sec)
    {
        if (this.interval.fd)
        {
            timerfd_disarm(this.interval.fd);   // util.c
            close(this.interval.fd);
            this.interval.fd                        = 0;
            this.interval.tspec.it_value.tv_sec     = 0;
            this.interval.tspec.it_value.tv_nsec    = 0;
            this.interval.tspec.it_interval.tv_sec  = 0;
            this.interval.tspec.it_interval.tv_nsec = 0;
            logdev("interval timer destroyed");
        }
        if ((this.interval.fd = timerfd_create(CLOCK_REALTIME, 0)) == -1)
        {
            logerr("timerfd_create()");
            exit(EXIT_FAILURE);
        }
        // Start time values - next even 10 seconds
        this.interval.tspec.it_value.tv_sec     = now.tv_sec + (10 - now.tv_sec % 10);
        // Interval values
        this.interval.tspec.it_interval.tv_sec  = cfg.execute.interval;
        // Start interval timer
        if (timerfd_settime(this.interval.fd, TFD_TIMER_ABSTIME, &this.interval.tspec, NULL) == -1)
        {
            logerr("timerfd_settime(this.interval.fd)");
            exit(EXIT_FAILURE);
        }
    }


    /*
     * Event Schedule Timer
     *
     *      If configuration has events, they will be parsed and committed
     *      by start-up routines (or SIGHUP routines).
     *
     *      eventheap (minimum heap datastructure) always has the next event
     *      to trigger, in the root of the heap, accessible through
     *      event_next() call. This call returns NULL if no events are
     *      configures.
     */
    if (this.schedule.fd)
    {
        timerfd_disarm(this.schedule.fd);   // util.c
        close(this.schedule.fd);
        this.schedule.fd                        = 0;
        this.schedule.tspec.it_value.tv_sec     = 0;
        this.schedule.tspec.it_value.tv_nsec    = 0;
        this.schedule.tspec.it_interval.tv_sec  = 0;
        this.schedule.tspec.it_interval.tv_nsec = 0;
        logdev("schedule timer destroyed");
    }
    event_t *event;
    if ((event = event_next()))
    {
        if ((this.schedule.fd = timerfd_create(CLOCK_REALTIME, 0)) == -1)
        {
            logerr("timerfd_create()");
            exit(EXIT_FAILURE);
        }
        this.schedule.tspec.it_value.tv_sec     = event->next_trigger;
        logdev("New schedule time created");
        // Start Schedule timer
        if (timerfd_settime(this.schedule.fd, TFD_TIMER_ABSTIME, &this.schedule.tspec, NULL) == -1)
        {
            logerr("timerfd_settime(this.schedule.fd)");
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        // event_next() gives ENODATA, which we clear here
        errno = 0;
        logdev("No events, schedule timer not created");
    }

// THIS SHOULD BE REPLACED WITH SCHEDULED EVENT EVENT_ACTION_IMPORTTMPFSTIMEOUT
    /*
     * Collecttmpfs Timeout Timer
     *
     *      config.c:DAEMON_COLLECTTMPFS_TIMEOUT (in millisecods)
     *      When this timer is activated, it needs to be done so as relative
     *      (2nd arg = 0) ...and obviously reactivated during each datalogger
     *      fork()'ing.
     *
     */
#pragma message "TO BE OBSOLETED BY SCHEDULED EVENT"
    if (!this.collecttmpfs.fd)
    {
// THIS WILL BE OBSOLETED BY SCHEDULED EVENT
        this.collecttmpfs.tspec.it_value.tv_sec     = DAEMON_IMPORTTMPFS_TIMEOUT;
        this.collecttmpfs.tspec.it_value.tv_nsec    = 0;
        // Interval = 0
        this.collecttmpfs.tspec.it_interval.tv_sec  = 0;
        this.collecttmpfs.tspec.it_interval.tv_nsec = 0;
        if ((this.collecttmpfs.fd = timerfd_create(CLOCK_REALTIME, 0)) == -1)
        {
            logerr("timerfd_create()");
            exit(EXIT_FAILURE);
        }
        // Do not activate. It's done when the collecttmpfs process is actually created.
    }


    /*
     * Worker Timeout Timer
     *
     *      config.c:DAEMON_DATALOGGER_TIMEOUT (in millisecods)
     *      When this timer is activated, it needs to be done so as relative
     *      (2nd arg = 0) ...and obviously reactivated during each datalogger
     *      fork()'ing.
     *
     * NOTE: Does not support multiple worker (datalogger) processes!
     */
    if (!this.worker.fd)
    {
        this.worker.tspec.it_value.tv_sec     = DAEMON_DATALOGGER_TIMEOUT / 1000;
        this.worker.tspec.it_value.tv_nsec    = (DAEMON_DATALOGGER_TIMEOUT % 1000) * 1000000;
        // Interval = 0
        this.worker.tspec.it_interval.tv_sec  = 0;
        this.worker.tspec.it_interval.tv_nsec = 0;
        if ((this.worker.fd = timerfd_create(CLOCK_REALTIME, 0)) == -1)
        {
            logerr("timerfd_create()");
            exit(EXIT_FAILURE);
        }
        // Do not activate. It's done when the worker process is actually created.
    }

    /*
     * Commit parsed (tested) schedule to production schedule
     *
     *      Always remove all .source = PARSED events before moving
     *      the content of the parsed before adding new PARSED.
     *      This initialization function is called by either the start-up
     *      or handle_SIGHUP() and both are responsible for parsing the
     *      event schedule string before entering this function.
     *
     *  Parsing done by
     *      config.c:cfg_check()
     *      daemon.c:handle_SIGHUP() (by calling cfg_check())
     */
    event_schedule_clear(EVENT_SOURCE_PARSED);
    // We expect the test schdule to contain event string content
    event_commit_test_schedule();

    logdev("Initialization completed");
}

/*
 * atexit() registered function.
 * Tested and working.
 * NOTE: on_exit() is some kind of SunOS specific stuff (not POSIX) - avoid
 */
static void daemon_unexpected_exit()
{
    // Development of this function has been left undone, since it was
    // immediately clear that the mystery crash of 03.10.2016 was not
    // a voluntary exit() call, but some kind of surpressed SIGSEGV.
//    if (this.state.running)
    logdev("Unexpected exit!");
    pidfile_unlock();   // pidfile.c
}

/******************************************************************************
 * SIGNAL HANDLERS
 *
 *      Exception among these: handle_SIGSEGV(), which is old-school handler.
 *
 *      Note that these are NOT entered by signals! These will be called from
 *      within daemon_main()'s signal fd code.
 *
 *      There is no other purpose for having these as separate functions than
 *      simply keeping the main loop easier to read and looking nicer.
 */

/*
 * SIGSEGV - the exception among the others (direct entry)
 */
static void handle_SIGSEGV(int sig, siginfo_t *si, void *unused)
{
    // THIS DOESN'T WORK AS I HOPED...
#define BACKTRACESIZE   32
    void   *array[BACKTRACESIZE];
    size_t  ncalls, index;
    char  **strings;

    ncalls  = backtrace(array, BACKTRACESIZE);
    strings = backtrace_symbols(array, ncalls);

    logdev("SIGSEGV at address: 0x%lx", (long)si->si_addr);
    logdev("backtrace(): %d", ncalls);
    // prints each string of function names of trace
    for (index = 0; index < ncalls; index++)
        logdev("  %s", strings[index]);

    pidfile_unlock();       // pidfile.c
    _exit(EXIT_FAILURE);
}

/*
 * Terminate daemon
 * Set the exit flag and let the do{...}while() fall through
 */
static void handle_SIGTERM()
{
    this.state.running = false;
    logmsg(LOG_INFO, "Received SIGTERM, shutting down....");
}

/*
 * Re-read configuration file
 * Since SIGINFO is not supported, administrator can issue
 * SIGHUP without modifications to the configuration file to
 * get information printed to syslog
 */
static void handle_SIGHUP()
{
// TODO: Detect and report following:
// 1)   Report WHICH config file is being read
//      OR if the program has been started without config file
// 2)   List configuration keys that are being overwritten by
//      commandline arguments (cannot be changed by edits to config file)
    // Daemon still needs to honor the priority given to
    // commandline arguments, after re-reading the config.
    // one in main.c:main().
    // This is why this implementation follows closely the
    //
    // Create shallow duplicate for re-read attempt
    config_t *newcfg = cfg_dup(&cfg);
    cfg_init(newcfg);
    // In case commandline specified a configuration file
    // There will be NO commands, since this program WILL exit
    // after executing the commands (ie. we would not be here).
    // But even if there were, we would not act on them here...
    cfg_preread_commandline(newcfg, cmdline); // cmdlinev saved in config.h/config.c
    // We now know for certain which configuration file is to be read
    logmsg(
          LOG_INFO,
          "Received SIGHUP - re-reading configuration file '%s'...",
          newcfg->filename
          );
    // THIS portion may fail, since user has likely been modifying
    // the configuration file content.
    if (cfg_read_file(newcfg))
    {
        logerr("Configuration file read failed! No values changed.");
        cfg_free(newcfg);
        return;
    }
    // We need to overwrite those values that are specified in
    // the commandline (they have priority).
    cfg_read_argv(newcfg, cmdline);
    if (cfg_check(newcfg)) // will test parse event schedule string
    {
        logerr("Configuration failed quality checks. No values changed.");
        return;
    }
    cfg_commit(newcfg);
    cfg_free(newcfg);

    // Apply new configuration values
    daemon_initialize(); // replaces .source = PARSED events
}

/*
 * Enter suspended operation mode (no workers fork()'ed)
 */
static void handle_SIGUSR1()
{
    if (this.state.suspended_by_command)
        logmsg(LOG_INFO, "Already in suspended mode! Ignoring suspend signal SIGUSR1...");
    else
    {
        this.state.suspended_by_command = true;
        logmsg(LOG_INFO, "Now in suspended mode...");
    }
}

/*
 * Return to normal operation mode
 */
static void handle_SIGUSR2()
{
    if (this.state.suspended_by_command)
    {
        this.state.suspended_by_command = false;
        logmsg(LOG_INFO, "Normal operation resumed. No longer in suspended mode...");
    }
    else
    {
        logmsg(LOG_INFO, "Already in normal operation. Ignoring resume signal SIGUSR2...");
    }
}

/*
 * Child (datalogger) has exited
 */
static void handle_SIGCHLD()
{

    int   status;
    pid_t pid;
    if (!(pid = waitpid(-1, &status, WNOHANG)))
    {
        logerr(
              "SIGCHLD received but waitpid() returned %d (expected %d)",
              pid,
              this.worker.pid
              );
        return;
    }

    // Make sure it's the PID we expect
    if (pid == this.worker.pid)
    {
        timerfd_disarm(this.worker.fd);    // util.c
        //
        // Datalogger / Worker PID...
        //
        logdev("Datalogger PID received");

        // NOTE: WIFEXITED and WIFSIGNALED cannot be true at the
        // same time (physically impossible - see waitstatus.h)
        if (WIFEXITED(status))
        {
            // Voluntary exit
            logdev("worker pid: %d exited with code: %d", pid, WEXITSTATUS(status));
            if (WEXITSTATUS(status))
            {
                ;
                // Here we would look at the flags and codes...
            }
            else
            {
                // clock in one more success
                execstats.n_datalog_success++;
            }
        }
        else if (WIFSIGNALED(status))
        {
            // A signal murdered the worker
            logmsg(
                LOG_INFO,
                "Datalogger (pid: %d) died to %s signal",
                pid,
                getsignalname(WTERMSIG(status))
                );
        }
        else
        {
            logerr("Worker child neither exited nor was terminated by signal - this is considered impossible!");
            logerr("waitpid() returned status 0x%.8X", status);
        }
                        /*
                         * Development info about child exit
                         *//*
                        logdev(
                              "Worker exited (PID: %d) WIFEXITED: %s, WIFSIGNALED: %s (status: 0x%.8X)",
                              fd.workerpid,
                              WIFEXITED(status) ? "true" : "false",
                              WIFSIGNALED(status) ? "true" : "false",
                              status
                              );
                        if (WIFEXITED(status)) // This macro should only be employed if WIFEXITED returned true.
                            logdev("WEXITSTATUS: 0x%.2X", WEXITSTATUS(status));
                        if (WIFSIGNALED(status)) // This macro should only be employed if WIFSIGNALED returned true.
                        {
                            logdev("WTERMSIG: 0x%.2X", WTERMSIG(status));
                            logdev("Signal Name: \"%s\"", signame[WTERMSIG(status)]);
                        }
                        *//********* END development info */
        // We collected our PID - null it so we know not to wait anymore
        this.worker.pid = 0;
    }
    else if (pid == this.collecttmpfs.pid)
    {
        timerfd_disarm(this.collecttmpfs.fd);    // util.c
        //
        // CollectTMPFS PID...
        //
        logdev("CollectTMPFS PID received");
        if (WIFEXITED(status))
        {
            if (WEXITSTATUS(status))
                logerr("CollectTMPFS process exited with code (%d)", WIFEXITED(status));
        }
        else if (WIFSIGNALED(status))
        {
            logmsg(
                  LOG_INFO,
                  "ColectTMPFS (pid: %d) died to %s signal",
                  pid,
                  getsignalname(WTERMSIG(status))
                  );
        }
        else
        {
            logerr("CollectTMPFS neither exited nor was terminated by signal - this is considered impossible!");
            logerr("waitpid() returned status 0x%.8X", status);
        }
        this.collecttmpfs.pid = 0;
    }
    else
    {
        logerr(
              "waitpid() returned %d (datalogger PID: %d, import PID: %d)",
              pid,
              this.worker.pid,
              this.collecttmpfs.pid
              );
        return;
    }
    logdev("handle_SIGCHLD() completed.");
}


/******************************************************************************
 * daemon_main()
 *
 *      Observes following external signals:
 *          SIGHUP      Re-read config file
 *          SIGTERM     Terminate execution
 *          SIGUSR1     Enter suspended mode
 *          SIGUSR2     Exit suspended mode
 *          SIGSEGV     Try to recover or at least log before exit
 *
 *      The main execution loop of this daemon has very few responsibilites due
 *      to the design requirements of high reliability. For this reason, all
 *      external factors have been delegated as far as possible to the worker
 *      process.
 *
 *      execve()'ing external scripts is inherently messy business that could,
 *      despite best efforts, cause issues in various things ranging from
 *      memory management to buffer overflows. We want to ensure that the
 *      daemon core keeps firing interval timer reliably even if external
 *      conditions are unfavourable. We do not even want to encounter situation
 *      where previously existing file has been removed or is otherwise no 
 *      accessible.
 *
 *      Therefore, the actual daemon loop is very simple. Main responsibility
 *      begin to reliably spawn datalogger (worker) processes in steady
 *      intervals.
 *
 *      In addition it also maintains a suspended mode, during which no worker
 *      processes are fork()'ed.
 */
void daemon_main(void)
{
    
    atexit(daemon_unexpected_exit);
    // record the start time into execution statistics
    time(&execstats.start_time);

    // Re-establish E(ffective) and I(herit) falgs on our capability
    capability_set();

    /*
     * Block "all" signals during daemon_main().
     * 
     *      All signals that we accept will be received through pselect().
     *      SIGKILL and SIGSTOP are naturally unblockable...
     *      ...and SIGSEGV we wish to handle in our own handler.
     */
    // Create mask with all signals set (set == to be blocked)
    sigset_t    sigmask_daemon_main;
    sigfillset(&sigmask_daemon_main);
    sigdelset(&sigmask_daemon_main, SIGSEGV);
    /*
     * Apply the above created signal set as the set of blocked signals
     *
     * 1st argument: HOW (read below for the three "how" alternatives)
     * SIG_BLOCK     Block the signals in set---add them to the existing mask.
     *               In other words, the new mask is the union of the existing mask and set.
     * SIG_UNBLOCK   Unblock the signals in set---remove them from the existing mask.
     * SIG_SETMASK   Use set for the mask; ignore the previous value of the mask.
     */
    if (sigprocmask     (
                        SIG_SETMASK,            // directive to replace with new list
                        &sigmask_daemon_main,   // pointer to sigset_t
                        NULL                    // pointer to OLD sigset_t
                        ) == -1)
    {
        logerr("sigprocmask(SIG_SETMASK, &sigmask_daemon_main, NULL)");
        exit(EXIT_FAILURE);
    }

    /*
     * SIGSEGV  handler shall be old-school since it makes no sense
     *          what-so-ever to wait for a segmentation fault in pselect()
     */
    struct sigaction sigact;
    memset(&sigact, 0, sizeof(struct sigaction));
    sigact.sa_flags = SA_SIGINFO;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_sigaction = handle_SIGSEGV;
    if (sigaction(SIGSEGV, &sigact, NULL) == -1)
    {
        logerr("sigaction() for SIGSEGV failed!");
        exit(EXIT_FAILURE);
    }


    /*
     * NOTE:
     * We do NOT create a sigmask for the pselect(). All the signals that we
     * care about are already received via fd.signal, so there is no need to
     * enable any signals during pselect() calls.
     */










    /*
     * Initialize values that depend on config_t cfg
     */
    daemon_initialize();

    /**************************************************************************
     * main processing loop
     *************************************************************************/
    logmsg(
          LOG_DEBUG,
          "Entering main daemon loop (interval %d seconds)...",
          cfg.execute.interval
          );

    int     rc;                     // pselect() return value

    // Timeout MAY someday act as a "watchdog" that ensures timers are ticking...
    // Timeout for dev message spam (set to zero, not needed atm)
    struct timespec pselecttimeout __attribute__ ((__unused__));
    pselecttimeout.tv_sec  = 1;
    pselecttimeout.tv_nsec = 0;     // 500000000; // == 500ms

    do
    {
        // Must be rebuild before each pselect()
        build_fdset();
/*
        DEVREPORT_TIMERFD("interval", this.interval.fd);
        DEVREPORT_TIMERFD("worker", this.worker.fd);
        DEVREPORT_TIMERFD("schedule", this.schedule.fd);
        logdev("About to enter pselect()");
*/
        /*
         * IMPORTANT NOTE
         *
         * sigmask in pselect() works by BLOCKING/DISABLING all set signals
         * in the mask. So, first create mask with sigfillset(),
         * then sigdelset() the ones you want to make pselect() return
         *
         * Process'es current sigmask HAS NO RELEVANCE
         * (it's not an union or anything else)
         */
        rc = pselect(
                    this.readfdrange + 1,   // Calculated by setup above
                    &this.readfds,          // ditto
                    NULL,                   // &writefds,  // If it would be needed
                    NULL,                   // &exceptfds, // Again, if it would be needed
                    NULL, //&pselecttimeout, // NULL // No timeout (not necessary, since we're monitorin timers)
                    NULL                    // Do not enable any signal interruptions during pselect() call
                    );
        /*
         * pselect() return scenarios:
         *
         * pres > 0
         *     Normal, number of status changed fd's
         *
         * pres == 0
         *     Timeout occured (we don't supply timeout, so not possible)
         *
         * pres < 0 && errno == EINTR
         *     Interrupt allowed by sigmask_pselect fired
         *     NOTE!!! readfds is FILLED still!!! DO NOT TEST FOR FD'S!!
         *
         * pres < 0 && errno != EINTR
         *     ERROR - log and terminate!
         *
         * NOTE: On error (rc < 0) both timeout and fd sets become undefined
         *       (fd sets remain defined on success?)
         */
        if (rc < 0 && errno != EINTR)
        {
            logerr("pselect() failure");
            exit(EXIT_FAILURE);
        }
        else if (rc < 0)
        {
            // This should not happen unless SIGKILL or SIGSTOP
            // and if it is either of them, we should terminate
            logerr("pselect() was interrupted by unknown signal");
            exit(EXIT_FAILURE);
        }

        DEVREPORT_TIMERFD("interval", this.interval.fd);
        DEVREPORT_TIMERFD("worker", this.worker.fd);
        DEVREPORT_TIMERFD("collecttmpfs", this.collecttmpfs.fd);
        DEVREPORT_TIMERFD("schedule", this.schedule.fd);

        /*
********** Signals
         */
        if (FD_ISSET(this.signal.fd, &this.readfds))
        {
            struct signalfd_siginfo sigfdinfo;
            ssize_t s;
            // Reading signal file descriptor nullifies it
            if((s = read(this.signal.fd, &sigfdinfo, sizeof(struct signalfd_siginfo))) != sizeof(struct signalfd_siginfo))
            {
                logerr("read(this.signal, &sigfdinfo, sizeof(struct signalfd_siginfo))");
                exit(EXIT_FAILURE);
            }

            switch(sigfdinfo.ssi_signo)
            {
                case SIGTERM:           // Terminate daemon
                    handle_SIGTERM();
                    break;
                case SIGHUP:            // Re-read configuration
                    handle_SIGHUP();
                    break;
                case SIGUSR1:           // Enter suspended mode
                    handle_SIGUSR1();
                    break;
                case SIGUSR2:           // Return to normal operation mode
                    handle_SIGUSR2();
                    break;
                case SIGCHLD:           // Datalogger has exited
                    handle_SIGCHLD();
                    break;
                default:
                    logerr(
                          "Received unexpected signal (%d) %s! Ignoring...",
                          sigfdinfo.ssi_signo,
                          getsignalname(sigfdinfo.ssi_signo)
                          );
                    break;
            }
        } // if() signal

        /*
********** Interval timer (Datalogger)
         */
        if (FD_ISSET(this.interval.fd, &this.readfds))
        {
            // Read file descriptor to reset state
            timerfd_acknowledge(this.interval.fd);  // util.c
            execstats.n_interval_ticks++;

            // launch worker process unless suspended
            if (!(this.state.suspended_by_command || this.state.suspended_by_schedule))
            {
                if (this.worker.pid)
                {
                    logerr("Previous worker still running, skipping this tick...");
                    // at least until multiple workers are supported...
                }
                else
                {
                    // create worker process 
                    this.worker.pid = fork();
                    if (this.worker.pid < 0)
                    {
                        logerr("Unable to fork worker process");
                        // No exit, acceptable to lose a tick (see design notes)
                        this.worker.pid = 0;
                    }
                    else if (this.worker.pid > 0)
                    {
                        // Start worker/datalogger child timer (relative)
                        timerfd_start_rel(this.worker.fd, &this.worker.tspec);
                        execstats.n_datalog_actions++;
                        logdev("Created worker process (PID: %d)", this.worker.pid);
                        // ...and that's all we need to do here
                    }
                    else
                    {
                        // fork() returned zero, this is child code
                        int rc = datalogger(time(NULL));
// Maybe some logging about datalogger return codes?
                        logmsg(LOG_DEBUG, "datalogger() function returned %d.", rc);
                        // Now the child needs to _exit(), or it will run daemon_main code...
                        // NOTE: It will need to be _exit() -function or child will call atexit()
                        // registered fucntion
                        _exit(rc);
                    }
                } // if (this.workerpid)
            } // if not suspended
        } // FD_ISSET(this.intervaltimer)

        /*
********** Worker/Datalogger timeout
         */
        if (FD_ISSET(this.worker.fd, &this.readfds))
        {
            timerfd_acknowledge(this.worker.fd);    // util.c
            timerfd_disarm(this.worker.fd);         // util.c
            // Kill it with 9... Change to SIGTERM when it works...
            logdev("Datalogger timed out! Killing PID: %d", this.worker.pid);
            if (kill(this.worker.pid, SIGKILL))
            {
                logerr("kill(%d, SIGKILL) failed", this.worker.pid);
                exit(EXIT_FAILURE); // Atleast until I figure out a way to resolve failed kills
            }
            // DO NOT wait() here, do it in the SIGCHLD handler!
            // It may take a little time for the child to actually die
        }

        /*
********** CollectTMPFS timeout
         */
        if (FD_ISSET(this.collecttmpfs.fd, &this.readfds))
        {
            timerfd_acknowledge(this.collecttmpfs.fd);    // util.c
            timerfd_disarm(this.collecttmpfs.fd);         // util.c
            // Kill it with 9... Change to SIGTERM when it works...
            logdev("CollectTMPFS timed out! Killing PID: %d", this.collecttmpfs.pid);
            if (kill(this.collecttmpfs.pid, SIGKILL))
            {
                logerr("kill(%d, SIGKILL) failed", this.collecttmpfs.pid);
                exit(EXIT_FAILURE); // Atleast until I figure out a way to resolve failed kills
            }
            // DO NOT wait() here, do it in the SIGCHLD handler!
            // It may take a little time for the child to actually die
        }


        /*
********** Schedule timer
         */
        if (FD_ISSET(this.schedule.fd, &this.readfds))
        {
            int n_events = 0;
            event_t *event;
            // Read file descriptor to reset state
            timerfd_acknowledge(this.schedule.fd);  // util.c
            // Start pumping events that have triggered thus far
            time_t now = time(NULL);
            while ((event = event_gettriggered(now)))
            {
                n_events++;
                if (event_execute(event))
                    logerr("Event %s failed", event_getactionstr(event->action));
                else
                    logdev("Event %s processed successfully", event_getactionstr(event->action));
                // calculate new triggering time and enter into the event heap
                // UNLESS it is of type that triggers only once
                if (event->type != EVENT_TYPE_ONCE)
                    devreport_rescheduling(now, event_reschedule(event), event);
                else
                    free(event);    // Release events that are not rescheduled
            }
            logdev("Schedule timer expired. %d events processed", n_events);
            execstats.n_schduled_events += n_events;
            // Set timer to wake up for the next event
            if ((event = event_next()))
            {
                this.schedule.tspec.it_value.tv_sec = event->next_trigger;
                if (timerfd_start_abs(this.schedule.fd, &this.schedule.tspec))
                {
                    // None of the timerfd_settime() errors are trivially recoverable
                    logerr("Failed to reschedule timer fd for scheduled events");
                    exit(EXIT_FAILURE);
                }
            }
            else
            {
                // Suspicious, but not proven error condition
                // There may someday be activities that use schedule for one time events
                // and I do not want to make an empty schedule here now an error condition.
                logmsg(LOG_ERR, "Event schedule is now empty! Schedule timer will not trigger anymore.");
            }
        } // if ISSET this.schedule.fd


    // Exit if SIGCHLD hander has set the flag
    } while (this.state.running);

    /*
     * Daemon termination procedures
     */
    // Report execution statistics
    logexecstats();
    // Close syslog
    closelog();
    // Release pidfile
    pidfile_unlock();   // pidfile.c
    // This will be expected exit
    // _exit() will not call atexit() registered functions
    _exit(EXIT_SUCCESS);
}

/* EOF daemon.c */
