/*
 * config.h - 2016 Jani Tammi <janitammi@gmail.com>
 *
 *      Program configuration for icmond
 *
 *	NOTE ON TERMINOLOGY
 *
 *	This daemon is basically a specialized datalogger.
 *	For this reason, there are TWO types of logging.
 *	Firstly, there is runtime messages/notifications
 *	that are recorded into syslog. (runtime messages)
 *	Secondly, the DATALOGGING which stores all the DOCSIS
 *	related data into the database file. (datalogging)
 */
#include <syslog.h>         // LOG_ERR, LOG_INFO, LOG_DEBUG
#include <limits.h>         // PATH_MAX
#include <netinet/in.h>     // INET_ADDRSTRLEN (although with IPv4, always 16...)

#ifndef __CONFIG_H__
#define __CONFIG_H__

#define DAEMON_HEADER   "Internet connection monitor (c) 2016 Jani Tammi"

#define FALSE                               0
#define TRUE                                1
#define AUTO                                2

/*
 * TMPFS SIZE
 *      Size will be 4 MB, based on 08.10.2016 calculations on daily data
 *      accumulation using minimal logging interval.
 *      (24*60*60=86'400 second/day) / 5 sec interval = 17'280 rows / day.
 *      Data record size in SQLite3 ~ 209Bytes.
 *      209 Bytes * 17'280 records = 3'611'520 Bytes = 3,45 MB => 4 MB
 */
#define DAEMON_NAME                         "icmond"
#define DAEMON_PIDFILE                      "/var/lock/"DAEMON_NAME".lck"
#define DAEMON_TMPFS_MOUNTPOINT             "/tmp/"DAEMON_NAME".tmpfs"              // used by tmpfsdb.c
#define DAEMON_TMPFS_SIZEMB                 4                                       // MB
#define DAEMON_TMPFS_DATABASEFILE           DAEMON_TMPFS_MOUNTPOINT"/"DAEMON_NAME".sqlite3"
#define DAEMON_RUN_AS_USER                  "daemon"
#define DAEMON_DATALOGGER_TIMEOUT           4800    // (milliseconds) grace time before datalogger process is terminated
#define DAEMON_IMPORTTMPFS_TIMEOUT          60      // (seconds) 1 minute before data moval from tmpfs to actual datafile is considered failed
#define DAEMON_IMPORTTMPFS_INTERVAL         600     // (seconds) 10 minutes

// These define compiled-in default configuration
#define CFG_DEFAULT_FILECONFIG              "/etc/"DAEMON_NAME".conf"               // USE ABSOLUTE PATH!
#define CFG_DEFAULT_FILEDATABASE            "/srv/"DAEMON_NAME".sqlite3"            // SQLite3 database file (and path)
#define CFG_DEFAULT_EXE_LOGLEVEL            LOG_INFO                                // see logwrite.h for details
#define CFG_DEFAULT_EXE_INTERVAL            10                                      // 10 sec data logging interval
#define CFG_DEFAULT_EXE_ASDAEMON            TRUE                                    // true or false
#define CFG_DEFAULT_EXE_TMPFS               AUTO
#define CFG_DEFAULT_INET_PINGHOSTS          "www.google.com"                        // Host(s) to ping to evaluate internet connection
#define CFG_DEFAULT_INET_PINGTIMEOUT        1000                                    // ms before ICMP Echo Request is considered failed
#define CFG_DEFAULT_MODEM_POWERCONTROL      FALSE                                   // placeholder - true/false for now
#define CFG_DEFAULT_MODEM_POWERUPDELAY      45                                      // seconds from power to be able to respond to HTTP request
#define CFG_DEFAULT_MODEM_PINGTIMEOUT       200                                     // ms
#define CFG_DEFAULT_MODEM_SCRUBBERTIMEOUT   4000                                    // before scrubber is considered tardy
#define CFG_DEFAULT_MODEM_SCRUBBER          "/usr/local/bin/"DAEMON_NAME".scrubber" // external scrubber script filepath
#define CFG_DEFAULT_MODEM_IP                "192.168.1.1"                           // Manufacturer's default CHANGE TO 192.168.0.1 !!!!
#define CFG_DEFAULT_EVENT_APPLYDST          0                                       // 0 == no DST, >0 == yes DST, -1 == "auto" (do NOT use)
#define CFG_DEFAULT_EVENT_STRING            ""                                      // See event.c for details

// Interval period range for logging (in seconds)
#define CFG_MIN_EXE_INTERVAL                5                                       // 5 seconds
#define CFG_MAX_EXE_INTERVAL                3600                                    // 1 hour
// Valid ping timeout range (in milliseconds)
#define CFG_MIN_PING_TIMEOUT                100                                     // 100 ms (0.1 sec)
#define CFG_MAX_PING_TIMEOUT                3000                                    // 3'000 ms (3 sec)
// Powerup delay range (in seconds)
#define CFG_MIN_MODEM_POWERUPDELAY          0
#define CFG_MAX_MODEM_POWERUPDELAY          300
// Scrubber timeout (in milliseconds)
#define CFG_MIN_MODEM_SCRUBBERTIMEOUT       200
#define CFG_MAX_MODEM_SCRUBBERTIMEOUT       5000
// Maximum allowed SQLite3 INSERT times SET LOW TO TEST!! RESET AFTER TESTING
#define CFG_MAX_INSERT_DELAY_MEAN           200.0L                                  // 200.0L ms mean
#define CFG_MAX_INSERT_DELAY_MAX            800.0L                                  // 800.0L ms 

// Maximum file and path length, for both database file and configuration file
// Value from <limits.h> PATH_MAX
// max filename length (not including null termin.)
#define CFG_MAX_FILENAME_LEN                PATH_MAX
//#define CFG_MAX_FILENAME_LEN                64                                      // for debugging...
#define CFG_MAX_CONFIGFILE_ROW_WIDTH        1024                                    // Read buffer size


/*
 * SPECIAL NOTES TO CONSIDER
 *
 *      CFG_MAX_PING_TIMEOUT obviously cannot exceed interval timer value.
 *      There needs to be some kind of a check and modification to this during
 *      start-up.
 *
 *      CFG_MAX_FILENAME_LEN seems redundant? See <limits.h> for
 *      #define PATH_MAX        4096    // # chars in a path name including nul
 *      or 
 *      MAX_FILE_NAME_whatever_it_is_called ....because filename max and path max are not the same thing
 *      RE-CHECK: HOW do I used my own max filename length macro anyway??
 *
 *      http://insanecoding.blogspot.fi/2007/11/pathmax-simply-isnt.html
 */
/*
 * It would be nice, if C preprocessor could enforce macrostring lengths
 * so that related errors could be caught in compile time, rather than runtime.
 * However, no support is required for sizeof() in specifications, and GCC doesn't have it.
 *
 * This is now implemented as assert()'s in config.c:cfg_init()
 *
 * (note that the space for null termination is allocated in the config_t)
 */
/* This here is for academic purposes only - below code DOES NOT WORK!
#if (sizeof(DEFAULT_CFG_FILE) > CFG_MAX_FILENAME_LEN)
#error "DEFAULT_CFG_FILE exceeds maximum allowed filename length!"
#endif

#if (sizeof(DEFAULT_DATABASEFILE) > CFG_MAX_FILENAME_LEN)
#error "DEFAULT_DATABASEFILE exceeds maximum allowed filename length!"
#endif
*/


/*
 * public configuration values structure
 */
typedef struct {
    char            filename[CFG_MAX_FILENAME_LEN + 1]; // cfg filename (+1 for NULL terminate)
    struct {
        unsigned int as_daemon : 1;                     // 0 = std execution, 1 = daemon
        unsigned int tmpfs     : 2;                     // false/true and 2 == auto
        int         interval;                           // probing interval in seconds
        int         loglevel;                           // priority as used by syslog
    } execute;
    struct {
        char        filename[CFG_MAX_FILENAME_LEN + 1]; // SQLite3 logging datafile
        char *      tmpfsfilename;                      // If not NULL, writes go here!
    } database;
    struct {
        int         pingtimeout;                        // ms
        char *      pinghosts;                          // List of hostnames (no default)
    } inet;
    struct {
        int         powercontrol;                       // true|falase (unimplemented)
        int         powerupdelay;                       // seconds
        char        ip[INET_ADDRSTRLEN + 1];            // modem IP (as string)
        int         pingtimeout;                        // ms, maximum allowed before killed
        struct {
            char    filename[CFG_MAX_FILENAME_LEN + 1];
            int     timeout;                            // ms
        } scrubber;
    } modem;
    struct {
        int         createdatabase;
        int         createconfigfile;
        int         testdbwriteperf;
    } cmd;
    struct {
        int         apply_dst;                          // 0 == no DST, >0 = yes, <0 = auto (do NOT use "auto")
        char *      liststring;
    } event;
} config_t;


/*
 * Configuration data for the whole program
 * Implementation in config.c
 */
extern config_t cfg;
/*
 * Commandline argument vector
 * Saved here (by cfg_store_argv()) so that the daemon can
 * honor commandline settings when SIGHUP forces it to re-read
 * configuration data.
 */
extern char **cmdline;

/*
 * Function prototypes
 */
void        cfg_prog_header();
void        cfg_prog_usage();
void        cfg_init(config_t *);
int         cfg_preread_commandline(config_t *, char **);
int         cfg_read_file(config_t *);
int         cfg_read_argv(config_t *, char **);
int         cfg_check(config_t *);
int         cfg_writefile(const char *);
void        cfg_print(config_t *config, int logpriority, const char *fmtstr, ...);
config_t *  cfg_dup(config_t *);
void        cfg_commit(config_t *);
void        cfg_free(config_t *);
char *      cfg_loglevel_val2str(int);
int         cfg_loglevel_str2val(const char *);
void        cfg_save_argv(char **);


#endif /* __CONFIG_H__ */

/* EOF config.h */
