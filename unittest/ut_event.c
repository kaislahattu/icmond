#include <sys/select.h>     // pselect()
#include <stdlib.h>
#include <stdint.h>         // uint64_t
#include <stdbool.h>        // true / false
#include <unistd.h>         // read()
#include <stdarg.h>         // va_start ...
#include <malloc.h>
#include <string.h>         // memset()
#include <time.h>
#include <sys/timerfd.h>
#include <errno.h>
#include <stdbool.h>        /* true, false                          */
#include <termios.h>        /* termios, TCSANOW, ECHO, ICANON       */
#include <unistd.h>         /* STDIN_FILENO                         */

#include "../event.h"
#include "../eventheap.h" // TEMPORARY TESTING
#include "../config.h"
#include "../logwrite.h"
#include "../util.h"

/*
 * structures copied from daemon.c
 * /
typedef struct {
    int                     fd;
    struct itimerspec       tspec;
} fdtimer_t;
static struct daemon_t
{
    fdtimer_t               signal;
    fdtimer_t               schedule;
    fdtimer_t               interval;
    struct {
        int                 pid;
        int                 fd;
        struct itimerspec   tspec;
    } worker;
    struct {
        int                 running;
        time_t              suspended_by_command;
        time_t              suspended_by_schedule;
    } state;
} this = {
    .worker = {
        .pid                    = 0
    },
    .state = {
        .running                = true, // Set to FALSE and main loop will exit
        .suspended_by_command   = 0,
        .suspended_by_schedule  = 0
    }
};
*/
// Array to induce all errors
char *strarr1[] =
{
    "",                     // Empty strings are supposed to be collapsed away
    "a:59 RESUME",          // hour conversion error
    "-3:00 RESUME",         // hour range error
    "20€ off now!",         // separator error
    "1:on",                 // minute conversion error
    "2:99 PWRON",           // minute range error
    "12: pwroff",           // another minute conversion error
    "23:59 ",               // no event code error
    "04:00 off",            // unrecognized event code error
    "2:2:0",                // misc malformed event strings
    "+12:+10+",
    "16:10 PwrOn16:25 PwrOff",
    NULL
};

// event strings which need to be accepted
char *strarr2[] =
{
    "03:20 SUSPEND",            // Standard format
    "3:30              poweron",  // Multple space separating code from time
    "4:5RESUME",                // one-digit hour and minute, no space between time and code
    "@09:30ImportTMPFS",        // case insensitivity
    "!00:01 ImportTMPFStimeout",
    "!49:59 POWEROFF",
    NULL
};

char *strarr3[] =
{
    "04:10 suspend",
    "04:55 resume",
    NULL
};

void ttyanykey(const char *prompt)
{
    static struct termios oldt, newt;
    fprintf(stderr, "%s", prompt);
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    printf("\n");
}

#define \
    __report_timer(fd, str) \
    ({ \
        struct itimerspec spec; \
        if (timerfd_gettime((fd), &spec)) { \
            logerr("timerfd_gettime() failure!"); \
        } else { \
            logdev("%-12s % 03d:%02d:%02d.%03ld remaining", (str), \
                   GETHOURS(spec.it_value.tv_sec), \
                   GETMINUTES(spec.it_value.tv_sec), \
                   GETSECONDS(spec.it_value.tv_sec), \
                   spec.it_value.tv_nsec / 1000000); \
        } \
    })
#define ARRAY_SIZE(x) ( sizeof(x) / sizeof((x)[0]) )
int main()
{
    doTest(); // eventheap.c:doTest();
    time_t t = time(NULL);
    event_unittest_settime(t);

    int rc __attribute__ ((__unused__)); // generic returncode
    char *buffer = NULL;
    printf("=======================================================\n");
    printf("01. Testing schedule parsing (%d samples)...\n", ARRAY_SIZE(strarr1) - 1);
    printf("=======================================================\n");
    int n_errors = event_test_parse(strarr1);
    if (n_errors > 0)
    {
        printf("   Parsing strarr1 generated %d errors (%d expected)\n", n_errors, ARRAY_SIZE(strarr1) - 2);
        printf("%s", event_test_errors());
        printf("Printing parsed schedule\n");
        printf("%s\n", bsprint_testparsed_schedule(&buffer));
        if (n_errors != ARRAY_SIZE(strarr1) - 2)
            exit(EXIT_FAILURE);
    }
    else if (n_errors < 0)
    {
        printf("   event_test_parse() reports invalid argument!\n");
        exit(EXIT_FAILURE);
    }
    else
    {
        printf("   No errors parsing strarr1! Seriously wrong result!\n");
        exit(EXIT_FAILURE);
    }
    ttyanykey("Press any key to continue...");

    printf("=======================================================\n");
    printf("02. Printing parsed schedule (nothing should be printed)\n");
    printf("=======================================================\n");
    bsfree(&buffer);
    printf("%s\n", bsprint_testparsed_schedule(&buffer));
    ttyanykey("Press any key to continue...");

    printf("=======================================================\n");
    printf("03. Parsing proper schedule array..\n");
    printf("=======================================================\n");
    n_errors = event_test_parse(strarr2);
    if (n_errors > 0)
    {
        printf("   Parsing strarr2 generated %d errors (none were expected!)\n", n_errors);
        printf("%s\n", event_test_errors());
        exit(EXIT_FAILURE);
    }
    else if (n_errors < 0)
    {
        printf("   bsprint_testparsed_schedule() reports invalid argument!\n");
        exit(EXIT_FAILURE);
    }
    else
    {
        printf("   No errors parsing strarr2\n");
        printf("   event_test_errors(): \"%s\" (should be empty)\n", event_test_errors());
    }
    ttyanykey("Press any key to continue...");
    
    printf("=======================================================\n");
    printf("04. Printing parsed schedule\n");
    printf("=======================================================\n");
    bsfree(&buffer);
    printf("%s\n", bsprint_testparsed_schedule(&buffer));
    ttyanykey("Press any key to continue...");

    /* Point?
printf("testing insert\n");
    event_t *newevent = calloc(1, sizeof(event_t));
    newevent->action         = EVENT_ACTION_WATCHDOG;
    newevent->type           = EVENT_TYPE_INTERVAL;
    newevent->source         = EVENT_SOURCE_INTERNAL;
    newevent->localoffset    = 22 * SECONDS_PER_HOUR;
    if (event_test_insert(newevent))
    {
        printf("Event was rejected!\n");
        exit(EXIT_FAILURE);
    }
    printf("%s\n", bsprint_testparsed_schedule(&buffer));
*/

    printf("=======================================================\n");
    printf("05. Committing parsed schedule\n");
    printf("=======================================================\n");
    event_commit_test_schedule();
    bsfree(&buffer);
    printf("%s\n", bsprint_schedule(&buffer));
    ttyanykey("Press any key to continue...");

    printf("=======================================================\n");
    printf("07. Do it again, so we know that heap restoration works\n");
    printf("=======================================================\n");
    bsfree(&buffer);
    printf("%s\n", bsprint_schedule(&buffer));
    ttyanykey("Press any key to continue...");

    printf("=======================================================\n");
    printf("08. Inserting two events (source: internal)\n");
    printf("=======================================================\n");
    if (event_create(EVENT_ACTION_WATCHDOG, 3 * SECONDS_PER_HOUR) < 0)
    {
        printf("event_create() failed!");
        exit(EXIT_FAILURE);
    }
    if (event_create(EVENT_ACTION_POWEROFF, 10 * SECONDS_PER_HOUR) < 0)
    {
        printf("event_create() failed!");
        exit(EXIT_FAILURE);
    }
    printf("Done!\n");
    ttyanykey("Press any key to continue...");

    printf("=======================================================\n");
    printf("09. Clearing parsed events\n");
    printf("=======================================================\n");
    printf("BEFORE event_schedule_clear(EVENT_SOURCE_PARSED)\n");
    bsfree(&buffer);
    printf("%s\n", bsprint_schedule(&buffer));
    event_schedule_clear(EVENT_SOURCE_PARSED);
    printf("AFTER event_schedule_clear(EVENT_SOURCE_PARSED)\n");
    bsfree(&buffer);
    printf("%s\n", bsprint_schedule(&buffer));
    ttyanykey("Press any key to continue...");

    printf("=======================================================\n");
    printf("10. Parsing small configuration and committing it.\n");
    printf("=======================================================\n");
    n_errors = event_test_parse(strarr3);
    if (n_errors > 0)
    {
        printf("   Parsing strarr3 generated %d errors!\n", n_errors);
        exit(EXIT_FAILURE);
    }
    else if (n_errors < 0)
    {
        printf("   event_test_parse() reports invalid argument!\n");
        exit(EXIT_FAILURE);
    }
    event_commit_test_schedule();
    printf("NEW SCHEDULE:\n");
    bsfree(&buffer);
    printf("%s\n", bsprint_schedule(&buffer));
    ttyanykey("Press any key to continue...");

    printf("=======================================================\n");
    printf("08. Stepping 50 hours in 1h steps querying the schedule\n");
    printf("=======================================================\n");
    event_t *e;
    time_t now = time(NULL);
//    now += SECONDS_PER_HOUR - (now % SECONDS_PER_HOUR); // Next even hour
    time_t end = now + 50 * SECONDS_PER_HOUR;
    for (; now < end; now += SECONDS_PER_MINUTE)
    {
        // IMPORTANT - Manipulate time_t for event.c:__update_today()
        event_unittest_settime(now);
        while ((e = event_gettriggered(now)))
        {
            printf("\n");
            printf("=== System Time %02d:%02d:%02d  (%ld) =============\n", GETHOURS(now), GETMINUTES(now), GETSECONDS(now), now);
            printf("%s (triggered) %02d:%02d:%02d\n", event_getactionstr(e->action), GETHOURS(e->next_trigger), GETMINUTES(e->next_trigger), GETSECONDS(e->next_trigger));
            bsfree(&buffer);
            bsprint_event(&buffer, e);
            if (e->type == EVENT_TYPE_ONCE)
                printf("Event is of type ONCE and will not be rescheduled.\n");
            else
            {
                bsprintf(&buffer, "Event rescheduled to : ");
                bsprint_time(&buffer, event_reschedule(e));
                printf("%s\n", buffer);
            }
        }
    }
    printf("\n=== System Time %02d:%02d:%02d  (%ld) =============\n", GETHOURS(now), GETMINUTES(now), GETSECONDS(now), now);
    printf("  END OF TEST RUN\n");

    printf("=======================================================\n");
    printf("09. Display schedule after run\n");
    printf("=======================================================\n");
    bsfree(&buffer);
    printf("%s\n", bsprint_schedule(&buffer));

    return EXIT_SUCCESS;
#ifdef NOTDEFINED
    // Event creation, at worst of times, takes less than a second.
    // Even IF the schedule array creation would leave the next event
    // 1 second into the past, it is of no consequence.
    //
    // It will just make our timer fire as soon as we enter pselect() and
    // the event takes place. Maybe at most, a second late - which means
    // NOTHING in minute -granule scheduling.

    printf("DST  Daylight Savings Time          UTC+<n> +0/1 DST. An hour is added during \"summer time\".\n");
    printf("LST  Local Standard Time            UTC+<n> and does NOT change with summer/winter time.\n");
    printf("UTC  Coordinated Universal Time     Name for the time standard (not a timezone).\n");
    printf("GMT  Greenwich Mean Time            Old and not obsolete time standard.\n");
    printf("\n");
    printf("Timezones\n");
    printf("    Notation is generally \"standard\"+<n> (for example: \"UTC+0\" or \"UTC-10\").\n");
    printf("    Neither UTC times, GMT or any other timezone (\"TZ\") ever change their time.\n");
    printf("    DST is an act of locally changing into another timezone.\n");
    printf("    For example, Finnish LST is UTC+2, but when DST is applied, Finland uses UTC+3.\n");
    printf("    However, even during DST, Finnish LST (Local Standard Time) is STILL UTC+2.\n");
    printf("    LST has very little use in practise and is therefore rarely encountered. Term\n");
    printf("    \"local time\" commonly means LST DST, which is usually written; UTC+2 DST\n");
    printf("\n");

    // Generate test events
#define NUM_TEST_EVENTS    5        // 1 is key, rest are events
    char **kv = calloc(NUM_TEST_EVENTS + 1, sizeof(char *));
    now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char buffer[EVENT_ACTIONSTR_MAXLEN + 1];
    int index;
    kv[0] = strdup("SCHEDULE");
    for (index = 1; index < NUM_TEST_EVENTS; index++)
    {
        tm.tm_min += 1;
        mktime(&tm);
        sprintf(buffer, "%02d:%02d %s", tm.tm_hour, tm.tm_min,
                index % 2 ? "SUSPEND" : "RESUME");
        kv[index] = strdup(buffer);
    }

    // flip switches to simulate control
    cfg.modem.powercontrol = true;
    cfg.modem.powerupdelay = 30;    // 30 seconds to boot

    printf("[Imaginary config.c begins]\n\n");
    // This will be called from config.c
    if (event_parse_schedule(kv))
    {
        printf("event_create_schedule() returned error code!");
    }
    printf("[Imaginary config.c is complete]\n\n");

    // Display Schedule content
    event_print_schedule();

    printf("[Imaginary daemon_main.c begins]\n");

    // Below is copied from daemon_main.c
    this.state.running = true;  // Set this true to terminate main loop
    int     nfds = 0;           // # of largest file descriptor in the set
    int     rc;                 // pselect() return value
    fd_set  readfds;            // file descriptor set

    // Timeout for dev message spam
    struct timespec pselecttimeout;
    pselecttimeout.tv_sec  = 10; // 2 seconds
    pselecttimeout.tv_nsec = 0; // 500000000; // == 500ms

    // Create scheduler timer fd
    event_set_timerfd(&this.schedule.tspec);
    if ((this.schedule.fd = timerfd_create(CLOCK_REALTIME, 0)) == -1)
    {
        logerr("timerfd_create()");
        exit(EXIT_FAILURE);
    }
    // Start interval timer
    if (timerfd_settime(this.schedule.fd, TFD_TIMER_ABSTIME, &this.schedule.tspec, NULL) == -1)
    {
        logerr("timerfd_settime()");
        exit(EXIT_FAILURE);
    }

    do
    {
        nfds = 0;
        FD_ZERO(&readfds);
        FD_SET(this.schedule.fd, &readfds);
        nfds = (nfds > this.schedule.fd ? nfds : this.schedule.fd);       // max(nfds, fd)
        logdev("entering pselect() with ndfs %d + 1", nfds);
        errno = 0;
        rc = pselect(
                    nfds + 1,        // Calculated by setup above
                    &readfds,        // ditto
                    NULL,            // &writefds,  // If it would be needed
                    NULL,            // &exceptfds, // Again, if it would be needed
                    &pselecttimeout, // No timeout (not necessary, since we're monitorin timers)
                    NULL             // Do not enable any signal interruptions during pselect() call
                    );
        if (rc < 0 && errno == EINTR)
        {
            // This should not happen unless SIGKILL or SIGSTOP
            // and if it is either of them, we should have already terminated anyway
            logerr("pselect() was interrupted by unknown signal");
            exit(EXIT_FAILURE);
        }
        else if (rc < 0)
        {
            logerr("pselect() failure");
            exit(EXIT_FAILURE);
        }
        else if (rc == 0)
        {
            logdev("pselect() timeout");
        }
        else // rc > 0
        {
            // signal fd timeour
            printf("MAIN(): Timer triggered!\n");
            // Read timerfd
            timerfd_acknowledge(this.schedule.fd);
            event_execute();
            // Rearm schedule timer
            event_set_timerfd(&this.schedule.tspec);
            timerfd_start_abs(this.schedule.fd, &this.schedule.tspec);
        }        

    // Exit if SIGCHLD hander has set the flag
    } while (this.state.running);
#endif
    return EXIT_SUCCESS;
}

/* EOF ut_event.c */
