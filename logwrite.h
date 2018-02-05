/*
 * logwrite.h - 2012, 2016 (c) Jani Tammi <janitammi@gmail.com>
 *
 *      Convenience functions to log runtime messages and errors.
 *      Specifically created for icmond logging daemon, but can be
 *      easily modified to other usages.
 *
 * REQUIREMENTS
 *
 *      Somewhere else in the program code, syslog has to be opened.
 *      openlog("program name", LOG_PID, LOG_DAEMON);
 *
 *      Following configuration items must be made available via
 *      config_t *cfg;
 *          .logging_level
 *
 *      This facility re-uses the following syslog logging priorities:
 *
 *      LOG_ERR      error condition.       (defined as 3)
 *      LOG_INFO     informational message  (defined as 6)
 *      LOG_DEBUG    debug-level message    (defined as 7)
 *
 *      This way, if the message is propagated to the actual syslog,
 *      it will already use syslog's own priority value. If user wishes,
 *      any other priority from syslog.h can be used. This facility can
 *      deal with them without issues, but rest of the icmond code might
 *      not be able deal with them (config.c/h and defined labels).
 *
 *  !!  NOTE that actual integer values of these defines MIGHT change.
 *      Your code should never use numbers, always macros.
 *
 *      What however is assumed, is that the ORDER of priorities will
 *      always stay the same - smaller the number, higher the priority.
 *
 * Logging practises and priorities
 *
 *      LOG_ERR     Program encounter unrecoverable error condition.
 *                  Information is logged with this priority before
 *                  execution is terminated.
 *      LOG_INFO    Normal, but significant, condition. For example,
 *                  the program should log the beginning of a network
 *                  outage under this priority and another message when
 *                  network operations have been restored.
 *      LOG_DEBUG   Detailed information about the execution. Mostly
 *                  useful for the deamon which has no standard output
 *                  stream to report to.
 *
 * Other syslog.h priorities
 *
 *      Any and all of the other syslog priority levels could also be used.
 *      Hoever, it has been decided that within this implementation, this
 *      program shall limit it's priorities to these three.
 *
 *      It should be noted, that while this module (writelog.c/h) can deal
 *      with any priority, rest of the program MAY not be able to. Please
 *      see at least config.c/h for defined labels for used priorities.
 */
#include <stdarg.h>     // va_list
#include <sys/types.h>  // pid_t
#include <syslog.h>     // LOG_ERR, LOG_INFO, LOG_DEBUG

/*
 * Initialize this facility
 *
 *      The function merely opens the syslog and stores the
 *      logging priority level. All messages of equal or smaller
 *      numeric priority value are logged, rest are discarded.
 *      (In syslog macros, the smaller value, the higher priority)
 */
void logwrite_init(const char *name, int priority_filter);

/*
 * Set priority filter
 *
 *      This affects logmsg() function only. logmsg() will deliver only those
 *      messages that have equal or lower priority value (see above for the
 *      priority values).
 */
void logwrite_set_logmsg_filter(int priority_filter);

/*
 * Register daemon PID
 *
 *      Logging functions will send the messages to syslog only if the getppid()
 *      returned PID is 1 (init => message from daemon) or __daemon_pid variable
 *      (=> message from datalogger process, child of daemon process).
 *      Use this function to register daemon PID so that these logging functions
 *      know how to deliver datalogger messages.
 *
 * USAGE:  (within daemon thread)
 *
 *      logwrite_register_daemon_pid(getpid());
 */
void logwrite_register_daemon_pid(pid_t daemonpid);

/*
 * Log messages to syslog or terminal/console
 *
 * void logmsg(int, const char *, ...)
 *
 *      If code is executing as daemon (parent PID = 1), all messages are
 *      sent to syslog. Otherwise messages are written into stderr. Error
 *      stream is chosen since the program as a whole may use stdout for
 *      transmitting work results (which may be piped to another program).
 *
 *      All messages are filtered by comparing cfg->log_level (icmond specific)
 *      and arg1 msglvl. If arg1 priority is equal or greater than specified
 *      in cfg->log_level, it will be delivered.
 *      (NOTE: Smaller defined number, the higher the priority.)
 *
 *      arg2 fmtstr is the message string and it may contain all the normal
 *      printf tokens. Parsing is done with with vfprintf().
 *
 * USAGE:
 *
 *      logmsg("Program executed for %d seconds", value);
 *
 * OUTPUT:
 *
 *      Program executed for 42 seconds
 */
void logmsg(int msglvl, const char *fmtstr, ...);
void vlogmsg(int msglvl, const char *fmtstr, va_list ap);

/*
 * Log errors to syslog or terminal/console
 *
 * void logerr(fmtstr, ...)
 *
 *      Otherwise similar to logmsg(), but does not take logging priority.
 *
 *      This function is intended to log terminal error conditions.
 *      Source file name, line number and function name are automatically
 *      added. Argument can be normal printf format string and it's arguments.
 *
 * USAGE:
 *
 *      logerr("Failed with value %d", value);
 *      logerr("unable to fork daemon, code=%d (%s)", errno, strerror(errno));
 *
 * OUTPUT:
 *
 *      worker.c:395:daemon_main() : Failed with value 42
 *      main.c:100::daemonize() : unable to fork daemon, code=12 (not enough memory)
 */
#define STRINGIFY(x) #x
#define TOSTRING(x)  STRINGIFY(x)

#define logerr(fmtstr, ...) \
        __logerr( \
                __FILE__ ":" TOSTRING(__LINE__) ":%s() : ", \
                __PRETTY_FUNCTION__, \
                (fmtstr), \
                ##__VA_ARGS__ \
                )
void __logerr(const char *pos, const char *fnc, const char *fmtstr, ...);

/*
 * Development tool - Log messages with millisecond time stamps
 *
 *  void logdev(fmtstr, ...)
 *
 *      Copied from __logerr() and modified to prefix the message with time as well.
 *      NOTE! Only available if __DEBUG is defined!
 *
 * USAGE:
 *
 *      logdev("Entering pselect(), scrubber is %s running.", scrubber.pid ? "still" : "not");
 *
 * OUTPUT:
 *
 *      [    0.000] datalogger.c:446:datalogger() : Entering pselect(), scrubber is not running.
 */
#ifdef _DEBUG
#define logdev(fmtstr, ...) \
        __logdev( \
                __FILE__ ":" TOSTRING(__LINE__) ":%s() : ", \
                __PRETTY_FUNCTION__, \
                (fmtstr), \
                ##__VA_ARGS__ \
                )
void __logdev(const char *pos, const char *fnc, const char *fmtstr, ...);
#else /* for type checking */
#define logdev(fmtstr, ...) \
        __noop("", "", (fmtstr), ##__VA_ARGS__)
#ifndef __LOGWRITE_H__
#define __LOGWRITE_H__
static inline void __noop(const char *pos, const char *fnc, const char *fmtstr, ...) { }
#endif /* __LOGWRITE_H__ */
#endif /* _DEBUG */


/* EOF */
