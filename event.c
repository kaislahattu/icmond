/*
 * event.c - 2016 Jani Tammi <janitammi@gmail.com>
 *
 *      Simple event scheduling solution for reoccuring and unrepeatable
 *      events. Solution holds two separate datastructures, the actual event
 *      schedule queue and one for user the provided event schedule string
 *      which is only parsed/tested, but not automatically put into the actual
 *      schedule queue (needs to be specifically committed).
 *
 * Events contain only a handful of attributes:
 * .next_trigger
 *      All events are sorted into ascending order based on this value.
 *      It is the Unix datetimestamp (seconds since epoch) when the event is
 *      supposed to happen next.
 * .localoffset (to be renamed into timeoffset)
 *      Defines the relative time of occurance. The exact interpreation is
 *      dependant on the .type attribute.
 * .type (daily, interval or once)
 *      Simply the scheduling type of the event. Relevant for parsing
 *      .next_trigger from .localoffset.
 * .action
 *      This value will instruct the event dispatcher function to call the
 *      appropriate handler function when the event triggers.
 * .source (internal or parsed)
 *      Simply for keeping track whether or this event is created from the
 *      user provided event schedule string or created by a function call.
 *      This is significant for SIGHUP handling, so that recreating events
 *      specified in the configuration file does not overrun others.
 *
 */
#include <stdio.h>
#include <stdlib.h>         // calloc()
#include <stdbool.h>        // true false
#include <string.h>         // strncasecmp()
#include <malloc.h>
#include <ctype.h>          // isspace()
#include <errno.h>
#include <time.h>
//#include <inttypes.h>       // PRId64, PRIu64, PRIx64
//#include <sys/timerfd.h>    // timerfd_create()

#include "event.h"
#include "eventheap.h"
#include "logwrite.h"
#include "database.h"
#include "daemon.h"         // daemon_suspend(), daemon_resume()
#include "power.h"          // power_on(), power_off()
#include "config.h"
#include "util.h"




/******************************************************************************
 * GLOBAL DATA
 */
event_t **g_parsed  = NULL;         // For test compilation (use; event_schedule_commit() to use)
char     *g_errors  = NULL;         // bsprintf() buffer for parsing errors

// Today/Now, used to compared event expirations and calculate new 
static struct today_t
{
    struct {
        time_t   now;       // Epoch seconds (written by time())
        time_t   midnight;  // Epoch seconds at 00:00:00 today
        time_t   offset;    // now is, how many seconds after midnight
    } utc;  // Universal Standard Time
    struct tm    lst; // Local Standard Time
} g_today = { { 0 } };

// externally available
// NOTE: arrfindnocase() is also used on this to determine the code value
//       and therefore this should not be converted into a function.
//       Larger redesign might see a totally different kind of struct array
//       be implemented, with default intervals and additional information.
typedef struct 
{
    char *name;
    int   default_type;
} event_action_t;

event_action_t event_action[] =
{
    { "(null)",             0 },
    { "SUSPEND",            EVENT_TYPE_DAILY },
    { "RESUME",             EVENT_TYPE_DAILY },
    { "POWEROFF",           EVENT_TYPE_DAILY },
    { "POWERON",            EVENT_TYPE_DAILY },
    { "IMPORTTMPFS",        EVENT_TYPE_INTERVAL },
    { "IMPORTTMPFSTIMEOUT", EVENT_TYPE_ONCE },
    { "WATCHDOG",           EVENT_TYPE_INTERVAL },
    { NULL }
};

typedef struct
{
    char *prefix;
    char *name;
} event_scheduling_t;

event_scheduling_t event_type[] =
{
    { "(null)", "(null)" },
    { "",       "DAILY" },
    { "@",      "INTERVAL" },
    { "!",      "ONCE" },
    { NULL }
};


/******************************************************************************
 * PRIVATE FUNCTIONS
 */ 

// Returns zero if not found
#define ARRAY_SIZE(x) ( sizeof(x) / sizeof((x)[0]) ) 
static int __actionstring2code(char *actionstring)
{
    if (!actionstring || !*actionstring)
        return 0;
    int i;
    for (i = 1; i < ARRAY_SIZE(event_action) - 1; i++)
        if (!strcasecmp(actionstring, event_action[i].name))
            return i;
    return 0;
}

static int __attribute__ ((__unused__)) __devlog_tm(const char *str, struct tm *tm)
{
    if (!tm)
    {
        logdev("printtm(NULL);");
        return (errno = EINVAL, EXIT_FAILURE);
    }
    if (!str)
        str = "";
    logdev(
          "%s%s%02d:%02d:%02d %02d.%02d.%04d%s",
          str, *str ? " " : "",
          tm->tm_hour,
          tm->tm_min,
          tm->tm_sec,
          tm->tm_mday,
          tm->tm_mon + 1,
          tm->tm_year + 1900,
          tm->tm_isdst > 0 ? " DST in effect" : " DST not in effect"
          );
    return EXIT_SUCCESS;
}

static int __attribute__ ((__unused__)) __devlog_time_t(const char *str, const time_t t)
{
    struct tm tm;
    if (str)
        logdev("[%10ld] %s", t, str);
    localtime_r(&t, &tm);
    __devlog_tm("    local time  :", &tm);
    gmtime_r(&t, &tm);
    __devlog_tm("    system time :", &tm);
    return EXIT_SUCCESS;
}

static void __attribute__ ((__unused__)) __devlog_today() 
{
    struct tm *tm;
    logdev("today (System Time is UTC+0):");
    tm = gmtime(&g_today.utc.now);
    logdev("  .utc.now        : %10ld (% 03d:%02d:%02d %02d.%02d.%04d)",
          g_today.utc.now,
          GETHOURS(g_today.utc.now),
          GETMINUTES(g_today.utc.now),
          GETSECONDS(g_today.utc.now),
          tm->tm_mday,
          tm->tm_mon + 1,
          tm->tm_year + 1900
          );
    tm = gmtime(&g_today.utc.midnight);
    logdev("  .utc.midnight   : %10ld (% 03d:%02d:%02d %02d.%02d.%04d)",
          g_today.utc.midnight,
          GETHOURS(g_today.utc.midnight),
          GETMINUTES(g_today.utc.midnight),
          GETSECONDS(g_today.utc.midnight),
          tm->tm_mday,
          tm->tm_mon + 1,
          tm->tm_year + 1900
          );
    logdev("  .utc.offset     : %10ld (% 03d:%02d:%02d %2d days)",
          g_today.utc.offset,
          GETHOURS(g_today.utc.offset),
          GETMINUTES(g_today.utc.offset),
          GETSECONDS(g_today.utc.offset),
          GETDAYS(g_today.utc.offset)
          );
    logdev("  .lst            : %10ld (% 03d:%02d:%02d %02d.%02d.%04d%s)",
          mktime(&g_today.lst),
          g_today.lst.tm_hour,
          g_today.lst.tm_min,
          g_today.lst.tm_sec,
          g_today.lst.tm_mday,
          g_today.lst.tm_mon + 1,
          g_today.lst.tm_year + 1900,
          cfg.event.apply_dst == false ? ", DST not applied" : ""
          );
    logdev("    .tm_isdst     : %10d", g_today.lst.tm_isdst);
#ifdef __USE_BSD                // Although I make no provisions for BSD otherwise...
    logdev("    .tm_gmtoff    : %10ld (%+03d:%02d:%02d %2d days%s)",
          g_today.lst.tm_gmtoff,  // Seconds east of UTC
          GETHOURS(g_today.lst.tm_gmtoff),
          GETMINUTES(g_today.lst.tm_gmtoff),
          GETSECONDS(g_today.lst.tm_gmtoff),
          GETDAYS(g_today.lst.tm_gmtoff),
          g_today.lst.tm_isdst > 0 ? ", DST in effect" : ", DST not in effect"
          );
    logdev("    .tm_zone      : %10s", g_today.lst.tm_zone); // Timezone abbreviation
#else
    logdev("    .__tm_gmtoff  : %10ld (%+03d:%02d:%02d %2d days%s)",
          g_today.lst.__tm_gmtoff, // Seconds east of UTC
          GETHOURS(g_today.lst.__tm_gmtoff),
          GETMINUTES(g_today.lst.__tm_gmtoff),
          GETSECONDS(g_today.lst.__tm_gmtoff),
          GETDAYS(g_today.lst.__tm_gmtoff),
          g_today.lst.tm_isdst > 0 ? ", DST in effect" : ", DST not in effect"
          );
    logdev("    .__tm_zone    : %10s", g_today.lst.__tm_zone); // Timezone abbreviation
#endif
}

/******************************************************************************
// UNITTEST HACK
//
// Unittesting code MUST call below function to set "current time"
*/
#ifdef _UNITTEST
static time_t _time;
void event_unittest_settime(time_t t)
{
//__devlog_time_t("event_unittest_settime()", t);
    _time = t;
}
#endif

/*
 * (Re)load global struct today
 */
static void __update_today()
{
    time_t tmptime;
#ifdef _UNITTEST
    g_today.utc.now = _time;
#else
    time(&g_today.utc.now);       // Gives UTC (system time)
#endif
// UNITTEST HACK END
/*****************************************************************************/
    g_today.utc.offset    = g_today.utc.now % SECONDS_PER_DAY;
    g_today.utc.midnight  = g_today.utc.now - g_today.utc.offset;
    // Observe cfg.apply_dst, IF DST is in effect
    localtime_r(&g_today.utc.now, &g_today.lst);
    if (g_today.lst.tm_isdst > 0 && cfg.event.apply_dst == false)
    {
        tmptime = g_today.utc.now - SECONDS_PER_HOUR;
        localtime_r(&tmptime, &g_today.lst);
    }
}

/*
 *  Calculate new .next_trigger
 *
 *          event_t.localoffset has a different interpretation depending on
 *          event_t.schedulingtype. If the event is normal daily event, the
 *          .localoffset is seconds from local standard midnight. For interval
 *          event, .localoffset is simply the interval in seconds.
 *
 *          For daily events the range is 0 .. (SECONDS_PER_DAY - 1) and
 *          for interval events any positive value is accepted.
 *
 *          This function also observed cfg.event.apply_dst configuration
 *          setting:
 *     0    Initially presumes that Daylight Savings Time (DST) is not in effect.
 *    >0    Initially presumes that DST is in effect.
 *    -1    Actively determines whether DST is in effect from the specified time
 *          and the local time zone. Local time zone information is set by the
 *          tzset subroutine.
 */
static void __schedule_next_trigger(event_t *e)
{
    __update_today();
    //
    // Simplest possible (interval)
    //
    if (e->type == EVENT_TYPE_DAILY)
    {
        struct tm   localtm;
        time_t      eventtime;  // UTC+0

        // Warn if localoffset is more than (24h + 2h)
        // (+2h is maximum DST that I have heard of, wherever it may be used)
        if (e->localoffset >= SECONDS_PER_DAY + 2 * SECONDS_PER_HOUR)
        {
            logerr(
                  ".localoffset (%ld) is too large! (%02d:%02d:%02d)",
                  e->localoffset,
                  GETHOURS(e->localoffset),
                  GETMINUTES(e->localoffset),
                  GETSECONDS(e->localoffset)
                  );
        }
        else if (e->localoffset < 0)
        {
            // Using zero here does not have much to do with logic.
            // Only reason is that I regard the continued datalogging
            // much more important than malformed scheduled event and
            // thus refuse to make it an error that would terminate the
            // daemon.
            logerr(
                  "arg1 (%ld) is negative. Substituting with zero value.",
                  e->localoffset
                  );
            e->localoffset = 0;
        }

        memset(&localtm, 0, sizeof(struct tm));
        // Now/Today's values
        localtm.tm_year    = g_today.lst.tm_year;
        localtm.tm_mon     = g_today.lst.tm_mon;
        localtm.tm_mday    = g_today.lst.tm_mday;
        localtm.tm_isdst   = cfg.event.apply_dst;       // config.h
        // Event specific time
        localtm.tm_hour    = GETHOURS(e->localoffset);
        localtm.tm_min     = GETMINUTES(e->localoffset);
        localtm.tm_sec     = GETSECONDS(e->localoffset);

        // get system time_t 
        eventtime = mktime(&localtm);

//__devlog_today();
        // If already expired for today...
//printf("eventtime=%ld <= now=%ld\n", eventtime, g_today.utc.now);
        if (eventtime <= g_today.utc.now)
        {
            // ...we want to move it till tomorrow
            // We do this through mktime() for the simple reason that doing so
            // ensures that DST is observed (if DST gets applied during the next
            // 24 hours)
            localtm.tm_mday += 1;
            eventtime = mktime(&localtm);
        }
        e->next_trigger = eventtime;
    } // else if e->scheduletype DAILY
    else
    // EVENT_TYPE_INTERVAL and EVENT_TYPE_ONCE
    {
        /*
        logdev(
              "\"%s\".next_trigger = %d",
              event_getactionstr(e->action),
              e->next_trigger
              );
        */
        if (e->next_trigger)
            e->next_trigger += e->localoffset;
        else
            e->next_trigger = g_today.utc.now + e->localoffset;
    }
}


/*
 * For event_create_schedule()'s qsort()
 */
static int __cmpevent(const void *a, const void* b)
{
    return (*((event_t **)a))->localoffset - (*((event_t **)b))->localoffset;
}

/*****************************************************************************/
// Headerfile listed functions
/*****************************************************************************/

#define ARRAY_SIZE(x) ( sizeof(x) / sizeof((x)[0]) ) 
char *event_getactionstr(int action)
{
    if (action >= 0 && action < ARRAY_SIZE(event_action) - 1)
        return event_action[action].name;
    return "(unknown)";
}

int event_test_size()
{
    if (!g_parsed)
        return (errno = ENODATA, 0);
    int n;
    for (n = 0; g_parsed[n]; n++);
    return (errno = 0, n);
}

int event_schedule_size()
{
    return eventheap_size();
}

time_t event_reschedule(event_t *event)
{
    // No NULLs and no EVENT_TYPE_ONCE, please
    if (!event)
    {
        logerr("arg1 is NULL");
        return (errno = EINVAL, -1);
    }
    else if (event->type == EVENT_TYPE_ONCE)
    {
        logerr("Event type EVENT_TYPE_ONCE cannot be rescheduled!");
        return (errno = EINVAL, -1);
    }
    // calculate new .next_trigger -value
    __schedule_next_trigger(event);
    time_t now = time(NULL);
    if (event->next_trigger < now)
    {
        logerr("__schedule_next_trigger(event) failed to create .next_trigger correctly!");
        exit(EXIT_FAILURE);
    }
    eventheap_insert(event);
    return (errno = 0, event->next_trigger);
}

/*
 * Execute all schedulet events/commands since the last event
 * NOTE: global last_event will be updated accordingly
 */
int event_execute(event_t *event)
{
    if (!event)
    {
        logerr("arg1 is NULL");
        return (errno = EINVAL, EXIT_FAILURE);
    }
    switch (event->action)
	{
        case EVENT_ACTION_SUSPEND:
            return daemon_suspend();           // daemon.c
            break;
        case EVENT_ACTION_RESUME:
            return daemon_resume();            // daemon.c
            break;
        case EVENT_ACTION_POWEROFF:
            return power_off();                // power.c
            break;
        case EVENT_ACTION_POWERON:
            return power_on();                 // power.c
            break;
        case EVENT_ACTION_IMPORTTMPFS:
            return daemon_importtmpfs();       // daemon.c
            break;
        case EVENT_ACTION_IMPORTTMPFSTIMEOUT:
            return daemon_importtmpfstimeout(); // daemon.c
            break;
        case EVENT_ACTION_WATCHDOG:
            return daemon_watchdog();          // daemon.c
            break;
        default:
            logerr(
                  "Unrecognized event action code (%d) received!",
                  event->action
                  );
	}
    return (errno = EINVAL, EXIT_FAILURE);
}

/*
 * Return the next event from the schedule, if it has been triggered
 * If the next has not triggered, NULL is returned.
 */ 
event_t *event_gettriggered(time_t now)
{
    return eventheap_fetchtriggered(now);
}

char *event_test_errors()
{
    return (g_errors ? g_errors : "");
}


/*
 * Return pointer to the next event
 */
event_t *event_next()
{
    return eventheap_peek();
}

/*
 * COMMIT TEST SCHEDULE (THIS IS AN ADD OPERATION!)
 *
 */
void event_commit_test_schedule()
{
    if (!g_parsed)
        return;

    // Insert events into eventheap
    // confusing bug fixed here... event_clear_parsed() free()'d the events,
    // since it did not know if they were committed or not... so now we NULL
    // g_parsed slots as we go... This way committed event_t memory buffers
    // are safe, and if user decides NOT to commit, they will be free()'s.
    int n;
    for (n = 0; g_parsed[n]; n++)
    {
        eventheap_insert(g_parsed[n]);
        g_parsed[n] = NULL; // Important this here is --Yoda
    }

    // set 'parsed' to NULL so that the next call to event_compile_schedule()
    // does not destroy our committed events
    event_test_clear();

    // NOTE: State determination IS NOT a concern of event scheduler.
    //       Separate routines are needed and they should mostly reside
    //       in the daemon.c anyway.
    logdev("%d events committed", eventheap_size());
    return;
}

/*
 * Release schedule (destroy eventheap)
 */
void event_schedule_clear(int source)
{
    if (source > 2) // BOTH = (PARSED(1) | INTERNAL(2))
        eventheap_destroy();
    else
    {
        // They could all be of saved type
        event_t **tmp = calloc(eventheap_size() + 1, sizeof(event_t *));
        event_t *event;
        int     index = 0;
        while ((event = eventheap_fetch()))
        {
            if (event->source & source)
                free(event);
            else
            {
                tmp[index] = event;
                index++;
            }
        }
        // put saved ones back
        for (index = 0; tmp[index]; index++)
            eventheap_insert(tmp[index]);
    }
}

void event_test_clear()
{
    event_t **tmp;
    if (g_parsed)
    {
        for (tmp = g_parsed; *tmp; tmp++)
            free(*tmp);
        free(g_parsed);
    }
    if (g_errors)
        free(g_errors);
    g_parsed = NULL;
    g_errors = NULL;
}

/*
#define PARSED_SLOTS_FREE \
    ((g_parsed) ? ((malloc_usable_size(g_parsed) / sizeof(void *)) - (event_test_size() + 1)) : 0)

// NOTE to self, malloc_usable_size() can tolerate NULL
int event_test_insert(event_t *e)
{
    if (!e)
        return (errno = EINVAL, EXIT_FAILURE);
    if (
        !e->action ||
        (e->type == EVENT_TYPE_DAILY && e->localoffset < 0) ||
        (e->type == EVENT_TYPE_INTERVAL && e->localoffset <= 60) ||
        (e->type == EVENT_TYPE_ONCE && e->localoffset <= 60)
       )
        return (errno = EINVAL, EXIT_FAILURE);

    __schedule_next_trigger(e);

    int nevents = event_test_size(); // returns 0 even if g_parsed is NULL pointer
    if (!PARSED_SLOTS_FREE)
    {
        if (!g_parsed)
            g_parsed = calloc(1, sizeof(void *) * 2);
        else
            g_parsed = realloc(g_parsed, malloc_usable_size(g_parsed) + sizeof(void *));
    }
    // It is not certain that realloc()'ed buffer contains zero's
    // If we would write this: g_parsed[event_parsed_size()] = e; and
    // g_parsed[event_parsed_size()] = NULL; (latter would be unnecessary
    // if we could be sure of the zero content), it would be not only
    // inefficient (two calls to count the length) but would also be
    // incorrect if the first statement replaces our NULL termination
    // with an element pointer, there by exposing unknown number of
    // non-null values for the event_parsed_size() to count.
    g_parsed[nevents] = e;
    g_parsed[nevents + 1] = NULL;

    return (errno = 0, EXIT_SUCCESS);
}
int event_test_create(int action, int schedulingtype, time_t seconds)
{
    event_t *e = calloc(1, sizeof(event_t));
    e->action         = action;
    e->type           = schedulingtype;
    e->localoffset    = seconds;
    if (event_test_insert(e))
    {
        free(e);
        return EXIT_FAILURE;
    }
    return (errno = 0, EXIT_SUCCESS);
}
*/
/*
 * Create new event into the production schedule (eventheap).
 * Returns triggering time_t for created event.
 * Should write argument range checks...
 */
time_t event_create(int action, time_t seconds)
{
    if (
       action > EVENT_ACTION_MAXVALUE ||
       action < 1 ||
       seconds < 1
       )
    {
        return (errno = EINVAL, -1);
    }
    // Values are OK, create the event
    event_t *event = calloc(1, sizeof(event_t));
    event->action         = action;
    event->type           = event_action[action].default_type;
    event->source         = EVENT_SOURCE_INTERNAL;      // internal: not parsed from a string
    event->localoffset    = seconds;
    // calculate new .next_trigger -value
    __schedule_next_trigger(event);
    // Some paranoia checking
    time_t now = time(NULL);
    if (event->next_trigger < now)
    {
        logerr("__schedule_next_trigger(event) failed to create .next_trigger correctly!");
        free(event);
        exit(EXIT_FAILURE); // non-recoverable error that cannot be ignored
    }
    eventheap_insert(event);
    return (errno = 0, event->next_trigger);
}

/******************************************************************************
 * Parses provided array into 'temporary' schedule buffer.
 * This function will output LOG_ERR messages for problems.
 *
 * RETURN
 *      EXIT_SUCCESS / 0        All events parsed OK
 *      EXIT_SUCCESS / ENODATA  No events to parse (most likely a fault in implementation)
 *      <1..n>       / EINVAL   <n> events discarded for parsing errors
 *      -1           / EINVAL   arg1 is NULL or pointer to NULL
 *
 */
int event_test_parse(char **array)
{
    if (!array)
    {
        logerr("arg1 is NULL");
        return (errno = EINVAL, -1);
    }
    // Remove emptry strings
    arrcollapse(array);
    if (!*array)
    {
        logerr("arg1 is empty");
        return (errno = EINVAL, -1);
    }

    // Release old 'parsed' (and 'errors' string buffer), if exists
    if (g_parsed)
        event_test_clear();

    // Get today/now 
    __update_today();

    /*
     * Allocate parsed -vector
     *
     *      By design, this function is expected to insert additional events
     *      for RESUME events, if external power control has been defined and
     *      if start up delay has been defined for the modem.
     *
     *      Since we cannot determine the number of such extra events before
     *      the array has been parsed, this function will calculate them later
     *      and allocate/create a new list to replace the first.
     */
    // one pointer for each + 1 for null termination
    g_parsed = calloc(arrlen(array) + 1, sizeof(event_t *));


    int      index;         // used by for() -loop
    int      n_events = 0;  // count accepted events
    int      n_discarded = 0;
    int      readvalue;     // hold strtod() converted value
    char    *sptr, *eptr;   // start pointer and end pointer, respectively (within event string)
    // Build event_t locally and copy at the end of the loop
    event_t  e;
    for (index = 0; array[index]; index++)
    {
        memset(&e, 0, sizeof(event_t));
        e.source = EVENT_SOURCE_PARSED;

        // WHY SO MUCH TROUBLE PARSING THE EVENT STRINGS?
        //
        //      Unfortunately, sscanf() parsing proved to be substandard with
        //      my test set, failing to parse a few and most alarmingly in one
        //      case provided a false reading.
        //
        //      In this case, doing the parsing manually is worth it.
        /*
        int     n, hour, min;
        char    cmd[4];
        n = sscanf(array[index], " %02d %*[:] %02d %3s", &hour, &min, cmd);
        printf("N=%d \"%s\" => \"%d\" : \"%d\" \"%s\"\n", n, array[index], hour, min, cmd);
        */

        if (*array[index] == '\0')
        {
            bsprintf(&g_errors, "array[%02d] is empty string. Skipping event.\n", index);
            n_discarded++;
            continue;
        }
        // Start from the beginning of the string
        sptr    = array[index];

        //
        // Inspect the first character. If '@' character, this event has
        // arbitrary interval of HH:MM. Otherwise it is a daily event that
        // specifies the time of day it triggers.
        //
        if (*array[index] == '@')
        {
            e.type = EVENT_TYPE_INTERVAL;
            sptr++;
        }
        else if (*array[index] == '!')
        {
            e.type = EVENT_TYPE_ONCE;
            sptr++;
        }
        else
            e.type = EVENT_TYPE_DAILY;

        //
        // Read hours
        //
        readvalue = strtol(sptr, &eptr, 10);
        if (errno)
        {
            bsprintf(
                    &g_errors,
                    "event[%d] \"%s\" - Error converting hours! Skipping event.\n",
                    index,
                    array[index]
                    );
            n_discarded++;
            continue;
        }
        if (sptr == eptr) // 2st char in value is invalid for conversion
        {
            bsprintf(
                    &g_errors,
                    "event[%d] \"%s\" - No hours to convert! Skipping event.\n",
                    index,
                    array[index]
                    );
            n_discarded++;
            continue;
        }
        // hours in legal range?
        if (
           e.type == EVENT_TYPE_DAILY &&
           (readvalue < 0 || readvalue > 23)
           )
        {
            bsprintf(
                    &g_errors,
                    "event[%d] \"%s\" - Hours (%02d) not within accepted range (00 - 23). Skipping event.\n",
                    index,
                    array[index],
                    readvalue
                    );
            n_discarded++;
            continue;
        }
        else if (readvalue < 0)
        {
            bsprintf(
                    &g_errors,
                    "event[%d] \"%s\" - Hours (%02d) may not be negative. Skipping event.\n",
                    index,
                    array[index],
                    readvalue
                    );
            n_discarded++;
            continue;
        }
        e.localoffset = readvalue * SECONDS_PER_HOUR;
        // Check for valid separator
        if (*eptr != ':')
        {
            bsprintf(
                    &g_errors,
                    "event[%d] \"%s\" - Invalid separator for time (\"%c\"). Skipping event.\n",
                    index,
                    array[index],
                    *eptr
                    );
            n_discarded++;
            continue;
        }
        sptr = eptr + 1;


        //
        // Read minutes
        //
        readvalue = strtol(sptr, &eptr, 10);
        if (errno)
        {
            bsprintf(
                    &g_errors,
                    "event[%d] \"%s\" - Error converting minutes! Skipping event.\n",
                    index,
                    array[index]
                    );
            n_discarded++;
            continue;
        }
        if (sptr == eptr) // 2nd char in value is invalid for conversion
        {
            bsprintf(
                    &g_errors,
                    "event[%d] \"%s\" - No minutes to convert! Skipping event.\n",
                    index,
                    array[index]
                    );
            n_discarded++;
            continue;
        }
        // minutes in legal range?
        if (readvalue < 0 || readvalue > 59)
        {
            bsprintf(
                    &g_errors,
                    "event[%d] \"%s\" - minutes (%02d) not within accepted range (00 - 59). Skipping event.\n",
                    index,
                    array[index],
                    readvalue
                    );
            n_discarded++;
            continue;
        }
        e.localoffset += readvalue * SECONDS_PER_MINUTE;
        // skip whitespace
        while (*eptr && isspace(*eptr))
            eptr++;
        // if null, event string is missing code
        if (!*eptr)
        {
            bsprintf(
                    &g_errors,
                    "event[%d] \"%s\" - Missing code! Skipping event.\n",
                    index,
                    array[index]
                    );
            n_discarded++;
            continue;
        }
        //
        // Read Event Code
        //
        if (!(e.action = __actionstring2code(eptr)))
        {
            bsprintf(
                    &g_errors,
                    "event[%d] \"%s\" - Unrecognized event action \"%s\"! Skipping event.\n",
                    index,
                    array[index],
                    eptr
                    );
            n_discarded++;
            continue;
        }

        //
        // One more check to ensure positive .localoffset
        //
        if (e.localoffset < 0)
        {
            bsprintf(
                    &g_errors,
                    "event[%d] .localoffset for event \"%s\" "
                    "resulted in negative value (%d)! Skipping event.\n",
                    index,
                    array[index],
                    e.localoffset
                    );
            n_discarded++;
            continue;
        }

        //
        // Create .next_trigger (system time_t when the event triggers next)
        //
        __schedule_next_trigger(&e);
//        e.next_trigger = __localoffset_to_next_systemtime(e.localoffset, cfg.event.apply_dst);

        //
        // Successful parsing completed
        // Enter this event to the parsed array
        //
        g_parsed[n_events] = calloc(1, sizeof(event_t));
        memcpy(g_parsed[n_events], &e, sizeof(event_t));
        n_events++;

    } // for () all array elements



    //
    // Return now, if no events were parsed
    //
    if (n_discarded)
    {
        // Only dev logging - let the called do real printout
        logdev("%d events discarded as malformed", n_discarded);
        return (errno = EINVAL, n_discarded);
    }
    // none discarded, none parsed..
    // Caller provided a really bad array....
    if (!n_events)
    {
        // Only dev logging, caller will print out whatever is deemed necessary
        logdev("No events parsed successfully.");
        return (errno = ENODATA, EXIT_SUCCESS);
    }


    /*
     * Insert PWRON events, if conditions are correct
     *
     *      1. Control for power must be defined (for now; cfg.modem.powercontrol != 0)
     *      2. Modem power-up is set (for now; cfg.modem.powerupdelay != 0)
     *      3. Event action is EVENT_ACTION_RESUME
     *
     *      In such case, EVENT_ACTION_POWERON event is created, cfg.modem.powerupdelay
     *      seconds before EVENT_ACTION_RESUME.
     */
    int n_resume = 0;
    if (cfg.modem.powercontrol && cfg.modem.powerupdelay)
    {
        // Count EVENT_ACTION_RESUME
        event_t **eptr = g_parsed;
        while (*eptr)
        {
            if ((*eptr)->action == EVENT_ACTION_RESUME)
                n_resume++;
            eptr++;
        }
//        logdev("Number of EVENT_ACTION_RESUME found : %d", n_resume);
        if (n_resume)
        {
            // new pointer list size is n_events + n_resume + 1
            event_t **tmplst = calloc(n_events + n_resume + 1, sizeof(event_t *));
            event_t *epwron;
            // copy pointers and insert EVENT_ACTION_POWERON when necessary
            int srcidx = 0;
            int tgtidx = 0;
            for (;g_parsed[srcidx]; srcidx++)
            {
                if (g_parsed[srcidx]->action == EVENT_ACTION_RESUME)
                {
                    // create EVENT_ACTION_POWERON
                    epwron = calloc(1, sizeof(event_t));
                    epwron->type    = EVENT_TYPE_DAILY;
                    epwron->source  = EVENT_SOURCE_PARSED;
                    epwron->localoffset = g_parsed[srcidx]->localoffset - cfg.modem.powerupdelay;
                    if (epwron->localoffset < 0)
                        epwron->localoffset += SECONDS_PER_DAY;
                    __schedule_next_trigger(epwron);
//                    epwron->next_trigger = __localoffset_to_next_systemtime(epwron->localoffset, cfg.event.apply_dst);
                    epwron->action = EVENT_ACTION_POWERON;
                    tmplst[tgtidx] = epwron;
                    tgtidx++;
                }
                tmplst[tgtidx] = g_parsed[srcidx];
                tgtidx++;
            }
            // Replace the list
            free(g_parsed);
            g_parsed = tmplst;
        }
        else // if (n_resume)
        {
            // No EVENT_CMD_RESUME
            ;
        }
    }


// TODO
// ISSUE WARNINGS IF GENERATED EVENT PRECEEDS SUSPEND OR PWROFF events

    //
    // Sort events into ascending local time order
    //
    qsort(g_parsed, n_events + n_resume, sizeof(event_t *), __cmpevent);


    return (errno = 0, EXIT_SUCCESS);
}

/******************************************************************************
 *
 * bsprint_* functions
 *
 *****************************************************************************/

/*
 * Let eventheap handle it
 */
char *bsprint_schedule(char **buffer)
{
    return bsprint_eventheap(buffer);
}

/*
 * Parse/describe g_parsed
 */
char *bsprint_testparsed_schedule(char **buffer)
{
    if (!g_parsed)
    {
        bsprintf(buffer, "parsed schedule size : 0\n");
        return (errno = ENODATA, *buffer);
    }
    bsprintf(
            buffer,
            "parsed schedule size : %d\n",
            event_test_size()
            );
    int n = 0;
    for (n = 0; g_parsed[n]; n++)
        bsprint_event(buffer, g_parsed[n]);
    return (errno = 0, *buffer);
}

/*
 * used to recreate configuration strings
 */
char *bsprint_eventstr(char **buffer, event_t *e)
{
    if (!e)
        return (errno = EINVAL, *buffer);
    bsprintf(
            buffer,
            "%s%02d:%02d %s",
            event_type[e->type].prefix,
            GETHOURS(e->localoffset),
            GETMINUTES(e->localoffset), 
            event_action[e->action].name
            );
    return (errno = 0, *buffer);
}

#define SOURCESTR(x) ((x) ? ((x) == 1 ? "INTERNAL" : "PARSED") : "UNKNOWN")
char *bsprint_event(char **buffer, event_t *e)
{
    if (!e)
    {
        bsprintf(buffer, "bsprint_event(buffer, (null))\n");
        return (errno = EINVAL, *buffer);
    }
    struct tm   *tm;
    time_t      tmptime;
    bsprintf(buffer, "Address: 0x%08x\n", e);
    bsprintf(buffer, "Config String     : \"");
    bsprint_eventstr(buffer, e);
    bsprintf(buffer, "\"\n");
    bsprintf(buffer, "  .action         : %10d \"%s\"\n", e->action, event_action[e->action]);
    bsprintf(buffer, "  .type           : %10d \"%s\"\n", e->type, event_type[e->type].name);
    bsprintf(buffer, "  .source         : %10d \"%s\"\n", e->source, SOURCESTR(e->source));
    bsprintf(buffer, "  .localoffset    : %10d (% 03d:%02d:%02d %2d days)\n",
            (int)e->localoffset,
            GETHOURS(e->localoffset),
            GETMINUTES(e->localoffset),
            GETSECONDS(e->localoffset),
            GETDAYS(e->localoffset) // localoffset is WRONG if it ever has 24h or more!!
            );
    tm = gmtime(&e->next_trigger);
    bsprintf(buffer, "  .next_trigger   : %10ld (% 03d:%02d:%02d %02d.%02d.%04d) (UTC+0)\n",
            e->next_trigger,
            tm->tm_hour,
            tm->tm_min,
            tm->tm_sec,
            tm->tm_mday,
            tm->tm_mon + 1,
            tm->tm_year + 1900
            );
    tm = localtime(&e->next_trigger);
    if (tm->tm_isdst > 0 && cfg.event.apply_dst == false)
    {
        tmptime = e->next_trigger - SECONDS_PER_HOUR;
        tm = localtime(&tmptime);
    }
    bsprintf(buffer, "                  : %10ld (% 03d:%02d:%02d %02d.%02d.%04d) (local time%s)\n",
            e->next_trigger,
            tm->tm_hour,
            tm->tm_min,
            tm->tm_sec,
            tm->tm_mday,
            tm->tm_mon + 1,
            tm->tm_year + 1900,
            cfg.event.apply_dst == false ? ", DST not applied" : ""
            );
    return (errno = 0, *buffer);
}



/* EOF event.c */
