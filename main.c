/*
 * main.c - 2016 Jani Tammi <janitammi@gmail.com>
 *
 */
#include <stdio.h>
#include <stdlib.h>                 // EXIT_SUCCESS, EXIT_FAILURE
#include <string.h>
#include <unistd.h>                 // unlink(), euidaccess()
#include <sys/time.h>               // setrlimit()
#include <sys/resource.h>           // setrlimit()
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/prctl.h>              // PR_CAPBSET_READ
#include <sys/capability.h>         // Link with -lcap    cap_*() functions
#include <fcntl.h>
#include <syslog.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>

#include "main.h"
#include "daemon.h"
#include "config.h"
#include "event.h"
#include "capability.h"
#include "logwrite.h"
#include "ttyinput.h"
#include "pidfile.h"
#include "user.h"
#include "database.h"
#include "version.h"
#include "util.h"
#include "tmpfs.h"

static pid_t daemon_pid;

/******************************************************************************
 * daemonchild_handler()
 *
 *	Signal handler specifically for the parent process.
 *	Main purpose is to receive SIGUSR1 that signifies
 *	successful startup, report it and exit.
 */
static void daemonchild_handler(int signum)
{
    switch(signum)
    {
    case SIGUSR1:
        /* Expected outcome - daemon child signaled successful startup
         */
        logmsg(LOG_INFO, "daemon process started successfully (pid %d)", daemon_pid);
        exit(EXIT_SUCCESS);
        break;
    case SIGUSR2:
        /* Possible outcome - child found another copy of the process already running
         */
        logmsg(LOG_ERR, "Another copy of the daemon process already running!");
        logmsg(LOG_ERR, "Check \"%s\"", DAEMON_PIDFILE);
        exit(EXIT_FAILURE);
        break;
    case SIGCHLD:
        /* Abnormal - daemon child process died
         */
        logmsg(LOG_ERR, "daemon process died on startup (SIGCHLD received)");
        exit(EXIT_FAILURE);
        break;
    case SIGALRM:
        /* Timeout - 2 seconds passed, and no SIGUSR1 received!
         */
        logmsg(LOG_INFO, "timeout! Daemon child didn't report successful startup within 2 seconds!");
        exit(EXIT_FAILURE);
        break;
    default:
        logmsg(LOG_ERR, "unrecognized signal (%d) received! exiting...\n", signum);
        exit(EXIT_FAILURE);
    }
}

/******************************************************************************
 * daemonize()
 *
 *      Deamonize the process. Accepts one argument that specifies if a child
 *      process will be fork()'ed. If not, main thread will "deamonized".
 *      This would be useful for debugging purposes, not much else.
 */
static void daemonize(int run_as_daemon)
{
    // sig act used twice for different purposes
    struct sigaction sigact;

    /* paranoia - if already a daemon */
    if (getppid() == 1)
    {
        logerr("Parent PID is 1, so this process is already a daemon!");
        exit(EXIT_FAILURE);
    }
    if(!run_as_daemon)
        logmsg(LOG_DEBUG, "nodaemon option requested. Will not fork() into a background process.");

    /* Instruct kernel to keep process capabilities
     * when we change UID/GID.
     * NOTE: The "effective" statuses are still lost,
     *       but we will fix that later when we drop
     *       all the unwanted capabilities.
     */
    prctl(PR_SET_KEEPCAPS, 1L, 0, 0);

    /*
     * If run as root, change to DAEMON_RUN_AS_USER
     * It is also allowed that non-root user invokes
     * this daemon - then we will simply run with those
     * privileges.
     *
     * NOTE: signaling between parent and child FAIL unless
     *       BOTH execute as the same user!!!
     */
    logmsg(LOG_DEBUG, "Setting user to " DAEMON_RUN_AS_USER);
    user_changeto(DAEMON_RUN_AS_USER);  // user.c
    capability_set();                   // capability.c
    // Below function prints out only if -D_DEBUG was defined during compile time
    capability_logdev();                // capability.c

    if (run_as_daemon)
    {
        // Register signal handler for parent purposes
#ifdef __SIGNAL__ // Old style
        signal(SIGCHLD, daemonchild_handler);               /* Child dies -signal           */
        signal(SIGUSR1, daemonchild_handler);               /* Child send OK -signal        */
        signal(SIGUSR2, daemonchild_handler);               /* Child found another process already running */
        signal(SIGALRM, daemonchild_handler);               /* Parent wait child timeout    */
#else
        memset(&sigact, 0, sizeof(struct sigaction));
        /* Because all we need is signum argument, we use old-style handler function...*/
        sigact.sa_handler = daemonchild_handler;
        /* Filled with signals NOT allowed to interrupt; "block list"
         * This applies DURING the handler execution.
         */
        sigfillset(&sigact.sa_mask);
        /* Register four signals to our action  */
        sigaction(SIGCHLD, &sigact, NULL);         /* Child process died - abnormal outcome        */
        sigaction(SIGUSR1, &sigact, NULL);         /* Child sends "start-up OK" - expected outcome */
        sigaction(SIGUSR2, &sigact, NULL);         /* Child finds another copy already running     */
        sigaction(SIGALRM, &sigact, NULL);         /* Child start-up takes too long, terminate it  */
#endif

        // fork() off the parent process
        daemon_pid = fork();
        if (daemon_pid < 0)
        {
            logerr("unable to fork daemon, code=%d (%s)", errno, strerror(errno));
            exit(EXIT_FAILURE);
        }
        // Parent...
        if (daemon_pid > 0)
        {
            /* Wait for confirmation from the child via SIGTERM or SIGCHLD, or
            * for two seconds to elapse (SIGALRM).  pause() should not return.
            * See: daemonchild_handler() in this file.
            */
            alarm(2);               /* exit via SIGALRM handler             */
            pause();                /* ...doesn't return if not signaled    */
            logerr("this process received timeout signal before daemon child signaled OK! (2 seconds)");
            exit(EXIT_FAILURE);
        }
    } // if (run_as_daemon)

    /*
****** Executing as the child process from there onwards
     *
     * NOTE: daemon process will NOT be a child of init (PID: 1)
     *       until parent actually terminates. That's why logmsg()
     *       cannot determine correctly where to deliver messages.
     *
     * USE syslog() FOR THE REMINDER OF THIS FUNCTION!
     */

    /* daemon mode only SYSLOG line marking the start of daemon execution */
    syslog(LOG_INFO, "daemon ver. %s build %s starting...", DAEMON_VERSION, daemon_build);

    /* Cancel unwanted signals (only options are SIG_IGN, SIG_DFL or custom func())	*/
#ifdef __SIGNAL__
    signal(SIGCHLD, SIG_DFL);           /* A child process dies                         */
    signal(SIGTSTP, SIG_IGN);           /* Various TTY signals                          */
    signal(SIGTTOU, SIG_IGN);           /* ...                                          */
    signal(SIGTTIN, SIG_IGN);           /* ...                                          */
#else
    sigact.sa_handler = SIG_DFL;
    sigaction(SIGCHLD, &sigact, NULL);         /* Child process dies                    */
    sigact.sa_handler = SIG_IGN;
    sigaction(SIGTSTP, &sigact, NULL);         /* Various TTY signals                   */
    sigaction(SIGTTOU, &sigact, NULL);         /* ...                                   */
    sigaction(SIGTTIN, &sigact, NULL);         /* ...                                   */
#endif
    if (cfg.execute.loglevel >= LOG_DEBUG)
        syslog(LOG_DEBUG, "Unwanted signals ignored... [OK]");

    // set file creation mask
    umask(0);
    if (cfg.execute.loglevel >= LOG_DEBUG)
        syslog(LOG_DEBUG, "File creation mode set to zero... [OK]");

    /*
     * Set resource limits to allow core dumps
     * (when -D_DEBUG is defined)
     * CAP_SYS_RESOURCE, but we're superuser anyway
     */
#ifdef _DEBUG
    struct rlimit rlim = { .rlim_cur = 16777216, .rlim_max = 16777216 }; // 16 MB
    if (setrlimit(RLIMIT_CORE, &rlim))
    {
        syslog(LOG_ERR, "Unable to set RLIMIT_CORE resource: code %d (%s)", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
#endif
    
    /*
     * Create a new SID for the child process
     * This way when our parent dies, daemon child is not automatically killed along it.
     * NOTE: This is not desired in nodaemon -mode! shell termination should
     *       terminate this process as well.
     * TEST THIS!!
     */
    pid_t sid = setsid();
    if (sid < 0)
    {
        syslog(LOG_ERR, "Unable to create a new session, code %d (%s)", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
    else
    {
        if (cfg.execute.loglevel >= LOG_DEBUG)
            syslog(LOG_DEBUG, "New session (%d) created... [OK]", sid);
    }

    /* Change the current working directory. This prevents the execution
     * directory from being locked; hence not being able to remove it.
     */
#ifdef _DEBUG
#define DAEMON_WORKINGDIRECTORY     "/tmp"
#else
#define DAEMON_WORKINGDIRECTORY     "/"
#endif
    if ((chdir(DAEMON_WORKINGDIRECTORY)) < 0)
    {
        syslog(
              LOG_ERR,
              "Unable to change directory to \"%s\", code %d (%s)",
              DAEMON_WORKINGDIRECTORY,
              errno,
              strerror(errno)
              );
        exit(EXIT_FAILURE);
    }
#ifndef _DEBUG
    if (cfg.execute.loglevel >= LOG_DEBUG)
#endif
        syslog(
              LOG_DEBUG,
              "Working directory set to \"%s\" ... [OK]",
              DAEMON_WORKINGDIRECTORY
              );
    

    // remap streams but only if run as daemon
    if (run_as_daemon)
    {
#ifdef _DEBUG
        freopen("/dev/null",       "r", stdin);
        freopen("/tmp/stdout.txt", "a+", stdout);
        freopen("/tmp/stderr.txt", "a+", stderr);
        if (errno)
            syslog(LOG_DEBUG, "freopen() : errno(%d) \"%s\"", errno, strerror(errno));
#else
        //
        // Redirect standard files to /dev/null
        // From now onwards, no more terminal messages!
        //
        freopen("/dev/null", "r", stdin);
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
#endif /* _DEBUG */
    }

    /*
     * Establish pidfile to prevent multiple instances
     * This MUST be called by the child process itself
     * because child processes never inherit file locks.
     */
    if (pidfile_lock(DAEMON_PIDFILE))   // pidfile.c
    {
        syslog(LOG_ERR, "PID file creation failed!");
        if (run_as_daemon)
        {
            // Using SIGUSR2 to signal "another process already running"
            kill(getppid(), SIGUSR2);
        }
        exit(EXIT_FAILURE);
    }
    else
    {
        if (cfg.execute.loglevel >= LOG_DEBUG)
            syslog(LOG_DEBUG, "PID lock file \"%s\" created ... [OK]", DAEMON_PIDFILE);
    }

    if (run_as_daemon)
    {
        // Report successful daemon start SIGUSR1 to parent
        if (cfg.execute.loglevel >= LOG_DEBUG)
            syslog(LOG_DEBUG, "daemon signaling successful start to parent (pid %d)", getppid());
        kill(getppid(), SIGUSR1);
        
        /*
         * Give parent time to die, so that we can start using logmsg()
         * without lost messages due to getppid() returning non-1...
         */
        sleep(1);
        
        // Register our PID to the logwrite.c, so that our child processes,
        // the dataloggers, can also have their messages delivered to syslog.
        logwrite_register_daemon_pid(getpid());
    }

}


/***************************************************************************************
 * PRE-DAEMON ROUTINES
 *
 *  Prepares any facilities necessary before the process is daemonized.
 */
#define NUM_SQLITE3_INSERT_TESTS    4
static int predaemon_initialize()
{
    // Test if so configured
    if (cfg.execute.tmpfs == AUTO)
    {
        dbperf_t *dbperf;   // Pointer to static struct in function - do not free!
        if (!(dbperf = database_testwriteperf(NUM_SQLITE3_INSERT_TESTS)))   // database.c
        {
            logerr("SQLite3 write performance test failed!");
            return EXIT_FAILURE;
        }
        // check 
        if (dbperf->mean > CFG_MAX_INSERT_DELAY_MEAN ||
            dbperf->max  > CFG_MAX_INSERT_DELAY_MAX)
        {
            logmsg(
                  LOG_INFO,
                  "SQLite3 database write performance is below accepted!"
                  );
            logmsg(
                  LOG_INFO,
                  "Required values: mean < %5.2f ms, max < %5.2f ms",
                  CFG_MAX_INSERT_DELAY_MEAN,
                  CFG_MAX_INSERT_DELAY_MAX
                  );
            logmsg(
                  LOG_INFO,
                  "Results: (n=%d) Min %5.2f ms Mean %5.2f ms Max %5.2f ms StdDev %4.2f",
                  dbperf->n,
                  dbperf->min,
                  dbperf->mean,
                  dbperf->max,
                  dbperf->stddev
                  );
            logmsg(
                  LOG_INFO,
                  "Ramdisk (tmpfs) will be created"
                  );
            cfg.execute.tmpfs = TRUE;
        }
        else
        {
            logmsg(
                  LOG_INFO,
                  "Results: (n=%d) Min %5.2f ms Mean %5.2f ms Max %5.2f ms StdDev %4.2f",
                  dbperf->n,
                  dbperf->min,
                  dbperf->mean,
                  dbperf->max,
                  dbperf->stddev
                  );
            logdev("SQLite3 write performance figures OK");
            cfg.execute.tmpfs = FALSE;
        }
        errno = 0;
    }
    // Now cfg.execute.tmpfs is no longer AUTO
    if (cfg.execute.tmpfs == TRUE)
    {
        if (tmpfs_mount(DAEMON_TMPFS_MOUNTPOINT, DAEMON_TMPFS_SIZEMB))  // tmpfs.c
        {
            logerr("Failed to mount tmpfs!");
            return EXIT_FAILURE;
        }
        cfg.database.tmpfsfilename = strdup(DAEMON_TMPFS_DATABASEFILE);

        // create the SQLite3 datafile
        if (user_set_eugid(DAEMON_RUN_AS_USER) == EXIT_FAILURE) // user.c
        {
            int savederrno = errno;
            logmsg(LOG_ERR, "Unable to assume effective UID of \"%s\"!", DAEMON_RUN_AS_USER);
            return (errno = savederrno, EXIT_FAILURE);
        }

        if (database_initialize(cfg.database.tmpfsfilename))
        {
            logerr("Failed creating database file in tmpfs!");
            return EXIT_FAILURE;
        }

        if (user_restore_eugid())               // user.c
        {
            logerr("Failed to restore effective UID and GID!");
            return EXIT_FAILURE;
        }

        // Insert a new event into the actual schedule
        if (event_create(
                        EVENT_ACTION_IMPORTTMPFS,        // .action
                        DAEMON_IMPORTTMPFS_INTERVAL      // .localoffset
                        ))
        {
            logerr("Failed to create periodic event to save tmpfs data");
            return EXIT_FAILURE;
        }

        logdev("tmpfs database creation completed.");
    }   
    // Another piece of code here might be to initialize the
    // remote mains controll... TBA

    logdev("Initialization routines completed.");
    return EXIT_SUCCESS;
}

/***************************************************************************************
 * SPECIAL COMMAND : Test SQLite3 database write performance
 *
 *  Executes repeated INSERTs and measures time it takes.
 */
static int cmd_testdbperf(int nsamples)
{
    double    time_elapsed;
    dbperf_t *dbperf;

    if (nsamples < 0)
        return (errno = EINVAL, EXIT_FAILURE);

    logmsg(LOG_ERR, "Testing SQLite3 write performance... Please wait.");

    /*
     * Clock the operation
     */
    xtmr_t *t = xtmr();     // util.c

    /*
     * Database file does not exist (anymore)
     * Hand processing over to database.c:db_initialize()
     */
    if (!(dbperf = database_testwriteperf(nsamples)))
        return EXIT_FAILURE;

    time_elapsed = xtmrlap(t);

    /*
     * Report
     */
    logmsg(
          LOG_ERR,
          "Results: (n=%d) Min %5.2f ms Mean %5.2f ms Max %5.2f ms StdDev %4.2f",
          dbperf->n,
          dbperf->min,
          dbperf->mean,
          dbperf->max,
          dbperf->stddev
          );
    logmsg(
          LOG_INFO,
          "Database write performance executed in %.2f seconds.",
          time_elapsed / 1000
          );
    free(t);

    return (errno = 0, EXIT_SUCCESS);
}

/***************************************************************************************
 * SPECIAL COMMAND : Create SQLite3 database file
 *
 *  Intended use case: Database file does not exist (program is being taken into
 *  usage or old has been removed). Superuser gives the commandline directive
 *  and the program creates the database with empty tables.
 */
static int cmd_initdb(char *filename)
{

    /*
     * Determine if specified databasefile already exists
     * and remove with user's approval.
     * Program is executed as root and with those privileges
     * we are certain to succeed in removal.
     * (means; do not change effective UID until we create the new)
     */
    if (euidaccess(filename, F_OK) != -1)
    {
        /*
         * File exists
         */
        if (euidaccess(filename, W_OK | R_OK) == -1)
        {
            /* Extremely unlikely with root privileges, but... */
            logmsg(
                  LOG_ERR,
                  "Database file (\"%s\") is not readable and writable!",
                  filename
                  );
            return (errno = EACCES, EXIT_FAILURE);
        }

        /*
         * Confirm user that existing one will be replaced
         */
        logmsg(
              LOG_ERR,
              "Database (\"%s\") already exists!",
              filename
              );
        if (!ttyprompt("Are you sure you want to overwrite? (y/n) : ")) // ttyinput.c
        {
            return (errno = ECANCELED, EXIT_CANCELLED);
        }

        /*
         * Delete existing file
         * Using unlink() to avoid directory deletes
         */
        if (unlink(filename))
        {
            int savederrno = errno;
            logmsg(LOG_ERR, "Unable to delete file \"%s\"\n", filename);
            return (errno = savederrno, EXIT_FAILURE);
        }
    }

    /*
     * Clock the operation
     */
    xtmr_t *t = xtmr();     // util.c

    /*
     * Change effective user privileges to that of the daemon user
     * (defined in config.h as DAEMON_RUN_AS_USER)
     */
    if (user_set_eugid(DAEMON_RUN_AS_USER) == EXIT_FAILURE)
    {
        int savederrno = errno;
        logmsg(LOG_ERR, "Unable to assume effective UID of \"%s\"!", DAEMON_RUN_AS_USER);
        return (errno = savederrno, EXIT_FAILURE);
    }

    /*
     * Database file does not exist (anymore)
     * Hand processing over to database.c:db_initialize()
     */
    if (database_initialize(filename))      // database.c
        return EXIT_FAILURE;
    if (user_restore_eugid())               // user.c
        return EXIT_FAILURE;

    /*
     * report elapset creation time
     */
    logmsg(
          LOG_INFO,
          "New database created in %.2f seconds.\n",
          xtmrlap(t) / 1000
          );
    free(t);

    return (errno = 0, EXIT_SUCCESS);
}

/***************************************************************************************
 * SPECIAL COMMAND : Write configuration file
 *
 *  Intended use case: Compiled-in or existing config file settings are not
 *  satisfactory. Superuser defines settings he wishes to change in commandline
 *  and includes the "-writeconfig" command. Commandline options take precedance
 *  (they always do) and the command executes, writing corrected configuration
 *  into a new configuration file.
 *
 */
static int cmd_writeconfig(char *filename)
{
    /*
     * If configuration file already exists, prompt confirmation
     * from the user before truncating.
     */
    if (euidaccess(filename, F_OK) != -1)
    {
        /* Check for necessary access
         */
        if (access(filename, W_OK | R_OK) == -1)
        {
            logmsg(
                  LOG_ERR,
                  "Configuration file (\"%s\") is not readable and writable!",
                  filename
                  );
            return (errno = EACCES, EXIT_FAILURE);
        }
        /*
         * We seem to have access to delete the existing file.
         * Confirm user that existing one will be replaced.
         */
        logmsg(
              LOG_ERR,
              "Configuration file (\"%s\") already exists!",
              filename
              );
        if (!ttyprompt("Are you sure you want to overwrite? (y/n) : ")) // ttyinput.c
        {
            return (errno = ECANCELED, EXIT_CANCELLED);
        }

        /*
         * Delete existing file
         * Using unlink() to avoid directory deletes
         */
        if (unlink(filename))
        {
            int savederrno = errno;
            logmsg(LOG_ERR, "Unable to delete file \"%s\"\n", filename);
            return (errno = savederrno, EXIT_FAILURE);
        }
    }

    /*
     * Clock the operation
     */
    xtmr_t *t = xtmr();     // util.c

    /*
     * Configuration file does not exist (anymore)
     * Hand processing over to config.c:cfg_writefile()
     */
    if (cfg_writefile(filename))
        return EXIT_FAILURE;

    logmsg(
          LOG_INFO,
          "New configuration file created in %.2f seconds.\n",
          xtmrlap(t) / 1000
          );
    free(t);

    return (errno = 0, EXIT_SUCCESS);
}

/***************************************************************************************
 *
 * MAIN
 */
int main(int argc, char **argv)
{
    /*
     * Store commandline for daemon and SIGHUP
     */
    cfg_save_argv(argv);

    /*
     * Initialize configuration values with compiled-in defaults
     * THIS IS HAS TO BE DONE BEFORE ANY logmsg() CALLS!
     * Otherwise the config_t *cfg is uninitialized and logmsg()
     * segfaults.
     */
    cfg_init(&cfg);

    logdev("Starting XTimer"); // Automatic, on first call to logdev()

    /*
     * Display program header with version information
     * (printed into stderr)
     */
    cfg_prog_header();

    /*
     * This program must be run as root
     */
    if (geteuid() != 0)
    {
        logmsg(LOG_ERR, "This program must be invoked with root privileges!\nExiting...\n");
        return EXIT_FAILURE;
    }

    /*
     * Create copy of configuration values.
     * This will be used to attempt to read configuration file and
     * commandline, without committing the changes.
     * NOTE: Shallow copy
     */
    config_t *newcfg = cfg_dup(&cfg);

    /*
     * Pre-read commandline arguments
     *
     * Three items are looked for;
     *  - Alternate config file
     *  - Create database -command
     *  - Create config -command
     *
     * If the corresponding create-command has been given, it is no longer
     * required that the file exists.
     *
     * If neither of the create-commands have been issues, it is still
     * necessary to know if the user has specified an alternate config -file
     * so that it is read instead of the compiled-in default. This is due to
     * the order in which commandline arguments are read last and overwrite
     * any of the compiled-in defaults or config-file values.
     */
    if (cfg_preread_commandline(newcfg, argv))
        return EXIT_FAILURE;

    /*
     * STEP 1: Read config file ...
     *         The function will output complaints, no output here necessary
     */
    if (cfg_read_file(newcfg))
        return EXIT_FAILURE;
    else
        errno = 0;  // not interested if return was EXIT_SUCCESS

    /*
     * STEP 2:  Read commandline ...
     *          ...and override values read from the config file above.
     *          Function will verbose on problems.
     */
    if (cfg_read_argv(newcfg, argv))
        return EXIT_FAILURE;

    /*
     * No errors, apply changes and then free the buffer
     */
    cfg_commit(newcfg);
    cfg_free(newcfg);

    /*
     * All configuration values have been processed. Now set the logging filter
     * value.
     */
    logwrite_set_logmsg_filter(cfg.execute.loglevel);

    /*
****** SPECIAL COMMAND: INIT_DATABASE
     *
     *	If database initialization was requested, the program will try
     *	to do that and exits. EXECUTION WILL NOT PROCEED!
     */
    if (cfg.cmd.createdatabase)
    {
        int rc;
        if ((rc = cmd_initdb(cfg.database.filename)) == EXIT_CANCELLED)
        {
            logmsg(LOG_ERR, "Initialization cancelled! Existing database left untouched.\n");
            exit(EXIT_FAILURE);
        }
        else if (rc == EXIT_FAILURE)
        {
            logmsg(LOG_ERR, "Database initialization failed!");
            exit(EXIT_FAILURE);
        }
    }

    /*
****** SPECIAL COMMAND: WRITE CONFIGURATION FILE
     *
     *	Write current cfg data into the specified .config -file.
     */
    if (cfg.cmd.createconfigfile)
    {
        int rc;
        if ((rc = cmd_writeconfig(cfg.filename)) == EXIT_CANCELLED)
        {
            logmsg(LOG_ERR, "Action cancelled! Existing configuration file left untouched.\n");
            exit(EXIT_FAILURE);
        }
        else if (rc == EXIT_FAILURE)
        {
            logmsg(LOG_ERR, "Writing configuration file failed!");
            exit(EXIT_FAILURE);
        }
    }

    /*
****** SPECIAL COMMAND: TEST SQLITE3 WRITE PERFORMANCE
     *
     */
    if (cfg.cmd.testdbwriteperf)
    {
        if (cmd_testdbperf(cfg.cmd.testdbwriteperf))
        {
            logmsg(LOG_ERR, "Database write performance test failed!");
            exit(EXIT_FAILURE);
        }
    }

    /*
     * Exit if any special commands were executed
     */
    if (cfg.cmd.createdatabase ||
        cfg.cmd.createconfigfile ||
        cfg.cmd.testdbwriteperf)
    {
        logdev("Commands executed successfully. Exiting...");
        return EXIT_SUCCESS;
    }

    /*
     * Perform a final configuration check before starting up.
     * Function needs to resolve absolute filepaths and (re)check
     * access to database and configuration file (SIGHUP) as well as 
     * generate schedule successfully (but not commit yet).
     */
    if (cfg_check(&cfg))
    {
        logdev("cfg_check() exit");
        return EXIT_FAILURE;
    }

    /*
     * Perform small test on SQLite3 write performance.
     * If the results are bad;
     *      1. Mount small tmpfs ("ramdisk").
     *      2. Create an event to periodically move
     *         the accumulated data to real database.
     *      3. Reconfigure this daemon so that datalogger
     *         processes write into the tmpfs instead.
     *      4. Commit schedule into use.
     */
    if (predaemon_initialize())
    {
        logdev("predaemon_initialize() exit");
        return EXIT_FAILURE;
    }

    /* Initialize syslog */
    logwrite_init(DAEMON_NAME, cfg.execute.loglevel);

cfg_print(&cfg, LOG_ERR, "=== Final configuration ===");
    /*
****** Daemonize
     */
    logdev("Will%s run the daemon as background process", cfg.execute.as_daemon ? "" : " NOT");
    daemonize(cfg.execute.as_daemon);

    /*
     * At this point, we are either a child (aka. daemon who's parent already died)
     * OR the original process that was instructed NOT to create a daemon.
     * Which ever it is, it will enter the daemon_main.c:daemon_main().
     */
    daemon_main();  // daemon.c

    /*
     * Execution should NEVER reach here.
     */
    logerr("daemon_main() returned! Should not be possible!");

    /* If we are here, at least close syslog */
    if (cfg.execute.as_daemon)
        closelog();

    return EXIT_SUCCESS;
}

/* EOF main.c */
