/*
 * datalogger.c - 2016 Jani Tammi <janitammi@gmail.com>
 *
 *  NOIE:   Being a child process to daemon, this MUST user _exit()
 *          (instead of exit()) in order to avoid calling atexit()
 *          registered functions!
 *
 *  Portion that actually retrieves and stores line data from the modem.
 *  Also known as "worker" or "worker process".
 *
 *
 *      Parent process fork()'s one child on each tick of the interval
 *      timer and directs the child's execution to datalogger() function
 *      along with time_t value and access to config.c:cfg structure.
 *
 *      Parent (the daemon proper) is uninterested in the specifics and
 *      only cares about the eventual exit code (and that the datalogger
 *      process does not exceed allotted execution time).
 *
 *      During normal execution, datalogger DOES NOT WRITE TO SYSLOG!
 *      Only when errors are encountered, entries are written into the syslog
 *      for debugging / analysis purposes.
 *
 *      Datalogger is responsible for executing the scrubber script (the
 *      code that actually retrieves and parses data from the modem's
 *      WebUI) and writing it into the database specified in the cfg struct.
 *
 *      EXIT CODES TO BE REDESIGNED
 *      Following return codes are used:
 *      0       No error.
 *      (range -1 .. -9 reserved for general errors)
 *      (range -10 .. -19 database related
 *      -10     Unable to open database
 *      -11     Write to database failed
 *
 */
#include <stdlib.h>         // EXIT_SUCCESS, EXIT_FAILURE
#include <unistd.h>         // sleep(), fork(), execve()
#include <stdbool.h>
#include <string.h>         // strdup()
#include <sqlite3.h>
#include <math.h>           // round()
#include <errno.h>          // errno
#include <time.h>
#include <signal.h>         // sigfillset() ...
#include <sys/prctl.h>      // prctl()
#include <sys/types.h>      // pid_t
#include <sys/wait.h>       // waitpid()
#include <sys/signalfd.h>   // signalfd()
#include <sys/timerfd.h>    // timerfd_create()
#include <sys/capability.h> // Link with -lcap    cap_*() functions

#include "datalogger.h"
#include "config.h"
#include "database.h"
#include "icmpecho.h"
#include "capability.h"
#include "logwrite.h"
#include "keyval.h"
#include "util.h"

// Which pipe is which...
#define PIPE_READ   0
#define PIPE_WRITE  1

// temporary re-label
#define devlog(fmtstr, ...) logdev((fmtstr), ##__VA_ARGS__)




/*
 * Signaling and timer file descriptors
 */
static struct datalogger_instance_t
{
    int                 signalfd;           // for receiving signals SIGHUP and SIGTERM
    databaserecord_t    dbrec;              // DB row/record. See: database.h
    int                 returnvalue;
} instance;

static struct inetping_t
{
    pid_t               pid;
    int                 timeoutfd;
    struct itimerspec   tspec;
} inetping;

static struct modemping_t
{
    pid_t               pid;
    int                 timeoutfd;
    struct itimerspec   tspec;
} modemping;

// script output should never exceed much over 102 + 1 characters anyway
#define SCRUBBER_STDOUTBUFFER_SIZE  128
static struct scrubber_t
{
    pid_t               pid;
    int                 killed_for_timeout;
    int                 timeoutfd;
    struct itimerspec   tspec;
    char *              script;
    char *              argv[3];            // ONE argument: modem IP
    char *              envp[5];            // How to use this... is still undecided
    int                 pipe[2];            // for receiving script stdout
    int                 stdoutnbytes;       // How many bytes was received
    char                stdoutbuffer[SCRUBBER_STDOUTBUFFER_SIZE];
} scrubber =
{
    .killed_for_timeout = false,
    .envp = {"HOME=/", "PATH=/bin:/usr/bin", NULL}
};

static void init_scrubber_struct(const char *scriptname, const char *modemip, int timeout)
{

    scrubber.script  = strdup(scriptname);
    scrubber.argv[0] = scrubber.script;
    scrubber.argv[1] = strdup(modemip);
    scrubber.argv[2] = NULL;

    /* Time relative from start
     * Scripts execution was times to be almost exactly 1 second.
     * Allowing for heavy load, network traffic and other factors,
     * THREE SECOND seems like a good compromise between the above
     * one second and the minimum interval of five second.
     */
    scrubber.tspec.it_value.tv_sec     = (timeout / 1000);  // arg3 value in milliseconds
    scrubber.tspec.it_value.tv_nsec    = (timeout % 1000);
    /* Interval = 0 (do not repeat) */
    scrubber.tspec.it_interval.tv_sec  = 0;
    scrubber.tspec.it_interval.tv_nsec = 0;

    if ((scrubber.timeoutfd = timerfd_create(CLOCK_REALTIME, 0)) == -1)
    {
        logerr("timerfd_create()");
        _exit(EXIT_FAILURE);
    }
    /* Pipe stuff */
    pipe(scrubber.pipe);
}
/*
void init_modemping_struct()
{
    modemping.pid       = 0;
    modemping.timeoutfd = 0;
    return;
}

void init_inetping_struct()
{
    inetping.pid       = 0;
    inetping.timeoutfd = 0;
    return;
}
*/
/*
 * Handle daemon termination
 * Terminate pending child processes
 */
void process_terminate()
{
    if (scrubber.pid)
    {
        devlog("Killing scrubber (PID: %d)", scrubber.pid);
        kill(scrubber.pid, SIGKILL);
        /* do I have to wait() it as well? */
    }
    if (modemping.pid)
    {
        devlog("Killing modem ping (PID: %d)", modemping.pid);
        kill(modemping.pid, SIGKILL);        
    }
    if (inetping.pid)
    {
        devlog("Killing inet ping (PID: %d)", inetping.pid);
        kill(inetping.pid, SIGKILL);        
    }
}

int process_child(pid_t pid, int status)
{
    if (pid == scrubber.pid)
    {
        /* Disable timeout */
        timerfd_disarm(scrubber.timeoutfd);

        /*
********** Development info about child exit
         */
        devlog(
              "Scrubber exited (PID: %d) WIFEXITED: %s, WIFSIGNALED: %s (status: 0x%.8X)",
              scrubber.pid,
              WIFEXITED(status) ? "true" : "false",
              WIFSIGNALED(status) ? "true" : "false",
              status
              );
        if (WIFEXITED(status)) /* This macro should only be employed if WIFEXITED returned true.  */
            devlog("WEXITSTATUS: 0x%.2X", WEXITSTATUS(status));
        if (WIFSIGNALED(status)) /* This macro should only be employed if WIFSIGNALED returned true.  */
            devlog("WTERMSIG: 0x%.2X %s", WTERMSIG(status), getsignalname(WTERMSIG(status)));
/********* END development info */

        /*
         * VERY IMPORTANT !! Clear scrubber.pid so that the main loop knows
         * that this child is no longer executing (loop exit condition)
         */
        scrubber.pid = 0;

        /*
         * WIFEXITED(status) - returns  true  if  the child terminated normally,
         * that is, by calling exit(3) or _exit(2), or by returning from main().
         * WEXITSTATUS(status) - return code from child.
         */
        if (WIFEXITED(status))
        {
            memset(scrubber.stdoutbuffer, 0, sizeof(scrubber.stdoutbuffer));
            // get stdout (proces just before DB insert)
            scrubber.stdoutnbytes = read(
                                        scrubber.pipe[PIPE_READ],
                                        scrubber.stdoutbuffer,
                                        sizeof(scrubber.stdoutbuffer) - 1
                                        );
            if (WEXITSTATUS(status) == 0)
            {
                // Regular no-error return code of zero
                devlog("Normal scrubber exit! status: %d, stdout: \"%s\"", status, scrubber.stdoutbuffer);
            }
            else
            {
                // Thus far this condition has appeared only with wget self-terminating with:
                // *** buffer overflow detected ***: wget terminated
                devlog(
                      "Scrubber terminated with exit code 0x%.2X (status 0x%8X). stdout: \"%s\"",
                      WEXITSTATUS(status),
                      status,
                      scrubber.stdoutbuffer
                      );
                // Set the flag to indicate scrubber failure
                instance.returnvalue |= DATALOGGER_FLAG_SCRUBBER_FAILURE;
            }
        }
        else if (WIFSIGNALED(status) && scrubber.killed_for_timeout)
        {
            if (WTERMSIG(status) != SIGKILL)
            {
                logerr("Scrubber timedout and was signaled SIGKILL, but died to signal %s!", getsignalname(WTERMSIG(status)));
            }
            // else ... exactly what we'd expect when we ourselves SIGKILL'ed it
            // Set the flag to indicate scrubber timeout
            instance.returnvalue |= DATALOGGER_FLAG_SCRUBBER_TIMEOUT;
        }
        else
        {
            logerr("Scrubber died to signal %s!", getsignalname(WTERMSIG(status)));
            instance.returnvalue |= DATALOGGER_FLAG_SCRUBBER_FAILURE;
        }
        // Whatever the conditions, close the pipe
        close(scrubber.pipe[PIPE_READ]);
    }
    else if (pid == inetping.pid)
    {
        devlog("inetping PID detected.");
        /* disable timer */
        /* clean pid */
        inetping.pid = 0;
        /* retrieve and store data */
    }
    else if (pid == modemping.pid)
    {
        devlog("modemping PID detected.");
        /* Disable modem ping timeout timer */
        /* clean pid */
        modemping.pid = 0;
    }
    else
    {
        logerr("Unrecoverable error! Unknown child PID %d received!", pid);
        _exit(EXIT_FAILURE);
    }

    return(EXIT_SUCCESS);
}


/******************************************************************************
 * datalogger() - worker process'es main function
 *
 *
 */
int datalogger(time_t logtime)
{
    // Have a different name in syslog messages for datalogger
    openlog(DAEMON_NAME".datalogger", LOG_PID, LOG_DAEMON);

    // In repeated calls to this function (development mode!)
    // we must clear some of the data structures or old data will
    // remain in error conditions
    memset(scrubber.stdoutbuffer, 0, SCRUBBER_STDOUTBUFFER_SIZE);
    memset(&instance, 0, sizeof(struct datalogger_instance_t));

    /*
     * UNIX timestamp, as handed to us by parent.
     * Will be stored into the database as such (32-bit time_t)
     */
    instance.dbrec.timestamp = logtime;
    instance.returnvalue     = 0;

    capability_set();
//    capability_logdev(); // capability.c

    /*
     * Must make sure that raw socket capability is still there.
     * The capability_set() would have failed already without CAP_INET_RAW
     * so consider this a paranoia check...
     */
    if (!prctl(PR_CAPBSET_READ, CAP_NET_RAW, 0, 0, 0))
    {
        logerr("Raw net socket capability missing! Cannot send ICMP Echo Request!");
        _exit(EXIT_FAILURE);
    }

    /* args: Scrubber Script, Modem IP, timeout */
    init_scrubber_struct(cfg.modem.scrubber.filename, cfg.modem.ip, cfg.modem.scrubber.timeout);

    /* set up signalfd's for:
     *
     *  SIGCHLD (execv scrubber)
     *  SIGCHLD (ping hosts) - either ping all or randomly pick one
     *  SIGCHLD (ping modem) - should be implemented to get additional status data on modem
     *  Timer for scrubber timeout
     *  Timer for ping(s)
     *
     * The set of signals to be received via the file descriptor should be blocked using
     * sigprocmask(2), to prevent the signals being handled according to their default
     * dispositions.
     *
     * Block all signals. We will process them in pselect() loop later.
     *
     */
    /* Create signal set containing all signals */
    sigset_t sigmask_generic;
    sigfillset(&sigmask_generic);

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
                        SIG_SETMASK,      // directive to replace with new list
                        &sigmask_generic, // pointer to sigset_t
                        NULL              // pointer to OLD sigset_t
                        ) == -1)
    {
        logerr("sigprocmask(SIG_SETMASK, &sigmask_generic, NULL)");
        _exit(EXIT_FAILURE);
    }

    /*
     * signalfd() - Receive signals via file descriptor
     */
    /*
     * Create empty signal set and add SIGCHLD and SIGTERM in it
     */
    sigset_t sigmask_signal_fd;
    sigemptyset(&sigmask_signal_fd);
    sigaddset(&sigmask_signal_fd, SIGCHLD);     // data retrieval child processes signal exit
    sigaddset(&sigmask_signal_fd, SIGTERM);     // parent signals termination (do we really observe this?)

    /*
     * Create signal file descriptor
     */
    if ((instance.signalfd = signalfd(-1, &sigmask_signal_fd, 0)) == -1)
    {
        logerr("signalfd(-1, &sigmask_signal_fd, 0)");
        _exit(EXIT_FAILURE);
    }

    /*
****** SET UP TIMER FD's
     * All timerspec setups require this (and I know of no alternative)
     * USE RELATIVE TIMERS!!! timerfd_settime()'s second argument as zero,
     * and the timer will be relative to the call instant.
     */
    /*
    struct   timespec tspec_now;
    // This bastard requires LINK option (gcc -lrt source.c) :(  
    if (clock_gettime(CLOCK_REALTIME, &tspec_now) == -1)
    {
        logerr("clock_gettime()");
    }
*/
    /*
     * Prepare ICMP Echo Request packet sending
     * NOTE; timeout in MILLISECONS!
     */
    struct icmpecho_t *icmpmodem = icmp_prepare(cfg.modem.ip, cfg.modem.pingtimeout);
//#pragma message "NO SUPPORT FOR MULTIPLE INET PING HOSTS - JUST TAKES THE FIRST..."
    struct icmpecho_t *icmpinet  = icmp_prepare(cfg.inet.pinghosts, cfg.inet.pingtimeout);
//icmp_dump(icmpmodem);

    /*
     * Scrubber timeout (relative timer)
     *
     *      Sets a timer to expire at maximum allowed wait time.
     *      This is not configurable atm...
     */
/* MOVE TO BEFORE LOOP */
    timerfd_start_rel(scrubber.timeoutfd, &scrubber.tspec);

    /*
     * execv scrubber is likely to deliver data in stdout
     * read and parse. Report parsing errors
     *
     * Read and parse pings
     */
    int status = 0;
    switch (scrubber.pid = fork())
    {
        case -1:
            perror("fork()");
            _exit(EXIT_FAILURE);
        case 0: // in the child
#ifdef _DEBUG
            syslog(
                  LOG_DEBUG,
                  "calling execve(\"%s\", {\"%s\"}, envp)",
                  scrubber.script,
                  scrubber.argv[1]
                  );
#endif
            close(scrubber.pipe[PIPE_READ]);
            dup2(scrubber.pipe[PIPE_WRITE], STDOUT_FILENO);
            status = execve(scrubber.script, scrubber.argv, scrubber.envp);
            // syslog() because lowrite.c does not recognize our parent pid.
            // This is the ONLY place in this whole solution where we need to do this.
            syslog(
                  LOG_ERR,
                  "execve(\"%s\", {\"%s\"}, envp) failed! (status 0x%.8X)",
                  scrubber.script,
                  scrubber.argv[1],
                  status
                  );
            _exit(status); // only happens if execve(2) fails
        default: // in parent
            close(scrubber.pipe[PIPE_WRITE]);
            break;
    }
//    devlog("Scrubber child (PID: %d) started", scrubber.pid);

    /*
     * Launch ICMP Echo Request (relative timers)
     */
    icmp_send(icmpinet);
    timerfd_start_rel(icmpinet->timeoutfd, &icmpinet->timeoutspec);
    icmp_send(icmpmodem);
    timerfd_start_rel(icmpmodem->timeoutfd, &icmpmodem->timeoutspec);

    /*
****** MAIN LOOP
     *
     *      Execution leaves this look only when all child processes are
     *      completed and collected.
     */
    int      nfds;          // number of file descriptors
    int      prc;           // pselect() return code (value)
    fd_set   readfds;
    devlog("Startup completed, entering main loop...");
    do
    {

        /*
         * Setup fd_set(s) ... every time
         * Reason is that the listen / select calls mess them up.
         * So, rebuild, every time before select()
         */
        nfds = 0;
        FD_ZERO(&readfds);
        FD_SET(scrubber.timeoutfd, &readfds);
        nfds = (nfds > scrubber.timeoutfd ? nfds : scrubber.timeoutfd);   // max(nfds, fd)
        FD_SET(instance.signalfd, &readfds);
        nfds = (nfds > instance.signalfd ? nfds : instance.signalfd);     // max(nfds, fd)
        // Add ICMP Echo Request fds
        if (icmpinet->sent_and_listening)
        {
            FD_SET(icmpinet->recvfd, &readfds);
            nfds = (nfds > icmpinet->recvfd ? nfds : icmpinet->recvfd);
            FD_SET(icmpinet->timeoutfd, &readfds);
            nfds = (nfds > icmpinet->timeoutfd ? nfds : icmpinet->timeoutfd);
        }
        if (icmpmodem->sent_and_listening)
        {
            FD_SET(icmpmodem->recvfd, &readfds);
            nfds = (nfds > icmpmodem->recvfd ? nfds : icmpmodem->recvfd);
            FD_SET(icmpmodem->timeoutfd, &readfds);
            nfds = (nfds > icmpmodem->timeoutfd ? nfds : icmpmodem->timeoutfd);
        }
//devlog("Entering pselect()");
        prc = pselect(
                     nfds + 1,        // Calculated by setup above
                     &readfds,        // ditto
                     NULL,            // &writefds,  // If it would be needed
                     NULL,            // &exceptfds, // Again, if it would be needed
                     NULL,            // No timeout (not necessary, since we're monitorin timers)
                     NULL             // needs sigfillset() mask? let's try without one...
                     );
//devlog("Returned from pselect(): %d", prc);
        if (prc == -1 && errno != EINTR)
        {
            logerr("pselect() failure");
            _exit(EXIT_FAILURE);
        }
        else if (prc == -1)
        {
            // This should not happen unless SIGKILL or SIGSTOP
            // and if it is either of them, we should terminate
            logerr("pselect() was interrupted by unknown signal");
            _exit(EXIT_FAILURE);
        }
        /*
********** SIGNALS
         */
        if (FD_ISSET(instance.signalfd, &readfds))
        {
            struct signalfd_siginfo sigfdinfo;
            ssize_t s;
            /*
             * Reading signal file descriptor nullifies it
             */
            if((s = read(instance.signalfd, &sigfdinfo, sizeof(struct signalfd_siginfo))) != sizeof(struct signalfd_siginfo))
            {
                logerr("read(instance.signalfd, &sigfdinfo, sizeof(struct signalfd_siginfo))");
                _exit(EXIT_FAILURE);
            }
            /*
             * Resolve which signal...
             */
            switch(sigfdinfo.ssi_signo)
            {
                case SIGTERM:
                    /*
                     * Terminate execution (but do we really handle this?)
                     */
                    logmsg(LOG_INFO, "Datalogger received SIGTERM, shutting down....");
                    process_terminate();
                    _exit(EXIT_SUCCESS);
                    break;
                case SIGCHLD:
                    /*
                     * One or more of our child processes have terminated
                     */
                    ;   // The language standard demands that labels are followed by statements (not declarations), so here's ";" for you...
                    pid_t   pid;
                    int     status;
                    int     rc;
                    while ((pid = waitpid(-1, &status, WNOHANG)) != -1)
                    {
                        // If process_child() returns non-zero, it is unrecoverable
                        // We will terminate this function/process and return the code
                        // returned by process_child()
                        if ((rc = process_child(pid, status)))
                        {
                            return(rc);
                        }
                    }
                    errno = 0; // last while() results in ECHILD (10), wipe it
                    break;
                default:
                    logerr("Datalogger received unexpected signal (%d)!", sigfdinfo.ssi_signo);
                    break;
            }
        }
        /*
********** Scrubber timeout
         */
        if (FD_ISSET(scrubber.timeoutfd, &readfds))
        {
            /*
             * Read file descriptor to reset state
             */
            timerfd_acknowledge(scrubber.timeoutfd);    // util.c
            logmsg(LOG_ERR, "Terminating scrubber (pid: %d) for exceeding time allowance...", scrubber.pid);
            kill(scrubber.pid, SIGKILL); // The bastard won't terminate with SIGTERM
            scrubber.killed_for_timeout = true;
        }

        /*
********** ICMP Echo
         */
        if (FD_ISSET(icmpmodem->recvfd, &readfds))
        {
            timerfd_disarm(icmpmodem->timeoutfd);
            icmp_receive(icmpmodem);
            devlog("Modem ICMP echo reply received in %.2f ms", icmp_getelapsed(icmpmodem));
        }
        if (FD_ISSET(icmpmodem->timeoutfd, &readfds))
        {
            // Read file descriptor to reset state
            timerfd_acknowledge(icmpmodem->timeoutfd);  // util.c
            devlog("Modem ICMP echo timeout");
            icmp_cancel(icmpmodem);
        }
        // Inet ICMP Echo Reply
        if (FD_ISSET(icmpinet->recvfd, &readfds))
        {
            timerfd_disarm(icmpinet->timeoutfd);
            icmp_receive(icmpinet);
            devlog("Inet ICMP echo reply received in %.2f ms", icmp_getelapsed(icmpinet));
        }
        if (FD_ISSET(icmpinet->timeoutfd, &readfds))
        {
            // Read file descriptor to reset state
            timerfd_acknowledge(icmpinet->timeoutfd);  // util.c
            devlog("Inet ICMP echo timeout");
            icmp_cancel(icmpinet);
        }

    /*
     * Time to exit loop?
     */
    } while (inetping.pid || modemping.pid || scrubber.pid ||
             icmpmodem->sent_and_listening || icmpinet->sent_and_listening);
//    devlog("All tasks completed. Exiting pselect() loop...");

    /*
****** Preprocess data
     *
     *      ICMP Echo Reply times, rounded to 2 decimals
     */
//    if ()
    {
        instance.dbrec.modemping_ms = round(icmp_getelapsed(icmpmodem) * 100) / 100;
        instance.dbrec.inetping_ms  = round(icmp_getelapsed(icmpinet)  * 100) / 100;
    }
    // ICMP's not needed anymore
    free(icmpinet);
    free(icmpmodem);

    // First set all values to symbolic null values
    // They will be replaced with proper values, if all goes well
    instance.dbrec.down_ch1_dbmv = DATABASE_DOUBLE_NULL_VALUE;
    instance.dbrec.down_ch1_db   = DATABASE_DOUBLE_NULL_VALUE;
    instance.dbrec.down_ch2_dbmv = DATABASE_DOUBLE_NULL_VALUE;
    instance.dbrec.down_ch2_db   = DATABASE_DOUBLE_NULL_VALUE;
    instance.dbrec.down_ch3_dbmv = DATABASE_DOUBLE_NULL_VALUE;
    instance.dbrec.down_ch3_db   = DATABASE_DOUBLE_NULL_VALUE;
    instance.dbrec.down_ch4_dbmv = DATABASE_DOUBLE_NULL_VALUE;
    instance.dbrec.down_ch4_db   = DATABASE_DOUBLE_NULL_VALUE;
    instance.dbrec.down_ch5_dbmv = DATABASE_DOUBLE_NULL_VALUE;
    instance.dbrec.down_ch5_db   = DATABASE_DOUBLE_NULL_VALUE;
    instance.dbrec.down_ch6_dbmv = DATABASE_DOUBLE_NULL_VALUE;
    instance.dbrec.down_ch6_db   = DATABASE_DOUBLE_NULL_VALUE;
    instance.dbrec.down_ch7_dbmv = DATABASE_DOUBLE_NULL_VALUE;
    instance.dbrec.down_ch7_db   = DATABASE_DOUBLE_NULL_VALUE;
    instance.dbrec.down_ch8_dbmv = DATABASE_DOUBLE_NULL_VALUE;
    instance.dbrec.down_ch8_db   = DATABASE_DOUBLE_NULL_VALUE;
    instance.dbrec.up_ch1_dbmv   = DATABASE_DOUBLE_NULL_VALUE;
    instance.dbrec.up_ch2_dbmv   = DATABASE_DOUBLE_NULL_VALUE;
    instance.dbrec.up_ch3_dbmv   = DATABASE_DOUBLE_NULL_VALUE;
    instance.dbrec.up_ch4_dbmv   = DATABASE_DOUBLE_NULL_VALUE;

    if (!scrubber.killed_for_timeout)
    {
        // Line data
        keyval_t kv = keyval_create(scrubber.stdoutbuffer);
        // There must be exactly 21 values (key + 20 values; 16 down stream and 4 upstream)
        if (keyval_nvalues(kv) != 20)
        {
            logerr("Malformed scrubber data! %d values in kv", keyval_nvalues(kv));
            free(kv);
            instance.returnvalue |= DATALOGGER_FLAG_SCRUBBER_DATAERROR;
        }
        else
        {
            // convert kv values into databaserecord_t doubles
            // This should never SIGSEGV because keyval_nvalues() has already
            // quaranteed that we have all pointers, even if they point to null.
            // (emptry string)
            // strtod() would be more advances, but I don't need any of it's features
            instance.dbrec.down_ch1_dbmv = atof(kv[1]);
            instance.dbrec.down_ch1_db   = atof(kv[2]);
            instance.dbrec.down_ch2_dbmv = atof(kv[3]);
            instance.dbrec.down_ch2_db   = atof(kv[4]);
            instance.dbrec.down_ch3_dbmv = atof(kv[5]);
            instance.dbrec.down_ch3_db   = atof(kv[6]);
            instance.dbrec.down_ch4_dbmv = atof(kv[7]);
            instance.dbrec.down_ch4_db   = atof(kv[8]);
            instance.dbrec.down_ch5_dbmv = atof(kv[9]);
            instance.dbrec.down_ch5_db   = atof(kv[10]);
            instance.dbrec.down_ch6_dbmv = atof(kv[11]);
            instance.dbrec.down_ch6_db   = atof(kv[12]);
            instance.dbrec.down_ch7_dbmv = atof(kv[13]);
            instance.dbrec.down_ch7_db   = atof(kv[14]);
            instance.dbrec.down_ch8_dbmv = atof(kv[15]);
            instance.dbrec.down_ch8_db   = atof(kv[16]);
            instance.dbrec.up_ch1_dbmv   = atof(kv[17]);
            instance.dbrec.up_ch2_dbmv   = atof(kv[18]);
            instance.dbrec.up_ch3_dbmv   = atof(kv[19]);
            instance.dbrec.up_ch4_dbmv   = atof(kv[20]);
        }
        free(kv);
    }
// Little extreme, but I have already needed this twice...
//database_logdev(&instance.dbrec);

    /*
****** INSERT
     */
    // Determine insert target
    char *datafile;
    if (cfg.execute.tmpfs)
        datafile = cfg.database.tmpfsfilename;
    else
        datafile = cfg.database.filename;
    xtmr_t *t = xtmr();   // util.c
    int rc;
    if ((rc = database_insert(datafile, &instance.dbrec)))
    {
        logerr("Database insert failed! Return code %d", rc);
        return(DATALOGGER_SQLITE3_ERROR | instance.returnvalue);
    }
    devlog("SQLite3 INSERT took %5.2f milliseconds", xtmrlap(t));
    free(t);

    // All done, returns to fork() code which will take care of
    // the actual _exit()
    return (errno = 0, EXIT_SUCCESS | instance.returnvalue);
}

/*
 * perhaps someday this can provide daemon main loop information about failures?
 * Since we are executing in different process spaces (daemon and worker), the
 * exit code can provide a "key". This function should translate the key into
 * user friendly explanation string.
 */
char *datalogger_errstr(int error)
{
    return NULL;  // THIS FUNCTION TURNS OUT TO BE UTTERLY POINTLESS - TO BE REMOVED
}


/* EOF datalogger.c */

