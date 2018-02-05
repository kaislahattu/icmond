/*
 * config.c - 2016 Jani Tammi <janitammi@gmail.com>
 */
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>             // vargs
#include <stdlib.h>             // malloc()
#include <stdbool.h>            // TRUE and FALSE
#include <string.h>             // strchr(), memset()
#include <ctype.h>              // isspace()
#include <unistd.h>             // getcwd(), euidaccess()
#include <errno.h>
#include <syslog.h>
#include <limits.h>
#include <malloc.h>

#include "config.h"
#include "version.h"
#include "logwrite.h"
#include "user.h"
#include "event.h"
#include "keyval.h"
#include "util.h"

/******************************************************************************
 * Global storage of config.
 * There must be only one instance of configuration data,
 * and it must exist always.
 *
 * String values are set in cfg_init()
 */
config_t cfg =
{
    .filename               = { CFG_DEFAULT_FILECONFIG },
    .database =
    {
        .filename           = { CFG_DEFAULT_FILEDATABASE },
        .tmpfsfilename      = NULL
    },
    .inet =
    {
        .pingtimeout        = CFG_DEFAULT_INET_PINGTIMEOUT,
        .pinghosts          = NULL
    },
    .cmd =
    {
        .createdatabase     = false,
        .createconfigfile   = false,
        .testdbwriteperf    = false
    },
    .execute =
    {
        .as_daemon          = CFG_DEFAULT_EXE_ASDAEMON,
        .tmpfs              = CFG_DEFAULT_EXE_TMPFS,
        .interval           = CFG_DEFAULT_EXE_INTERVAL,
        .loglevel           = CFG_DEFAULT_EXE_LOGLEVEL
    },
    .modem =
    {
        .powercontrol       = CFG_DEFAULT_MODEM_POWERCONTROL,
        .powerupdelay       = CFG_DEFAULT_MODEM_POWERUPDELAY,
        .ip                 = { CFG_DEFAULT_MODEM_IP },
        .pingtimeout        = CFG_DEFAULT_MODEM_PINGTIMEOUT,
        .scrubber =
        {
            .filename       = { CFG_DEFAULT_MODEM_SCRUBBER },
            .timeout        = CFG_DEFAULT_MODEM_SCRUBBERTIMEOUT
        }
    },
    .event =
    {
        .apply_dst          = CFG_DEFAULT_EVENT_APPLYDST,
        .liststring         = NULL
    }
};

/*
 * Commandline argument vector, saved for daemon and SIGHUP
 */
char **cmdline;


// Copied from <syslog.h> ...something robust should be deviced instead
#define CONFIG_LOGLEVELARRAY_MAXINDEX   7
static char *_loglevel[] =
{
    "LOG_EMERG",        // 0
    "LOG_ALERT",
    "LOG_CRIT",
    "LOG_ERR",          // 3
    "LOG_WARNING",
    "LOG_NOTICE",
    "LOG_INFO",         // 6
    "LOG_DEBUG",        // 7
    NULL
};

/*****************************************************************************/


/*
 * Function to store commandline argument array
 */
void cfg_save_argv(char **argv)
{
    cmdline = argv;
}

/*
 * Shallow copy - will NOT duplicate buffers pointed by members.
 */
config_t *cfg_dup(config_t *config)
{
    config_t *c = calloc(1, sizeof(config_t));
    memcpy(c, config, sizeof(config_t));
    return c;    
}

/*
 * Replaced config_t cfg content with the specified buffer content
 */
void cfg_commit(config_t *config)
{
    if (!config)
    {
        logerr("NULL pointer argument!");
        errno = EINVAL;
        return;
    }
    // free() buffers pointed to by original config_t cfg
    // IF the new configuration data points to new buffers.
    if (cfg.inet.pinghosts && cfg.inet.pinghosts != config->inet.pinghosts)
        free(cfg.inet.pinghosts);
    if (cfg.event.liststring && cfg.event.liststring != config->event.liststring)
        free(cfg.event.liststring);
    if (cfg.database.tmpfsfilename && cfg.database.tmpfsfilename != config->database.tmpfsfilename)
        free(cfg.database.tmpfsfilename);
    // Copy temporary config_t's content over to actual cfg
    memcpy(&cfg, config, sizeof(config_t));
    errno = 0;
    return;
}

/*
 * Destroy config_t heap
 */
void cfg_free(config_t *config)
{
    // Cannot free cfg, obviously
    if (!config || config == &cfg)
    {
        errno = EINVAL;
        return;
    }
    // Release buffers allocated for the now-discarded tmpcfg
    // BUT ONLY IF they're not used by the actual config_t cfg
    if (cfg.event.liststring != config->event.liststring)
        free(config->event.liststring);
    if (cfg.inet.pinghosts != config->inet.pinghosts)
        free(config->inet.pinghosts);
    if (cfg.database.tmpfsfilename != config->database.tmpfsfilename)
        free(cfg.database.tmpfsfilename);
    // Release the config_t buffer
    free(config);
}

char *cfg_loglevel_val2str(int loglevel)
{
    if (loglevel > CONFIG_LOGLEVELARRAY_MAXINDEX)
        return (errno = EINVAL, NULL);
    return (errno = 0, _loglevel[loglevel]);
}

/*
 * Case insensitive
 * Returns the index (and at the same time, the value of defined label)
 * from config_loglevel array.
 * If the string is not found, -1 is returned
 */
int cfg_loglevel_str2val(const char *logstr)
{
    if (!logstr || !*logstr)
        return (errno = EINVAL, -1);
    int idx;
    for (idx = 0; _loglevel[idx]; idx++)
    {
        if (eqlstrnocase(logstr, _loglevel[idx]))
            break;
    }
    if (!_loglevel[idx])
        return (errno = EINVAL, -1);
    return (errno = 0, idx);
}



/*
 * cfg_prog_header()
 *
 *      Only used to print out program header during the startup.
 */
void cfg_prog_header()
{
    fprintf(stderr, "\n%s ver. %s - %s\n", DAEMON_NAME, DAEMON_VERSION, DAEMON_HEADER);
    fprintf(stderr, "Build %s, gcc ver. %s\n", daemon_build, GNUC_VERSION);
    fprintf(stderr, "Distributed under the terms of the GNU General Public License\n");
    fprintf(stderr, "http://www.gnu.org/licenses/gpl.txt\n\n");
}

/*
 * cfg_prog_usage()
 *
 * PRIVATE
 *
 * RETURNS
 */
void cfg_prog_usage()
{
    /* this will not be used as daemon, so console output is OK */
    fprintf(stderr, "Usage:    %s [COMMAND] [OPTION=VALUE]...\n\n", DAEMON_NAME);
    fprintf(stderr, "    OPTION       DESCRIPTION                 DEFAULT VALUE\n");
    fprintf(stderr, "    -hosts       Target host's Name or IP    \"%s\"\n", CFG_DEFAULT_INET_PINGHOSTS);
    fprintf(stderr, "    -interval    Logging interval (seconds)  %-6d [%d - %d]\n", CFG_DEFAULT_EXE_INTERVAL, CFG_MIN_EXE_INTERVAL, CFG_MAX_EXE_INTERVAL);
    fprintf(stderr, "    -timeout     Ping timeout (milliseconds) %-6d [%d - %d]\n", CFG_DEFAULT_INET_PINGTIMEOUT, CFG_MIN_PING_TIMEOUT, CFG_MAX_PING_TIMEOUT);
    fprintf(stderr, "    -daemon      Run as daemon               %-6s [TRUE | FALSE]\n", CFG_DEFAULT_EXE_ASDAEMON ? "TRUE" : "FALSE");
    fprintf(stderr, "    -ramdisk     Use tmpfs as intermediate   %-6s [TRUE | FALSE | AUTO]\n", CFG_DEFAULT_EXE_TMPFS == 2 ? "AUTO" : (CFG_DEFAULT_EXE_TMPFS ? "TRUE" : "FALSE"));
    fprintf(stderr, "    -loglevel    Execution message details   \"%s\" [LOG_ERR | LOG_INFO | LOG_DEBUG]\n", cfg_loglevel_val2str(CFG_DEFAULT_EXE_LOGLEVEL));
    fprintf(stderr, "    -database    Database file               \"%s\"\n", CFG_DEFAULT_FILEDATABASE);
    fprintf(stderr, "    -config      Alternate config file       \"%s\"\n", CFG_DEFAULT_FILECONFIG);
    fprintf(stderr, "\n");
    fprintf(stderr, "    COMMAND      DESCRIPTION\n");
    fprintf(stderr, "    -createdb    Create or replace existing database:\n");
    fprintf(stderr, "                 \"%s\"\n", cfg.database.filename);
    fprintf(stderr, "    -writeconfig Create or replace existing configuration file:\n");
    fprintf(stderr, "                 \"%s\"\n", cfg.filename);
    fprintf(stderr, "    -testdbwrite Measure SQLite3 write performance.\n");
    fprintf(stderr, "                 Optionally number of samples can be defined;\n");
    fprintf(stderr, "                  \"-testdbwrite=40\"\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "NOTE:  Please make sure the config file is readable to the daemon process,\n");
    fprintf(stderr, "       if you want to be able to update config via config file and\n");
    fprintf(stderr, "       SIGHUP. Daemon process will execute as user '%s'.\n", DAEMON_RUN_AS_USER);
    fprintf(stderr, "NOTE2: The 'loglevel' setting DOES NOT affect monitoring data.\n");
    fprintf(stderr, "       Only the execution messages (usually, to syslog) are affected.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Example:\n\n    %s -hosts=www.google.com -interval=20\n\n", DAEMON_NAME);
}

/*
 * cfg_init()
 *
 *      Initializes the config structure with defaults.
 *      Does it to the provided config_t structure.
 *
 *
 */
void cfg_init(config_t *new)
{
    /*
     * Such a shame that C preprocessor does not offer support for sizeof() conditionals...
     * Now, the program has to be compiled with potentially too long defaults and this is
     * noticed only at first execution, with below asserts.
     *
     * C's preprocessor should be able to do better...
     */
    assert(strlen(CFG_DEFAULT_FILECONFIG)   <= CFG_MAX_FILENAME_LEN);
    assert(strlen(CFG_DEFAULT_FILEDATABASE) <= CFG_MAX_FILENAME_LEN);

    strncpy(new->filename, CFG_DEFAULT_FILECONFIG, sizeof(new->filename));
    new->execute.as_daemon      = CFG_DEFAULT_EXE_ASDAEMON;
    new->execute.tmpfs          = CFG_DEFAULT_EXE_TMPFS;
    new->execute.interval       = CFG_DEFAULT_EXE_INTERVAL;
    new->execute.loglevel       = CFG_DEFAULT_EXE_LOGLEVEL;
    strncpy(new->database.filename, CFG_DEFAULT_FILEDATABASE, sizeof(new->database.filename));
    new->database.tmpfsfilename = NULL;
    new->inet.pingtimeout       = CFG_DEFAULT_INET_PINGTIMEOUT;
    if (new->inet.pinghosts)
        free(new->inet.pinghosts);
    new->inet.pinghosts         = strdup(CFG_DEFAULT_INET_PINGHOSTS);
    new->modem.powercontrol     = CFG_DEFAULT_MODEM_POWERCONTROL;
    new->modem.powerupdelay     = CFG_DEFAULT_MODEM_POWERUPDELAY;
    strncpy(new->modem.ip, CFG_DEFAULT_MODEM_IP, sizeof(new->modem.ip));
    new->modem.pingtimeout      = CFG_DEFAULT_MODEM_PINGTIMEOUT;
    strncpy(new->modem.scrubber.filename, CFG_DEFAULT_MODEM_SCRUBBER, sizeof(new->modem.scrubber.filename));
    new->modem.scrubber.timeout = CFG_DEFAULT_MODEM_SCRUBBERTIMEOUT;
    new->cmd.createdatabase     = false;    // Obviously, no defaults for these two...
    new->cmd.createconfigfile   = false;
    new->cmd.testdbwriteperf    = false;
    new->event.apply_dst        = CFG_DEFAULT_EVENT_APPLYDST;
    // Avoid empty strings, use NULL instead
    if (new->event.liststring)
        free(new->event.liststring);
    if (strlen(CFG_DEFAULT_EVENT_STRING))
        new->event.liststring   = strdup(CFG_DEFAULT_EVENT_STRING);
    else
        new->event.liststring   = NULL;
}

/*
 * Special function that is to be called by main.c:main() only,
 * and prior to reading configuration file.
 * Will terminate 
 */
#define isopt(s, o) \
    (strncmp((s), (o), sizeof((o)) - 1) == 0)
int cfg_preread_commandline(config_t *cfgptr, /*int argc,*/ char **argv)
{
    int idx;
    for (idx = 0; argv[idx] /*&& idx < argc*/; idx++)
    {
        if (isopt(argv[idx], "-config="))
        {
            char *valstr = argv[idx] + sizeof("-config=") - 1;
            if (strlen(valstr) + 1 > sizeof(cfgptr->filename))
            {
                logmsg(
                      LOG_ERR,
                      "Specified configuration filename exceeds maximum "
                      "allowed length of %d characters",
                      sizeof(cfgptr->filename) - 1
                      );
                return (errno = EINVAL, EXIT_FAILURE);
            }
            strncpy(
                   cfgptr->filename,
                   valstr,
                   sizeof(cfgptr->filename)
                   );
        }
        else if (isopt(argv[idx], "-createdb"))
        {
            cfgptr->cmd.createdatabase = true;
        }
        else if (isopt(argv[idx], "-writeconfig"))
        {
            cfgptr->cmd.createconfigfile = true;
        }
    }
    // if new config file was specified WITHOUT createconfigfile,
    // the file must exist and be readable
    if (!eqlstr(cfgptr->filename, CFG_DEFAULT_FILECONFIG) && !cfgptr->cmd.createconfigfile)
    {
        if(!file_exist(cfgptr->filename))
        {
            logmsg(
                  LOG_ERR,
                  "Specified configuration file \"%s\" must exist, "
                  "unless it will be created by this program.\n"
                  "(configuration file is created if \"-writeconfig\" is given)",
                  cfgptr->filename
                  );
            return (errno = ENOENT, EXIT_FAILURE);
        }
        if (!file_useraccess(cfgptr->filename, DAEMON_RUN_AS_USER, R_OK))
        {
            logmsg(
                  LOG_ERR,
                  "Specified configuration file \"%s\" exists, "
                  "but is not readable to user \"%s\".",
                  cfgptr->filename,
                  DAEMON_RUN_AS_USER
                  );
            return (errno = EPERM, EXIT_FAILURE);
        }
    }
    // We leave the issue of weather or not we are allowed to write
    // the config file, to the actual write code...

    // cfg.cmd.createdatabase will be necessary when executing
    // cfg_read_file(), where we will allow missing database file
    // if we know that we'll be creating it next
    return (errno = 0, EXIT_SUCCESS);
}
#undef isopt

/*
 * cfg_read_file()
 *
 *      Read configuration file and update config_t accordingly.
 *      IMPORTANT! Will read the file that is specified by the
*       parameter config_t pointer (tmpcfg->filename)
 *
 * RETURNS
 *      EXIT_SUCCESS    on success
 *      EXIT_FAILURE    on error
 * ERRNO
 *      ENOENT          No such file
 *      EINVAL          Value range/read/parse error
 */
int cfg_read_file(config_t *tmpcfg)
{
    keyval_t    kv;
    FILE        *fp;

    /*
     * If file does not exist
     */
    if (!file_exist(tmpcfg->filename))
    {
        /*
         * SPECIAL CASE: Configuration filename is the same as compiled in
         *               default and does not exist. This is allowed.
         */
        if (eqlstr(tmpcfg->filename, CFG_DEFAULT_FILECONFIG))
        {
#ifdef _DEBUG
            logdev(
#else
            logmsg(
                  LOG_INFO,
#endif
                  "INFO: Default configuration file \"%s\" does not exist (allowed, skipping file read).",
                  CFG_DEFAULT_FILECONFIG
                  );
            return (errno = ENOENT, EXIT_SUCCESS);
        }
        /*
         * In any other case, the used has manually specified the config file
         * We cannot accept a missing file when it is specified by user!
         */
        else
        {
            logmsg(
                  LOG_ERR,
                  "ERROR: Specified configuration file \"%s\" does not exist! (user \"%s\")",
                  tmpcfg->filename,
                  user_get_ename()
                  );
            return (errno = ENOENT, EXIT_FAILURE);
        }
    }


    /*
     * if opening the existing file fails
     */
    if (!(fp = fopen(tmpcfg->filename, "r")))
    {
        int savederrno = errno;
        logmsg(LOG_ERR, "Unable to open config file '%s'!", tmpcfg->filename);
        return (errno = savederrno, EXIT_FAILURE);
    }

    /*
     * Read config file, line-by-line
     * We keep those keyval_t buffers that we need (hosts, for example)
     * and free() those that we do not (like interval).
     */
    int n_errors   = 0;
    int n_warnings = 0;
    int n_line     = 0;
    char line[CFG_MAX_CONFIGFILE_ROW_WIDTH];
    while (fgets(line, CFG_MAX_CONFIGFILE_ROW_WIDTH, fp))
    {
        n_line++;
        if ((kv = keyval_create(line)))
        {
            // Parameters we DO NOT read from config file:
            // cfg.filename
            // cfg.cmd.createdatabase
            // cfg.cmd.createconfigfile
// DAEMON (cfg.execute.as_daemon)
            if (keyval_iskey(kv, "daemon"))
            {
                if (eqlstrnocase(kv[1], "TRUE"))
                {
                    tmpcfg->execute.as_daemon = true;
                }
                else if (eqlstrnocase(kv[1], "FALSE"))
                {
                    tmpcfg->execute.as_daemon = false;
                }
                else
                {
                    logmsg(
                          LOG_INFO,
                          "%s(%d): parameter for key 'daemon' (\"%s\") unrecognized [TRUE|FALSE].",
                          tmpcfg->filename,
                          n_line,
                          kv[1]
                          );
                    n_errors++;
                }
                free(kv);
                continue;
            }
// INTERVAL (cfg.execute.interval)
            else if (keyval_iskey(kv, "interval"))
            {
                tmpcfg->execute.interval = atoi(kv[1]);
                if (tmpcfg->execute.interval < CFG_MIN_EXE_INTERVAL ||
                    tmpcfg->execute.interval > CFG_MAX_EXE_INTERVAL)
                {
                    logmsg(
                          LOG_ERR,
                          "%s(%d): parameter 'interval' (%d) out of bounds [%d-%d].",
                          tmpcfg->filename,
                          n_line,
                          tmpcfg->execute.interval,
                          CFG_MIN_EXE_INTERVAL,
                          CFG_MAX_EXE_INTERVAL
                          );
                    n_errors++;
                }
                free(kv);
                continue;
            }
// LOGLEVEL (cfg.execute.loglevel)
            else if (keyval_iskey(kv, "loglevel"))
            {
                tmpcfg->execute.loglevel = cfg_loglevel_str2val(kv[1]);
                if (errno != 0)
                {
                    logmsg(
                          LOG_INFO,
                          "%s(%d): parameter 'loglevel' is invalid. (\"%s\")",
                          tmpcfg->filename,
                          n_line,
                          kv[1]
                          );
                    n_errors++;
                }
                free(kv);
                continue;
            }
// DATABASE (cfg.database.filename)
            else if (keyval_iskey(kv, "database"))
            {
                if (strlen(kv[1]) <= CFG_MAX_FILENAME_LEN)
                {
                    // If create -command is issued, the file does not need
                    // to exist nor do we need to check access to existing.
                    // Creation will be executed as root.
                    if (!tmpcfg->cmd.createdatabase)
                    {
                        if (!file_exist(kv[1]))
                        {
                            logmsg(
                                  LOG_ERR,
                                  "%s(%d): database (\"%s\") does not exist.",
                                  tmpcfg->filename,
                                  n_line,
                                  CFG_MAX_FILENAME_LEN
                                  );
                            n_errors++;
                        }
                        // Existing file must always be Read-Write
                        else if (!file_useraccess(kv[1], DAEMON_RUN_AS_USER, R_OK | W_OK))
                        {
                            logmsg(
                                  LOG_ERR,
                                  "%s(%d): user \"%s\" has no R/W access to database (\"%s\").",
                                  tmpcfg->filename,
                                  n_line,
                                  DAEMON_RUN_AS_USER,
                                  kv[1]
                                  );
                            n_errors++;
                        }
                        else
                        {
                            // No create-command and access is OK
                            strcpy(tmpcfg->database.filename, kv[1]);
                        }
                    } // if (!cfg.cmd.createdatabase)
                    else
                    {
                        // Create command is issued
                        strcpy(tmpcfg->database.filename, kv[1]);
                    }
                }
                else
                {
                    logmsg(
                          LOG_ERR,
                          "%s(%d): parameter 'database' is too long [max %d characters].",
                          tmpcfg->filename,
                          n_line,
                          CFG_MAX_FILENAME_LEN
                          );
                    n_errors++;
                }
                free(kv);
                continue;
            }
// RAMDISK (cfg.execute.tmpfs)
            if (keyval_iskey(kv, "ramdisk"))
            {
                // cfg.database.tmpfsfilename will be assigned when tmpfs is mounted
                if (eqlstrnocase(kv[1], "TRUE"))
                {
                    tmpcfg->execute.tmpfs = TRUE;
                }
                else if (eqlstrnocase(kv[1], "FALSE"))
                {
                    tmpcfg->execute.tmpfs = FALSE;
                }
                else if (eqlstrnocase(kv[1], "AUTO"))
                {
                    tmpcfg->execute.tmpfs = AUTO;
                }
                else
                {
                    logmsg(
                          LOG_INFO,
                          "%s(%d): parameter for key 'ramdisk' (\"%s\") unrecognized [TRUE|FALSE|AUTO].",
                          tmpcfg->filename,
                          n_line,
                          kv[1]
                          );
                    n_errors++;
                }
                free(kv);
                continue;
            }
// PING HOSTS (cfg.inet.pinghosts)
            else if (keyval_iskey(kv, "inet pinghosts"))
            {
                keyval_remove_empty_values(kv);
                if (keyval_nvalues(kv))
                {
                    // DO NOT free existing inet.pinghosts buffer!
                    // If you do, the real array disappears and
                    // we may end up discarding this read attempt.
                    tmpcfg->inet.pinghosts = keyval2valstr(kv);
                    // TODO: has values, check them
                    if (0) // replace with call to check_function()
                    {
                        logmsg(
                              LOG_ERR,
                              "%s(%d): parameter 'inet pinghosts' has error(s).",
                              tmpcfg->filename,
                              n_line
                              );
                        free(tmpcfg->inet.pinghosts);
                        tmpcfg->inet.pinghosts = NULL;
                        n_errors++;
                    }
                }
                // else no values - program just won't ping inet host(s)
                // tmpcfg.inet.pinghosts will remain NULL
                else
                {
                    // contains no values, free useless keyval_t
                    free(kv);
                    tmpcfg->inet.pinghosts = NULL;
                    // Do not increment n_errors, this is accepted outcome.
                }
                continue;
            }
// TIMEOUT (cfg.inet.pingtimeout)
            else if (keyval_iskey(kv, "inet pingtimeout"))
            {
                tmpcfg->inet.pingtimeout = atoi(kv[1]);
                if (tmpcfg->inet.pingtimeout < CFG_MIN_PING_TIMEOUT ||
                    tmpcfg->inet.pingtimeout > CFG_MAX_PING_TIMEOUT)
                {
                    logmsg(
                          LOG_INFO,
                          "%s(%d): parameter 'inet pingtimeout' (%d) is out of bounds [%d-%d].",
                          tmpcfg->filename,
                          n_line,
                          tmpcfg->inet.pingtimeout,
                          CFG_MIN_PING_TIMEOUT,
                          CFG_MAX_PING_TIMEOUT
                          );
                    n_errors++;
                }
                free(kv);
                continue;
            }
// MODEM POWERCONTROL (cfg.modem.powercontrol)
            if (keyval_iskey(kv, "modem powercontrol"))
            {
                if (eqlstrnocase(kv[1], "TRUE"))
                {
                    tmpcfg->modem.powercontrol = true;
                }
                else if (eqlstrnocase(kv[1], "FALSE"))
                {
                    tmpcfg->modem.powercontrol = false;
                }
                else
                {
                    logmsg(
                          LOG_INFO,
                          "%s(%d): parameter for key 'modem powercontrol' (\"%s\") unrecognized [TRUE|FALSE].",
                          tmpcfg->filename,
                          n_line,
                          kv[1]
                          );
                    n_errors++;
                }
                free(kv);
                continue;
            }
// MODEM POWERUPDELAY (cfg.modem.powerupdelay)
            else if (keyval_iskey(kv, "modem powerupdelay"))
            {
                tmpcfg->modem.powerupdelay = atoi(kv[1]);
                if (tmpcfg->modem.powerupdelay < CFG_MIN_MODEM_POWERUPDELAY ||
                    tmpcfg->modem.powerupdelay > CFG_MAX_MODEM_POWERUPDELAY)
                {
                    logmsg(
                          LOG_ERR,
                          "%s(%d): parameter 'modem powerupdelay' (%d) out of bounds [%d-%d].",
                          tmpcfg->filename,
                          n_line,
                          tmpcfg->modem.powerupdelay,
                          CFG_MIN_MODEM_POWERUPDELAY,
                          CFG_MAX_MODEM_POWERUPDELAY
                          );
                    n_errors++;
                }
                free(kv);
                continue;
            }
// MODEM IP (cfg.mode.ip)
            else if (keyval_iskey(kv, "modem ip"))
            {
                keyval_remove_empty_values(kv);
                if (keyval_nvalues(kv) == 1)
                {
                    if (strlen(kv[1]) > INET_ADDRSTRLEN)
                    {
                        logmsg(
                              LOG_INFO,
                              "%s(%d): parameter 'modem ip' too long . (\"%s\")",
                              tmpcfg->filename,
                              n_line,
                              kv[1]
                              );
                        n_errors++;
                    }
                    else
                        snprintf(tmpcfg->modem.ip, sizeof(tmpcfg->modem.ip), "%s", kv[1]);
                }
                else
                {
                    // Modem IP we simply must have
                    logmsg(
                          LOG_INFO,
                          "%s(%d): parameter 'modem ip' malformed. (\"%s\")",
                          tmpcfg->filename,
                          n_line,
                          kv[1]
                          );
                    n_errors++;
                }
                free(kv);
                continue;
            }
// MODEM TIMEOUT (cfg.modem.pingtimeout)
            else if (keyval_iskey(kv, "modem pingtimeout"))
            {
                tmpcfg->modem.pingtimeout = atoi(kv[1]);
                if (tmpcfg->modem.pingtimeout < CFG_MIN_PING_TIMEOUT ||
                    tmpcfg->modem.pingtimeout > CFG_MAX_PING_TIMEOUT)
                {
                    logmsg(
                          LOG_INFO,
                          "%s(%d): parameter 'modem pingtimeout' (%d) is out of bounds [%d-%d].",
                          tmpcfg->filename,
                          n_line,
                          tmpcfg->modem.pingtimeout,
                          CFG_MIN_PING_TIMEOUT,
                          CFG_MAX_PING_TIMEOUT
                          );
                    n_errors++;
                }
                free(kv);
                continue;
            }
// MODEM SCRUBBER (cfg.mode.scrubberp)
            else if (keyval_iskey(kv, "modem scrubber"))
            {
                if (keyval_nvalues(kv) == 1)
                {
                    if (strlen(kv[1]) > CFG_MAX_FILENAME_LEN)
                    {
                        logmsg(
                              LOG_INFO,
                              "%s(%d): parameter 'modem scrubber' too long . (\"%s\")",
                              tmpcfg->filename,
                              n_line,
                              kv[1]
                              );
                        n_errors++;
                    }
                    else
                        snprintf(tmpcfg->modem.scrubber.filename, sizeof(tmpcfg->modem.scrubber.filename), "%s", kv[1]);
                }
                else
                {
                    // Modem scrubber we simply must have
                    logmsg(
                          LOG_INFO,
                          "%s(%d): parameter 'modem scrubber' malformed. (\"%s\")",
                          tmpcfg->filename,
                          n_line,
                          kv[1]
                          );
                    n_errors++;
                }
                free(kv);
                continue;
            }
// MODEM SCRUBBERTIMEOUT (cfg.modem.scrubbertimeout)
            else if (keyval_iskey(kv, "modem scrubbertimeout"))
            {
                tmpcfg->modem.scrubber.timeout = atoi(kv[1]);
                if (tmpcfg->modem.scrubber.timeout < CFG_MIN_MODEM_SCRUBBERTIMEOUT ||
                    tmpcfg->modem.scrubber.timeout > CFG_MAX_MODEM_SCRUBBERTIMEOUT)
                {
                    logmsg(
                          LOG_INFO,
                          "%s(%d): parameter 'modem scrubbertimeout' (%d) is out of bounds [%d-%d].",
                          tmpcfg->filename,
                          n_line,
                          tmpcfg->modem.scrubber.timeout,
                          CFG_MIN_MODEM_SCRUBBERTIMEOUT,
                          CFG_MAX_MODEM_SCRUBBERTIMEOUT
                          );
                    n_errors++;
                }
                free(kv);
                continue;
            }
// SCHEDULE APPLYDST (cfg.event.apply_dst)
            if (keyval_iskey(kv, "schedule dst"))
            {
                if (eqlstrnocase(kv[1], "TRUE"))
                {
                    tmpcfg->event.apply_dst = true;
                }
                else if (eqlstrnocase(kv[1], "FALSE"))
                {
                    tmpcfg->event.apply_dst = false;
                }
                else
                {
                    logmsg(
                          LOG_INFO,
                          "%s(%d): parameter for key 'schedule dst' (\"%s\") unrecognized [TRUE|FALSE].",
                          tmpcfg->filename,
                          n_line,
                          kv[1]
                          );
                    n_errors++;
                }
                free(kv);
                continue;
            }
// SCHEDULE 
            else if (keyval_iskey(kv, "schedule"))
            {
                keyval_remove_empty_values(kv);
                if (keyval_nvalues(kv))
                {
                    tmpcfg->event.liststring = keyval2valstr(kv);
                    // TODO: has values, check them
                    if (0) // replace with call to check_function()
                    {
                        logmsg(
                              LOG_ERR,
                              "%s(%d): parameter 'event liststring' has error(s).",
                              tmpcfg->filename,
                              n_line
                              );
                        free(tmpcfg->event.liststring);
                        tmpcfg->event.liststring = NULL;
                        n_errors++;
                    }
                }
                else
                {
                    // contains no values, free useless keyval_t
                    free(kv);
                    tmpcfg->event.liststring = NULL;
                    // Do not increment n_errors, this is accepted outcome.
                }
                continue;
            }
// ==== UNKNOWN ====
            else
            {
                logmsg(
                      LOG_ERR,
                      "%s(%d): Warning! Unrecognized configuration setting '%s'",
                      tmpcfg->filename,
                      n_line,
                      kv[0]
                      );
                n_warnings++;
                free(kv);
            }
        } // if (kv)
        // ignore unknowns
    } // while (fgets(line))

    // If n_errors, complain and return
    if (n_errors)
    {
        // output with extra newline, for nice formatting
        logmsg(
              LOG_ERR,
              "%s: %d error%s and %d warnings in configuration file!\n",
              tmpcfg->filename,
              n_errors,
              n_errors > 1 ? "s" : "",
              n_warnings
              );
        return (errno = EINVAL, EXIT_FAILURE);
    }
    else if (n_warnings)
    {
        // output with extra newline, for nice formatting
        logmsg(
              LOG_ERR,
              "%s: %d warnings in configuration file.\n",
              tmpcfg->filename,
              n_warnings
              );
    }

    // return
    fclose(fp);
    return (errno = 0, EXIT_SUCCESS);
}


/*
 * cfg_read_argv()
 *
 * 	PUBLIC - Read configuration from commandline.
 *  Commandline arguments are parsed ONLY on
 *  startup and NEVER while running as daemon.
 *
 * RETURNS
 *      EXIT_SUCCESS    on success
 *      EXIT_FAILURE    on error
 */
/* sizeof() - 1, because we don't want to include the trailing '\0' */
#define isopt(s) \
    (strncmp(argv[argvidx], (s), sizeof((s)) - 1) == 0)
int cfg_read_argv(config_t *tmpcfg, /*int argc,*/ char **argv)
{
    int      n_errors = 0;
    int      argvidx;
    keyval_t kv;
    for(argvidx = 1; argv[argvidx] /*&& argvidx < argc*/; argvidx++)
    {
        kv = keyval_create(argv[argvidx]);
        /*
         * -loglevel=<LOG_DEBUG|LOG_INFO|LOG_ERR>
         */
		if (isopt("-loglevel="))
        {
            // May not be delimited list
            if (keyval_nvalues(kv) == 1)
            {
                tmpcfg->execute.loglevel = cfg_loglevel_str2val(kv[1]);
            }
            // Detect conversion problem
            if (tmpcfg->execute.loglevel == -1 || errno != 0)
            {
                logmsg(
                      LOG_ERR,
                      "%s: parameter 'loglevel' malformed. (\"%s\")",
                      DAEMON_NAME,
                      argv[argvidx]
                      );
                n_errors++;
            }
            free(kv);
        }
        /*
         * -hosts=<www.host.com>
         */
        else if (isopt("-hosts="))
        {
            // keyval2valstr() allocates a new buffer and
            // compiles delimited list string there
            tmpcfg->inet.pinghosts = keyval2valstr(kv);
            // Detect conversion problems
            if (
               errno != 0 ||
               !tmpcfg->inet.pinghosts ||
               strlen(tmpcfg->inet.pinghosts) == 0
               )
            {
                logmsg(
                      LOG_ERR,
                      "s%: parameter 'hosts' malformed. (\"%s\")",
                      DAEMON_NAME,
                      argv[argvidx]
                      );
                n_errors++;
            }
            free(kv);   // We can free() kv, see above
        }
        /*
         * -daemon=<TRUE|FALSE>
         */
        else if (isopt("-daemon="))
        {
            if (keyval_nvalues(kv) == 1 && eqlstrnocase(kv[1], "TRUE"))
            {
                tmpcfg->execute.as_daemon = true;
            }
            else if (keyval_nvalues(kv) == 1 && eqlstrnocase(kv[1], "FALSE"))
            {
                tmpcfg->execute.as_daemon = false;
            }
            else
            {
                logmsg(
                      LOG_ERR,
                      "%s: parameter out of bounds -- '%s'\n",
                      DAEMON_NAME,
                      argv[argvidx]
                      );
                n_errors++;
            }
            free(kv);
        }
        /*
         * -ramdisk=<TRUE|FALSE|AUTO>
         */
        else if (isopt("-ramdisk="))
        {
            if (keyval_nvalues(kv) == 1 && eqlstrnocase(kv[1], "TRUE"))
            {
                tmpcfg->execute.tmpfs = TRUE;
            }
            else if (keyval_nvalues(kv) == 1 && eqlstrnocase(kv[1], "FALSE"))
            {
                tmpcfg->execute.tmpfs = FALSE;
            }
            else if (keyval_nvalues(kv) == 1 && eqlstrnocase(kv[1], "AUTO"))
            {
                tmpcfg->execute.tmpfs = AUTO;
            }
            else
            {
                logmsg(
                      LOG_ERR,
                      "%s: parameter out of bounds -- '%s'\n",
                      DAEMON_NAME,
                      argv[argvidx]
                      );
                n_errors++;
            }
            free(kv);
        }
        // Not matched by above? Missing '='?
        else if (isopt("-ramdisk")) // interpreted as TRUE
        {
            tmpcfg->execute.tmpfs = TRUE;
        }
        /*
         * -nodaemon
         */
        else if (isopt("-nodaemon"))
        {
            tmpcfg->execute.as_daemon = false;
            free(kv);
        }
        /*
         * -interval=<integer>
         */
        else if (isopt("-interval="))
        {
            tmpcfg->execute.interval = atoi(kv[1]);
            if (tmpcfg->execute.interval < CFG_MIN_EXE_INTERVAL ||
                tmpcfg->execute.interval > CFG_MAX_EXE_INTERVAL)
            {
                logmsg(
                      LOG_ERR,
                      "%s: parameter out of bounds -- '%s'\n",
                      DAEMON_NAME,
                      argv[argvidx]
                      );
                n_errors++;
            }
            free(kv);
        }
        /*
         * -timeout=<integer>
         */
        else if (isopt("-timeout="))
        {
            tmpcfg->inet.pingtimeout = atoi(kv[1]);
            if (tmpcfg->inet.pingtimeout < CFG_MIN_PING_TIMEOUT ||
                tmpcfg->inet.pingtimeout > CFG_MAX_PING_TIMEOUT)
            {
                logmsg(
                      LOG_ERR,
                      "%s: parameter out of bounds -- '%s'\n",
                      DAEMON_NAME,
                      argv[argvidx]
                      );
                n_errors++;
            }
            free(kv);
        }
        /*
         * -config=<filepath string>
         */
        else if (isopt("-config="))
        {
            // NOTE: This parameter is handled by cfg_set_config_filename()
            //       and therefore ignored here.
            //
            //       This needs to be here so that this commandline option
            //       does not become "unrecognized"
            free(kv);
        }
        /*
         * -database=<filepath string>
         */
        else if (isopt("-database="))
        {
/* ugly and no checks... I need some utils... */
            strcpy(tmpcfg->database.filename, argv[argvidx] + sizeof("-database=") - 1);
            free(kv);
        }
        /*
         * -createdb
         */
        else if (isopt("-createdb"))
        {
            //
            // Special command that makes the program to (re)create
            // the database and exit.
            //
            tmpcfg->cmd.createdatabase = true;
            free(kv);
        }
        /*
         * -writeconfig
         */
        else if (isopt("-writeconfig"))
        {
            //
            // Special command that makes the program to (re)create
            // the configuration file and exit.
            //
            tmpcfg->cmd.createconfigfile = true;
            free(kv);
        }
        /*
         * -writeconfig
         */
        else if (isopt("-testdbwrite"))
        {
            //
            // Special command that tests SQLite3 write performance
            //
            if (keyval_nvalues(kv))
                tmpcfg->cmd.testdbwriteperf = atoi(kv[1]);
            else
                tmpcfg->cmd.testdbwriteperf = 6;
            free(kv);
        }
        else
        {
            logmsg(
                  LOG_ERR,
                  "%s: invalid option -- '%s'\n",
                  DAEMON_NAME,
                  argv[argvidx]
                  );
            n_errors++;
            free(kv);
        }
    }

    // If n_errors, complain and return
    if (n_errors)
    {
        cfg_prog_usage();
        logmsg(
              LOG_ERR,
              "%s: %d error%s in commandline options!\n",
              DAEMON_NAME,
              n_errors,
              n_errors > 1 ? "s" : ""
              );
        return (errno = EINVAL, EXIT_FAILURE);
    }

    return (errno = 0, EXIT_SUCCESS);
}
#undef isopt


/*
 * Basic configuration parameter checks
 *
 *	This should be called by the main() AFTER all the configuration
 *	values have been read, BEFORE proceeding with execution.
 */
int cfg_check(config_t *config)
{
    char *tmp;
    char *executing_username;
    // Determine if the given config data will execute as
    // current user or as DAEMON_RUN_AS_USER
    if (config->execute.as_daemon)
        executing_username = DAEMON_RUN_AS_USER;
    else
        executing_username = user_get_ename();   // effective - should be 'root'

    /*
     * Check access to config -file
     *
     *      At this point we already know that cfg_read_file() has ensured
     *      that the config file exists OR if not, it is
     *      CFG_DEFAULT_FILECONFIG.
     *
     *      Read permission is checked here even though we cannot be sure
     *      that the file will remain accessible until SIGHUP arrives.
     *      This will notify the user of existing issue, which is useful.
     */
    if (file_exist(config->filename))
    {
        if(!file_useraccess(config->filename, executing_username, R_OK))
        {
            logmsg(
                  LOG_ERR,
                  "user '%s' does not have read access to configuration file \"%s\".",
                  executing_username,
                  config->filename
                  );
            logmsg(LOG_ERR, "SIGHUP (re-read configuration) will fail.");
            return (errno = EACCES, EXIT_FAILURE);
        }
        // File exists and access is OK
        // Use duplicated buffer because both arguments
        // cannot be the same string.
        tmp = strdup(config->filename);
        if (!realpath(tmp, config->filename))
        {
            // NULL returned, error occured
            int savederrno = errno;
            logmsg(
                  LOG_ERR,
                  "Could not resolve real path to \"%s\".",
                  config->filename
                  );
            free(tmp);
            return (errno = savederrno, EXIT_FAILURE);
        }
        free(tmp);
    }
    else
    {
        // does not exists - can only be the CFG_DEFAULT_FILECONFIG
        if (!eqlstr(config->filename, CFG_DEFAULT_FILECONFIG))
        {
            logmsg(
                  LOG_ERR,
                  "non-default configuration file does not exist! (\"%s\")",
                  config->filename
                  );
            return (errno = ENOENT, EXIT_FAILURE);
        }
        // else, carry on, everything is OK
    }

    /*
     * Check access to database
     *
     *      Database file requires R/W access.
     */
    if (!file_exist(config->database.filename))
    {
        logmsg(
              LOG_ERR,
              "database file \"%s\" does not exist.",
              config->database.filename
              );
        return (errno = ENOENT, EXIT_FAILURE);
    }
    if(!file_useraccess(config->database.filename, executing_username, R_OK | W_OK))
    {
        logmsg(
              LOG_ERR,
              "user '%s' does not have read and write access to database file \"%s\".",
              executing_username,
              config->database.filename
              );
        logmsg(LOG_ERR, "No data can be saved.");
        return (errno = EACCES, EXIT_FAILURE);
    }
    // Update database.filename with real path.
    // Use duplicated buffer because both arguments
    // cannot be the same string.
    tmp = strdup(config->database.filename);
    if (!realpath(tmp, config->database.filename))
    {
        // NULL returned, error occured
        int savederrno = errno;
        logmsg(
              LOG_ERR,
              "Could not resolve real path to \"%s\".",
              config->database.filename
              );
        free(tmp);
        return (errno = savederrno, EXIT_FAILURE);
    }
    free(tmp);

    /*
     * Check existance and access of scrubber script
     *
     */
    if (!file_exist(config->modem.scrubber.filename))
    {
        logmsg(
              LOG_ERR,
              "scrubber file \"%s\" does not exist.",
              config->modem.scrubber.filename
              );
        return (errno = ENOENT, EXIT_FAILURE);
    }
    if(!file_useraccess(config->modem.scrubber.filename, executing_username, X_OK))
    {
        logmsg(
              LOG_ERR,
              "user '%s' does not have execute rights to scrubber file \"%s\".",
              executing_username,
              config->modem.scrubber.filename
              );
        logmsg(LOG_ERR, "No data can be retrieved from modem.");
        return (errno = EACCES, EXIT_FAILURE);
    }
    // Update scrubber.filename with real path.
    // Use duplicated buffer because both arguments
    // cannot be the same string.
    tmp = strdup(config->modem.scrubber.filename);
    if (!realpath(tmp, config->modem.scrubber.filename))
    {
        // NULL returned, error occured
        int savederrno = errno;
        logmsg(
              LOG_ERR,
              "Could not resolve real path to \"%s\".",
              config->modem.scrubber.filename
              );
        free(tmp);
        return (errno = savederrno, EXIT_FAILURE);
    }
    free(tmp);

    //
    // CHECK SCHEDULED EVENTS
    //
    char **eventarray = str2arr(cfg.event.liststring);  // util.c
    if (arrlen(eventarray))
    {
        int n;
        if ((n = event_test_parse(eventarray)))   // event.c
        {
            if (n < 0)
                logdev("eventarray was NULL. Ignored");
            else // n > 0 == number of discarded events
            {
                int savederrno = errno;
                logmsg(
                      LOG_ERR,
                      "%d events failed to parse. Source string: \"%s\"",
                      n,
                      cfg.event.liststring
                      );
                free(eventarray);
                return (errno = savederrno, EXIT_FAILURE);
            }
        }
        // else n == 0 which means all is OK
    }
    free(eventarray);

    // NOTE: Parsed schedule will be committed in
    //       daemon.c:daemon_initialize()

    return (errno = 0, EXIT_SUCCESS);
}


/*
 * Write existing configuration into a configuration file
 */
int cfg_writefile(const char *cfgfilename)
{
    FILE *cfgfile;

    /*
     * Open for write, create if missing.
     */
    if ((cfgfile = fopen(cfgfilename, "w+")) == NULL)
    {
        logerr("cfg_writefile(): fopen(\"%s\", \"w+\") failed!", cfgfilename);
        return(EXIT_FAILURE);
    }

    /*
     * Write configuration file content
     */
    fprintf(cfgfile, "#  %s\n", cfgfilename);
    fprintf(cfgfile, "#\n");
    fprintf(cfgfile, "#  Configuration file for icmond - Internet Connection MONitor Daemon\n");
    fprintf(cfgfile, "#\n");
    fprintf(cfgfile, "\n");

    fprintf(cfgfile, "# [daemon] Run as daemon?\n");
    fprintf(cfgfile, "# VALUES  : TRUE or FALSE\n");
    fprintf(cfgfile, "# DEFAULT : %s\n", (CFG_DEFAULT_EXE_ASDAEMON ? "TRUE" : "FALSE"));
    fprintf(cfgfile, "daemon = %s\n", (cfg.execute.as_daemon ? "TRUE" : "FALSE"));
    fprintf(cfgfile, "\n");

    fprintf(cfgfile, "# [ramdisk] Will a tmpfs be mounted for intermediate data storage\n");
    fprintf(cfgfile, "# VALUES  : TRUE, FALSE or AUTO\n");
    fprintf(cfgfile, "# DEFAULT : %s\n", (CFG_DEFAULT_EXE_TMPFS == 2 ? "AUTO" : (CFG_DEFAULT_EXE_TMPFS ? "TRUE" : "FALSE")));
    fprintf(cfgfile, "daemon = %s\n", (cfg.execute.tmpfs  == 2 ? "AUTO" : (CFG_DEFAULT_EXE_TMPFS ? "TRUE" : "FALSE")));
    fprintf(cfgfile, "\n");

    fprintf(cfgfile, "# [interval] modem data logging interval\n");
    fprintf(cfgfile, "# VALUES  : %d - %d\n", CFG_MIN_EXE_INTERVAL, CFG_MAX_EXE_INTERVAL);
    fprintf(cfgfile, "# DEFAULT : %d\n", CFG_DEFAULT_EXE_INTERVAL);
    fprintf(cfgfile, "interval = %d\n", cfg.execute.interval);
    fprintf(cfgfile, "\n");

    fprintf(cfgfile, "# [loglevel] Defines the priority for a message to get logged\n");
    fprintf(cfgfile, "# NOTE: Does NOT affect monitoring data, only the messages from monitoring software itself.\n");
    fprintf(cfgfile, "# VALUES  : LOG_ERR , LOG_INFO or LOG_DEBUG\n");
    fprintf(cfgfile, "# DEFAULT : %s\n", cfg_loglevel_val2str(CFG_DEFAULT_EXE_LOGLEVEL));
    fprintf(cfgfile, "loglevel = %s\n", cfg_loglevel_val2str(cfg.execute.loglevel));
    fprintf(cfgfile, "\n");

    fprintf(cfgfile, "# [database] SQLite3 database file where the logging information is stored\n");
    fprintf(cfgfile, "# NOTE: spaces are not supported (and \" -quotations will not help - sorry!)\n");
    fprintf(cfgfile, "# VALUES  : (filepath string)\n");
    fprintf(cfgfile, "# DEFAULT : %s\n", CFG_DEFAULT_FILEDATABASE);
    fprintf(cfgfile, "database = %s\n", cfg.database.filename);
    fprintf(cfgfile, "\n");

    fprintf(cfgfile, "# [inet pinghosts] host or address of the ping target\n");
    fprintf(cfgfile, "# VALUES  : single host \"www.host.com\" or list \"www.host1.com,www.host2.com,www.host3.com\"\n");
    fprintf(cfgfile, "# DEFAULT : %s\n", CFG_DEFAULT_INET_PINGHOSTS);
    fprintf(cfgfile, "inet pinghosts = %s\n", cfg.inet.pinghosts);
    fprintf(cfgfile, "\n");

    fprintf(cfgfile, "# [inet pingtimeout] ping timeout in milliseconds\n");
    fprintf(cfgfile, "# VALUES  : %d - %d\n", CFG_MIN_PING_TIMEOUT, CFG_MAX_PING_TIMEOUT);
    fprintf(cfgfile, "# DEFAULT : %d\n", CFG_DEFAULT_INET_PINGTIMEOUT);
    fprintf(cfgfile, "inet pingtimeout = %d\n", cfg.inet.pingtimeout);
    fprintf(cfgfile, "\n");

    fprintf(cfgfile, "# [modem powercontrol] do scheduled events control mains power\n");
    fprintf(cfgfile, "# NOT IMPLEMENTED, USE FALSE\n");
    fprintf(cfgfile, "# VALUES  : TRUE or FALSE\n");
    fprintf(cfgfile, "# DEFAULT : %s\n", (CFG_DEFAULT_MODEM_POWERCONTROL ? "TRUE" : "FALSE"));
    fprintf(cfgfile, "modem powercontrol = %s\n", (cfg.modem.powercontrol ? "TRUE" : "FALSE"));
    fprintf(cfgfile, "\n");

    fprintf(cfgfile, "# [modem powerupdelay] time it takes for the modem to boot up\n");
    fprintf(cfgfile, "# VALUES  : %d - %d seconds\n", CFG_MIN_MODEM_POWERUPDELAY, CFG_MAX_MODEM_POWERUPDELAY);
    fprintf(cfgfile, "# DEFAULT : %d\n", CFG_DEFAULT_MODEM_POWERUPDELAY);
    fprintf(cfgfile, "modem powerupdelay = %d\n", cfg.modem.powerupdelay);
    fprintf(cfgfile, "\n");

    fprintf(cfgfile, "# [modem ip] do scheduled events control mains power\n");
    fprintf(cfgfile, "# VALUES  : TRUE or FALSE\n");
    fprintf(cfgfile, "# DEFAULT : %s\n", CFG_DEFAULT_MODEM_IP);
    fprintf(cfgfile, "modem ip = %s\n", cfg.modem.ip);
    fprintf(cfgfile, "\n");

    fprintf(cfgfile, "# [modem pingtimeout] ping timeout in milliseconds\n");
    fprintf(cfgfile, "# VALUES  : %d - %d\n", CFG_MIN_PING_TIMEOUT, CFG_MAX_PING_TIMEOUT);
    fprintf(cfgfile, "# DEFAULT : %d\n", CFG_DEFAULT_MODEM_PINGTIMEOUT);
    fprintf(cfgfile, "modem pingtimeout = %d\n", cfg.modem.pingtimeout);
    fprintf(cfgfile, "\n");

    fprintf(cfgfile, "# [modem scrubber] script that retrieves data from modem\n");
    fprintf(cfgfile, "# VALUES  : (full path and filename)\n");
    fprintf(cfgfile, "# DEFAULT : %s\n", CFG_DEFAULT_MODEM_SCRUBBER);
    fprintf(cfgfile, "modem scrubber = %s\n", cfg.modem.scrubber.filename);
    fprintf(cfgfile, "\n");

    fprintf(cfgfile, "# [modem scrubbertimeout] scrubber timeout in milliseconds\n");
    fprintf(cfgfile, "# VALUES  : %d - %d (milliseconds)\n", CFG_MIN_MODEM_SCRUBBERTIMEOUT, CFG_MAX_MODEM_SCRUBBERTIMEOUT);
    fprintf(cfgfile, "# DEFAULT : %d\n", CFG_DEFAULT_MODEM_SCRUBBERTIMEOUT);
    fprintf(cfgfile, "modem scrubbertimeout = %d\n", cfg.modem.scrubber.timeout);
    fprintf(cfgfile, "\n");

    fprintf(cfgfile, "# [schedule dst] is daylight savings observed\n");
    fprintf(cfgfile, "# VALUES  : TRUE or FALSE\n");
    fprintf(cfgfile, "# DEFAULT : %s\n", (CFG_DEFAULT_EVENT_APPLYDST == 1 ? "TRUE" : "FALSE"));
    fprintf(cfgfile, "schedule dst = %s\n", (cfg.event.apply_dst == 1 ? "TRUE" : "FALSE"));
    fprintf(cfgfile, "\n");

    fprintf(cfgfile, "# [schedule] events to schedule\n");
    fprintf(cfgfile, "# VALUES  : time in HH:MM followed by SUSPEND or RESUME (example \"04:30 SUSPEND\")\n");
    fprintf(cfgfile, "# DEFAULT : %s\n", CFG_DEFAULT_EVENT_STRING);
    fprintf(cfgfile, "schedule = %s\n", cfg.event.liststring);
    fprintf(cfgfile, "\n");

    fprintf(cfgfile, "#EOF\n");

    fclose(cfgfile);
    return(EXIT_SUCCESS);
}

/*
 * Debug function - display the contents of config_t
 */
void cfg_print(config_t *config, int logpriority, const char *fmtstr, ...)
{
    va_list	 args;
    va_start(args, fmtstr);

    vlogmsg(logpriority, fmtstr, args);

    logmsg(logpriority, "config_t structure (%d Bytes):", sizeof(config_t));
    logmsg(logpriority, "  .filename                = \"%s\"", config->filename);
    logmsg(logpriority, "  .execute.as_daemon       = %s", config->execute.as_daemon ? "TRUE" : "FALSE");
    logmsg(logpriority, "  .execute.tmpfs           = %s", cfg.execute.tmpfs  == 2 ? "AUTO" : (CFG_DEFAULT_EXE_TMPFS ? "TRUE" : "FALSE"));
    logmsg(logpriority, "  .execute.interval        = %d (seconds)", config->execute.interval);
    logmsg(logpriority, "  .execute.loglevel        = (%d) \"%s\"", config->execute.loglevel, cfg_loglevel_val2str(config->execute.loglevel));
    logmsg(logpriority, "  .database.filename       = \"%s\"", config->database.filename);
    logmsg(logpriority, "  .inet.pinghosts          = (0x%08x) {%s}", config->inet.pinghosts, config->inet.pinghosts);
    logmsg(logpriority, "  .inet.pingtimeout        = %d (milliseconds)", config->inet.pingtimeout);
    logmsg(logpriority, "  .modem.powercontrol      = %s", config->modem.powercontrol ? "TRUE" : "FALSE");
    logmsg(logpriority, "  .modem.powerupdelay      = %d (seconds)", config->modem.powerupdelay);
    logmsg(logpriority, "  .modem.ip                = \"%s\"", config->modem.ip);
    logmsg(logpriority, "  .modem.pingtimeout       = %d (milliseconds)", config->modem.pingtimeout);
    logmsg(logpriority, "  .modem.scrubber.filename = \"%s\"", config->modem.scrubber.filename);
    logmsg(logpriority, "  .modem.scrubber.timeout  = %d (milliseconds)", config->modem.scrubber.timeout);
    logmsg(logpriority, "  .cmd.createdatabase      = %s", config->cmd.createdatabase ? "TRUE" : "FALSE");
    logmsg(logpriority, "  .cmd.createconfigfile    = %s", config->cmd.createconfigfile ? "TRUE" : "FALSE");
    logmsg(logpriority, "  .event.apply_dst         = %d (%s)", config->event.apply_dst,
           config->event.apply_dst == 0 ? "DST not applied" : (config->event.apply_dst > 0 ? "DST applied" : "auto"));
    logmsg(logpriority, "  .event.liststring        = (0x%08x) {%s}", config->event.liststring, config->event.liststring);

    va_end(args);
}

/* EOF config.c */
