/*
 * schedule.h - 2016 Jani Tammi <janitammi@gmail.com>
 *
 * Daily Scheduling - Version 2
 *
 *          icmond needs a scheduling solution that supports arbitrary event
 *          intervals. Events that user can define happen once a day, but the
 *          internally scheduled events (COLLECTTMPFS, for now) can have any
 *          interval.
 *
 *          Example of user definable events:
 *
 *          schedule = 04:30 suspend, 05:00 resume, 11:15 suspend, 11:20 resume
 *
 *          Let's imagine it's 10:05 now. When the schedule is created,
 *          events 04:30 and 05:00 are already late and their initial
 *          triggering time will be postponed +24 hours. You could imagine:
 *          "11:15 SUSPEND, 11:20 RESUME, 27:30 SUSPEND, 28:00 RESUME"
 *
 * DATASTRUCTURES
 *
 *          Events will be stored into an event_t array for storage. They will
 *          be chronologically ordered by their .localoffset, but this is no
 *          longer absolutely necessary, like it was in the version 1.
 *
 *          Events will also be indexed into a minimum heap (eventheap.c) from
 *          which they are retrieved in the order that they will expire and
 *          re-inserted with updated .next_trigger values.
 *
 *          Start-up State
 *
 *          To determine the correct state for starting program, it needs to
 *          look into the latest expired event - in this case one just before
 *          "now" (10:05) is the "05:00 on". Program needs to execute this
 *          event during the start-up.
 *
 * WATCHDOG
 *
 *          TO-BE-IMPLEMENTED ...feature that triggers once a day and inspects
 *          all the events in the event array confirming that every one of
 *          them have an expiration time in the future (are active) and that
 *          time is less than 24 hours.
 *
 * SPECIAL CONDITIONS
 *
 *          1.  User many define events in whatever order. Program will be
 *              responsible for ordering them chronologically.
 *          2.  Time shall be HH:MM in 24-hour clock notation. Separator shall
 *              be ":" and recognized event codes/types shall be in this header.
 *          3.  If user defines two events for the same instant of time,
 *              (for example; 04:30 on, 04:30 off) the order in which they are
 *              executed behaviour vill be undefined.
 *
 * UTC+0 (also, Greenwich Mean Time) and Local Time
 *
 *          System time_t Epoch is UTC+0. User deals with Local Timer, which
 *          or may not be DST adjusted (daylight savings time).
 *
 *          Solution;   User provided times are adjusted to GMT and handled
 *                      as Epoch seconds.
 *
 * Daylight Savings
 *
 *          Special provisions are made to allow user to define "usedst"
 *          configuration option (true/false). This will determine if
 *          daylight savings shift is applied to time conversions.
 *
 *          If the user has mechanical 24 hour timer plug (which does not
 *          have the ability to adjust to DST), user will most likely want
 *          disable DST application in order to keep his suspended time in
 *          sync with the mechanical timer plug.
 *
 *          cfg.event.applydst = true | false
 *
 * Datastructure event_t
 *
 *          next_trigger    timestamp (in future) of the next occurance
 *          localoffset     [0 .. 25h] in seconds. Time from midnight (see below)
 *          code            numeric code for event action
 *          string          Textual representation of the event
 *
 *
 *          localoffset
 *
 *          Number of seconds from LST (Local Standard Time) midnight.
 *          LST means that daylight savings are not added to the midnight.
 *          If user set cfg.event.applydst > 0 (and thus his events are
 *          given in DST time), this value can be up to 25h (minus one second).
 *
 *          For example, the "04:30 off", when given in DST time AND during
 *          summer time (DST is +1 hours, generally), the event is actually set
 *          for 03:30 in local Standard Time. When calendar time progresses and
 *          local timezone exits DST, then the same given event will happen at
 *          04:30. If cfg.event.applydst > 0, event happens at the exact time
 *          in "local time" - the user's wrist watch would agree with it.
 *
 *          If cfg.event.applydst == 0, then given events are taken to be set
 *          for LST (Local Standard Time). During summer time, it appears to
 *          the user as if the events happen an hour late. User's wrist watch
 *          says it's 04:30, but the LST knows it's really 03:30 and it will
 *          take another hour before the event triggers. When it does, user's
 *          wrist watch claims it is 05:30.
 *
 *          localoffset WILL ALWAYS BE ADJUSTED TO INDICATE SECONDS FROM
 *          MIDNIGHT IN LOCAL STANDARD TIME.
 *
 *          Allowed range is from 0 seconds (midnight) to
 *          (full day - 1 second) + DST amount
 *
 *          NOTE: DST is not always +1 hour. It can be +30 minutes to +2 hours.
 *
 *          This value is stored so that the events can be sorted into logical
 *          chronological order and so that they can be re-parsed back into
 *          strings (when needed).
 *
 *
 *          next_trigger
 *
 *          This value is actual system time in Epoch seconds. When the working
 *          this value is always between "now" and "now" + 24h.
 *
 * ISO 8601, the associated time
 * https://en.wikipedia.org/wiki/ISO_8601
 *
 *          
 * USAGE
 *
 *  // pre-ops (config.c)
 *  event_create_schedule(stringarray); // called from config.c
 *
 *  // daemon (daemon.c)
 *  event_t *event;
 *  if ((event = event_next()))
 *  {
 *      __create_abs_timer(&this.schedule.fd);
 *      __set_timer(this.schedule.tspec, event->next_trigger);
 *      __add_to_fd_set();
 *  }
 *
 *  do {
 *      // FD_SET(this.schedule.fd, nfds);
 *      // pselect();
 *      if (FD_ISSET(this.schedule.fd, &readfds))
 *      {
 *          timerfd_acknowledge(this.schedule.fd);
 *          time_t now = time(NULL);
 *          event_t *event;
 *          while ((event = event_gettriggered(now)))
 *          {
 *              if (event_execute(event))
 *                  logfailure();
 *              event_reschedule(event);
 *          }
 *          event = event_next();
 *          _set_timer(this.schedule.tspec, event->next_trigger);
 *      }
 *  } while (this.state.running);
 *  schedule_free();
 */
#include <time.h>       // time_t

#include "keyval.h"

#ifndef __EVENT_H__
#define __EVENT_H__

// Scheduling schemas (type)
// Value 0 is considered uninitialized/invalid
#define EVENT_TYPE_DAILY                1   // daily at specified hour:min (dst possibly applied)
#define EVENT_TYPE_INTERVAL             2   // every hh:mm
#define EVENT_TYPE_ONCE                 3   // just once, after hh:mm
#define EVENT_TYPE_MAXVALUE             EVENT_TYPE_ONCE

// Value 0 is uninitialized/invalid
#define EVENT_ACTION_SUSPEND            1
#define EVENT_ACTION_RESUME             2
#define EVENT_ACTION_POWEROFF           3
#define EVENT_ACTION_POWERON            4
#define EVENT_ACTION_IMPORTTMPFS        5
#define EVENT_ACTION_IMPORTTMPFSTIMEOUT 6
#define EVENT_ACTION_WATCHDOG           7

#define EVENT_ACTION_MAXVALUE           EVENT_ACTION_WATCHDOG
#define EVENT_ACTIONSTR_MAXLEN         20

#define EVENT_SOURCE_UNKNOWN            0
#define EVENT_SOURCE_INTERNAL           1   // Created with a function call
#define EVENT_SOURCE_PARSED             2   // Parsed from userinput string

// Event Code string array ORDER MUST MATCH ABOVE DEFINED VALUES!!
//extern char *event_action[];      // instantiated in event.c

#define SECONDS_PER_DAY     86400
#define SECONDS_PER_HOUR     3600
#define SECONDS_PER_MINUTE     60

#define GETDAYS(s)      (int)(((s) / SECONDS_PER_DAY))
#define GETHOURS(s)     (int)(((s) % SECONDS_PER_DAY)  / SECONDS_PER_HOUR)
#define GETMINUTES(s)   (int)(((s) % SECONDS_PER_HOUR) / SECONDS_PER_MINUTE)
#define GETSECONDS(s)   (int)(((s) % SECONDS_PER_MINUTE))

// Some bitfields are signed on purpose. They need to be able to temporarily
// contain negative results from parsing/(other).
typedef struct event_tag
{
    time_t          next_trigger;                       // UTC+0 when this event triggers next time
    time_t          localoffset;                        // hours and minutes from midnight, local STANDARD time
    int             type   : 3;                         // [0-3] See EVENT_TYPE_* defines
    int             action : 5;                         // [0-8] See EVENT_ACTION_* defines (MUST BE SIGNED!)
    unsigned int    source : 2;                         // parsed or created with a function (internal)
//    char    string[1 + EVENT_ACTIONSTR_MAXLEN + 6 + 1];    // "23:59 OFF" reparsed to meet criteria 6 + code + null
} event_t;


/******************************************************************************
 * Test parsing functions
 *****************************************************************************/
/*
 * Parse char vector of events
 *
 *      Parses provided array into (internal) 'parsed' schedule buffer.
 *      Parsing errors will be stored into a buffer. User event_error() to get.
 *
 * RETURN
 *      EXIT_SUCCESS / 0        All events parsed OK
 *      EXIT_SUCCESS / ENODATA  No events to parse (most likely a fault in implementation)
 *      <1..n>       / EINVAL   <n> events discarded for parsing errors
 *      -1           / EINVAL   Invalid argument: arg1 is NULL or pointer to NULL
 *
 */
int      event_test_parse(char **arr);

/*
 * Return errors stringbuffer (useful after event_parse_schedule())
 * If no errors, NULL is returned
 */
char *   event_test_errors();

int      event_test_size();
//int      event_test_insert(event_t *event);
// ONLY WAY FOR INDIVIDUAL EVENTS TO THE SCHEDULE IS THROUGH THE SCHEDULE STRING
//int      event_test_create(int action, int schedulingtype, time_t time);
void     event_test_clear();

/*
 * Commit parsed test schedule
 *
 *      THIS IS AN ADD OPERATION!
 *
 *      Contents of the parsed test schedule will be inserted into the actual
 *      schedule (eventheap). The reason why this function does not
 *      automatically remove all .source = PARSED events is because this
 *      intends to support multiple parsed sources (although not used now).
 *
 * SIGHUP:  Processing this signal, user should call event_schedule_clear()
 *          with EVENT_SOURCE_PARSED argument before calling this function.
 *
 *
 *      This function will clear out the test schedule after before exiting.
 *
 */
void     event_commit_test_schedule();               // commit 'parsed' to eventheap

/*
 * Clear 'schedule' / 'parsed'
 *
 *      Function deletes contents and releases the memory.
 */
void     event_schedule_clear(int source);    // PARSED = 0x01, INTERNAL = 0x02, BOTH = 0x03
int      event_schedule_size();

/*
 * return action name (string) for the action code value
 */
char *   event_getactionstr(int action);

/*
 * Create / add new scheduled event
 *
 *      Creates and allocated the event, inserts it into the production
 *      schedule (eventheap) and returns the time_t for the next triggering
 *      of the new event.
 *
 * RETURN
 *      time_t  / 0         time_t for the next triggering of the new event
 *      -1      / EINVAL    Invalid argument(s)
 */
time_t   event_create(int action, time_t seconds);

/*
 * Returns a pointer to the next event in the schedule.
 * May or may not be triggered. Schedule is untouched.
 * Returns NULL (with errno ENODATA) if there is no schedule at all.
 */
event_t *event_next();

/*
 * Get triggered/expired event
 *
 *      If next event is triggered (compared to provided argument value)
 *      it is removed from the event heap and returned by this function.
 *      This call should be called repeatedly until no expired/triggered
 *      events remain in the eventheap.
 */
event_t *event_gettriggered(time_t now);

/*
 * Execute action
 */
int      event_execute(event_t *event);

/*
 * Reschedule event
 *
 *      New .next_trigger will be calculated and the event will be
 *      reinserted into the eventheap. Function returns the time_t
 *      for .next_trigger.
 */
time_t   event_reschedule(event_t *event);

/*
 * Describe event, 'parsed' or 'schedule'
 *
 *      Parse/describe 'parsed' into a string buffer. If there is nothing to
 *      describe (internal g_described remains NULL), a pointer to empty
 *      string will be returned, making this safe to use as an argument for
 *      printf() functions.
 */
char *   bsprint_event(char **buffer, event_t *event);
char *   bsprint_testparsed_schedule(char **buffer);
char *   bsprint_schedule(char **buffer);
char *   bsprint_eventstr(char **buffer, event_t *e);

#ifdef _UNITTEST
void event_unittest_settime(time_t t);
#endif // _UNITTEST

#endif /* __EVENT_H__ */

/* EOF event.c */
