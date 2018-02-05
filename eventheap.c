/*
 * eventheap.c - 2016 Jani Tammi <janitammi@gmail.com>
 *
 *      Heap will be a single allocated buffer of pointers to event_t
 *      objects. Unlike most datastructures in C, this one uses 1-based
 *      indexing (first pointer to an event_t is at index location 1).
 *      index location zero will keep track of the heap size.
 *
 *      Heap will be automatically reallocated each time the space runs
 *      out in SIZE_INCREMENT increments.
 *
 *
 *
 *  FIND MAX
 *      If need arises, simple approach is to scan the second half of the
 *      values. Maximum value will be in one of the leafs, and due to the
 *      structure, it means the latter half.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <time.h>

#include "eventheap.h"
#include "util.h"       // bsprintf()

#define SIZE_INCREMENT      256             // Steps in which size is increased
#define LCHILDINDEX(x)      (2 * (x))
#define RCHILDINDEX(x)      (2 * (x) + 1)
#define PARENTINDEX(x)      ((x) / 2)
#define HEAPSIZE            (*((int *)g_heap))
#define VAL(x)              (g_heap[(x)]->next_trigger)

// Size will be recorded in the zero index position
// Reading and modifying will obviously need casting...
static event_t **g_heap = NULL;

/*
 * Functions private to this translation unit
 */
       void heapify(int index);
inline void swap(int index1, int index2);


/******************************************************************************
 * Functions that are made available in the header
 *****************************************************************************/
void eventheap_destroy()
{
    if (!g_heap)
        return;
    event_t *event;
    while ((event = eventheap_fetch()))
        free(event);
    free(g_heap);
    g_heap = NULL;
}

int eventheap_size()
{
    if (!g_heap)
        return (errno = ENODATA, 0);
    return (errno = 0, *((int *)g_heap));
}

void eventheap_insert(event_t *e)
{
    if (!g_heap) // Initial allocation (or the next statement points to ???)
        g_heap = calloc(SIZE_INCREMENT, sizeof(event_t *));

    // Expand allocated heap if necessary
    if (malloc_usable_size(g_heap) / sizeof(event_t *) < HEAPSIZE + 2)
        g_heap = realloc(g_heap, malloc_usable_size(g_heap) + SIZE_INCREMENT);

    // Place event into the last slot
    int *size = (int *)g_heap;
    *size += 1;
    g_heap[*size] = e;

    // bubble up
    int index = *size;
    while (index > 1)
    {
        if (g_heap[index]->next_trigger < g_heap[PARENTINDEX(index)]->next_trigger)
        {
            swap(index, PARENTINDEX(index));
            index = PARENTINDEX(index);
        }
        else
            break;
    }
}

inline event_t *eventheap_peek()
{
    if (!g_heap || HEAPSIZE < 1)
        return (errno = ENODATA, NULL);
    return (errno = 0, g_heap[1]);
}

event_t *eventheap_fetch()
{
    if (!g_heap || HEAPSIZE < 1)
        return (errno = ENODATA, NULL);
    event_t *e = g_heap[1];
    g_heap[1] = g_heap[HEAPSIZE];
    HEAPSIZE--;
    heapify(1);
    return e;
}

event_t *eventheap_fetchtriggered(time_t now)
{
    if (!g_heap || HEAPSIZE < 1)
        return (errno = ENODATA, NULL);
    if (g_heap[1]->next_trigger > now)
        return (errno = 0, NULL);
    return (errno = 0, eventheap_fetch());
}

// Special kind - pops the whole datastructure 
// to show EXACTLY in what order will the events
// come out of the structure.
char *bsprint_eventheap(char **buffer)
{
    if (!g_heap)
    {
        bsprintf(buffer, "bsprint_eventheap(null)\n");
        return (errno = ENODATA, *buffer);
    }
//    if (HEAPSIZE < 1)
//    {
//        bsprintf(buffer, "g_heap size : 0");
//        return (errno = 0, *buffer);
//    }
    // Save the heap buffer so that it can be restored
    event_t *event;
    event_t **tmpheap = calloc(1, malloc_usable_size(g_heap));
    memcpy(tmpheap, g_heap, malloc_usable_size(g_heap));
    bsprintf(buffer, "g_heap size : %d\n", eventheap_size());
    while ((event = eventheap_fetch()))
        bsprint_event(buffer, event); // event.c
    memcpy(g_heap, tmpheap, malloc_usable_size(g_heap));
    free(tmpheap);
    return (errno = 0, *buffer);
}

/******************************************************************************
 * Functions private to this translation unit
 *****************************************************************************/

inline void swap(int index1, int index2)
{
    event_t *temp  = g_heap[index1];
    g_heap[index1] = g_heap[index2];
    g_heap[index2] = temp;
}

void heapify(int rootindex)
{
    int smallest;

    if (LCHILDINDEX(rootindex) <= HEAPSIZE &&
        VAL(LCHILDINDEX(rootindex)) < VAL(rootindex))
        smallest = LCHILDINDEX(rootindex);
    else
        smallest = rootindex;

    if (RCHILDINDEX(rootindex) <= HEAPSIZE &&
        VAL(RCHILDINDEX(rootindex)) < VAL(smallest))
        smallest = RCHILDINDEX(rootindex);

    if (VAL(smallest) != VAL(rootindex))
    {
        swap(smallest, rootindex);
        heapify(smallest);
    }
}

// log2(n) implementation
// DO NOT specify negative index! results in indefinite loop
inline int heaplevel(int index)
{
    int targetlevel = 0;
    while (index >>= 1)
        ++targetlevel;
    return targetlevel;
}

/* EOF eventheap.c */
