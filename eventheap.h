/*
 * eventheap.h - 2016 Jani Tammi <janitammi@gmail.com>
 *
 *      Specialized minimum heap for events (pointers). Heap is kept
 *      super-simple as a single malloc()'ed memory buffer. Zero index is
 *      used to record heap size and thus the actual heap is 1-indexed.
 *      (generally computers deal with zero indexes)
 *
 *      None of this matters as the API to this datastructure consists of only
 *      four function calls, none of which concern themselves with
 *      implementation specifics.
 *
 *  WHY
 *      Existing table model, static array of events, indexed by pointers that
 *      provide us with next and last etc... cannot cope with differing
 *      intervals. Only as long as the intervals are exactly the same,
 *      the table will stay in order.
 *
 *      Now that there is a need to schedule events that have unique intervals,
 *      an advanced datastructure is needed to keep them in chronological order
 *      despite the varying intervals.
 *
 *      This structure will still handle poorly if we need to remove/cancel
 *      an event which is not the next, but I will deal with that issue IF
 *      if becomes something that needs to be addressed. (There is no support
 *      for arbitrary removals, as of now).
 */
#ifndef __EVENTHEAP_H__
#define __EVENTHEAP_H__

#include "event.h"

/*
 * Add event to heap
 */
void        eventheap_insert(event_t *event);

/*
 * Get next event (does NOT remove it)
 */
event_t *   eventheap_peek();

/*
 * Return event if it triggered (.next_trigger <= now)
 * AND remove it from the heap. NULL returned if none triggered exist.
 */
event_t *   eventheap_fetchtriggered(time_t now);

/*
 * Returnes AND removes the next event
 */
event_t *   eventheap_fetch();

/*
 * Return the number of events in the heap
 */
int         eventheap_size();

/*
 * Destroy eventheap AND the events it contains
 */
void        eventheap_destroy();

/*
 * Describe eventheap
 *
 *      Parse/describe heap  by simulating emptying the whole
 *      datastructure by repeated evenheap_fetch() calls.
 *      eventheap is restored before this function returns.
 */
char *bsprint_eventheap();


#endif /* __EVENTHEAP_H__ */

/* EOF eventheap.h */