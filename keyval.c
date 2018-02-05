/*
 * keyval.c - 2016, Jani Tammi <janitammi@gmail.com>
 *
 * RULE 1:
 *          = -character delimits key from value(s).
 * RULE 2:
 *          # -character begins a comment. Comments are removed.
 * RULE 3:
 *          \ -character escapes the next character.
 * RULE 4:
 *          ,; -characters delimit a list
 * RULE 5:
 *          Delimiters do not delimit, escape does not escape (the next)
 *          and comment characters do not beging a comment
 *          if it itself has been escaped.
 *          3\,4,4 => "3,4" and "4"
 *          \\\\tunkki\\srv => "\\tunkki\srv"
 *          \3\,\4,\4 => "3,4" and "4"   (unnecessary escapes allowed)
 *          ncon\#.a\;01\=debug\\release => "ncon#.a;01=debug\release"
 * RULE 6:
 *          Whitespace characters are handled as equals to letters and digits
 *          with the exception that they are trimmed from strings.
 *          (removed from the beginning and the end)
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>            // true, false
#include <stdint.h>             // uintptr_t
#include <malloc.h>             // malloc(), malloc_usable_size()
#include <errno.h>
#include <ctype.h>               // isspace()

#include "keyval.h"
#include "logwrite.h"           // logdev(), logerr()
#include "util.h"

#define isdelim(c)              (int)strchr(KEYVAL_LIST_DELIMITERS, (c))
#define flipflag(i)             ((i) ? ((i) = false) : ((i) = true))

/*
 * Internal function to remove comments while observing escapes.
 * Will malloc() a working copy of the src string.
 * Caller is responsible for free()'ing the buffer when no loger needed.
 */
static char *__remove_comments(const char *src)
{
    char  chr;
    char *result;
    char *writeptr;
    char *readptr;
    int   f_escape = false;

    if (!src)
        return NULL;
    /* do NOT forget the null termination + 1 there! */
    if (!(result = malloc(strlen(src) + 1)))
        return NULL;


    writeptr = result;
    readptr  = (char *)src;
    while (*readptr)
    {
        chr = *readptr;
        /*
         * Unless escaped itself, '\' sets an escape for the next character.
         * Escape has to be monitored in and out of quotes, but it will be
         * preserved (printed) only within quotes.
         */
        if (chr == '\\' && !f_escape)
        {
            f_escape = true;
            *writeptr = chr;
            writeptr++;
        }
        /*
         * Unless escaped or within quotes, string terminates here!
         */
        else if (chr == '#' && !f_escape)
            break;
        else
        {
            f_escape = false;
            *writeptr = chr;
            writeptr++;
        }
        readptr++;
    }
    /* null terminate */
    *writeptr = '\0';
    return result;
}

/*
 * Internal function used to copy key and value strings to a keyval_t buffer.
 *
 * The "len" argument should be similar to strlen(src) result. Or in other
 * words, scr + len == src + strlen(src) == '\0' ...or in our case, the
 * terminating character WHICH WILL BE REPLACED WITH NULL for valid string
 * NULL termination.
 *
 * For example, with string :
 * " big thing = some value  "
 * value len should be 11 (str[11] == '=').
 */
static size_t __trimcpy(char *dst, const char *src, int len)
{
    const char *begin  = src;
    const char *end    = src + len;
    int         dstlen = 0;

    /* Trim leading space */
    while (isspace(*begin))
        begin++;

    /* If all spaces... */
    if (*begin == 0)
    {
        *dst = '\0';
        return 0;
    }

    /* Trim trailing space */
    do
    {
        end--;
    } while (end > begin && isspace(*end));
    end++;;     /* Step one back - the last tested was not space */

    /* Copy trimmed string and add null terminator */
    dstlen = end - begin;
    memcpy(dst, begin, dstlen);
    dst[dstlen] = '\0';
    return dstlen;
}

/*
 * Internal function to remove escpes from keyval_t array
 */
void __sanitize_values(keyval_t kv)
{
    int     f_escape;
    /* keyval and char indexes */
    int     kvindex, chindex;
    char   *kvstr, *writeptr;
    char    chr;

    if (!kv)
        return;

    /* handle each keyval string in turn */
    for (kvindex = 0; kv[kvindex]; kvindex++)
    {
        writeptr = kvstr = kv[kvindex];
        f_escape = 0;
        for (chindex = 0; kvstr[chindex]; chindex++)
        {
            chr = kvstr[chindex];
            /*
             * Unless escaped itself, '\' sets an escape for the next character.
             * Escape has to be monitored in and out of quotes, but it will be
             * preserved (printed) only within quotes.
             */
            if (chr == '\\' && !f_escape)
            {
                f_escape = true;
            }
            else if (chr == '\\' && f_escape)
            {
                f_escape = false;
                *writeptr = chr;
                writeptr++;
            }
            else
            {
                f_escape = false;
                *writeptr = chr;
                writeptr++;
            }
        } /* for (;;chindex++) */
        /* null terminate */
        *writeptr = '\0';
    }
}

/******************************************************************************
 *
 * __getkeyval()
 *
 *  NOTE:   Caller MUST have stripped comments before calling this function.
 *
 * 1)   Test for non-escapted and non-quoted '=' character.
 *      Exactly one must be found, or therwise the string cannot be processed.
 *      Pointer to value is set internally.
 * 2)   Scan value for non-quoted and non-escaped delimiters.
 *      This will be the number of values in the list.
 *      Empty is accepted! ("key = "). Value pointer will just be null
 *      in the returned buffer.
 * 3)   A result buffer is allocated for size:
 *          address_t key_ptr +
 *          address_t val_ptr * number_of_values +
 *          address_t null_prt +
 *          sizeof(source_string)
 *
 * RETURN
 *
 *      NULL        ...if provided string is not "key = val" format.
 *      keyval_t *  malloc()'ed buffer. See notes.
 */
static keyval_t __getkeyval(const char *src)
{
    keyval_t    result;
    int         ptrlst_size;
    int         buffer_size;
    int         srcindex;
    int         tgtindex;
    char        chr;
    int         f_escape;
    int         f_delim;

    char       *equalsignptr;      /* Will point to the '=' character   */
    /* key = from (src) to (equalsignptr), length = (equalsignptr - src)*/
    /* values = from (equalsignptr) to (src + strlen(equalsignptr))     */
//#ifdef __DEBUG
//printf("__getkeyval(): src: \"%s\"\n", src);
//#endif

    /*
     * Scan for non-escaped '=' character.
     * If there is no real key/val delimiter, we will return null.
     */
    f_delim = 0;
    f_escape = false;
    srcindex = tgtindex = 0;
    while (src[srcindex])
    {
        chr = src[srcindex];
        /*
         * Unless escaped itself, '\' sets an escape for the next character.
         * Escape has to be monitored in and out of quotes, but it will be
         * preserved (printed) only within quotes.
         */
        if (chr == '\\' && !f_escape)
            f_escape = true;
        /*
         * Unless escaped or within quotes, string terminates here!
         */
        else if (chr == '#' && !f_escape)
            break;
        else
        {
            if (chr == '=' && !f_escape)
            {
                f_delim++;
                equalsignptr = (char *)src + srcindex;
            }
            f_escape = false;
        }
        srcindex++;
    }

    if (f_delim != 1)
    {
//#ifdef __DEBUG
//printf("Number of delimiters != 1 (%d) returning NULL\n", f_delim);
//#endif
        return NULL;
    }
    /* At this point, there was only one valid equal sign and
     * equalsignptr has been set to point to the correct position in the src.
     */

    /*
     * Count non-escaped delimiters.
     * This is used to calculate the necessary space for
     * the pointer table in the buffer.
     */
    f_delim = 0;
    f_escape = false;
    srcindex = tgtindex = 0;
    while (src[srcindex])
    {
        chr = src[srcindex];
        /*
         * Unless escaped itself, '\' sets an escape for the next character.
         * Escape has to be monitored in and out of quotes.
         */
        if (chr == '\\' && !f_escape)
            f_escape = true;
        else
        {
            if (isdelim(chr) && !f_escape)
                f_delim++;
            f_escape = false;
        }
        srcindex++;
    }
    /* Now f_delim contains the number of valid list delimiters */
//#ifdef __DEBUG
//printf("Number of list delimiters: %d\n", f_delim);
//#endif

    /*
     * Calculate required buffer size
     * (hopefully portable... but could be (void *) too I suppose...)
     */
    ptrlst_size = 
        sizeof(uintptr_t) +                     /* key pointer                              */
        sizeof(uintptr_t) * (f_delim + 1) +     /* value pointers                           */
        sizeof(uintptr_t);                      /* null termination for the pointer list    */
    buffer_size =
        ptrlst_size +
        strlen(src) + 1;                        /* + 1 for null termination                 */

    if (!(result = malloc(buffer_size)))
    {
        return NULL;
    }

//#ifdef __DEBUG
//printf("Pointer list size:     %3.d (%d pointer slots)\n", ptrlst_size, ptrlst_size / 4);
//printf("Requested buffer size: %3.d\n", buffer_size);
//printf("Actual buffer size:    %3.d\n", malloc_usable_size(result));
//memset(result, '?', buffer_size);
//devlogheap(result);
//#endif
    /*
     * Build result buffer
     */
    char *writeptr;             /* imagine; "the tip of a pen" or such      */
    f_delim = 0;                /* Works as pointer index from now on       */
    /* Write pointer to buffer = First byte behind pointer list             */
    writeptr = (char *)result + ptrlst_size;
//#ifdef __DEBUG
//printf("result:   0x%.8X\n", (unsigned int)result);
//printf("writeptr: 0x%.8X\n", (unsigned int)writeptr);
//#endif
    /* Store key                                                            */
    result[f_delim] = writeptr; /* write pointer goes to the pointer list   */
    writeptr += __trimcpy(writeptr, src, equalsignptr - src);
    *writeptr = '\0';
    writeptr++;

    /* scanptr - will traverse over the string in the following loop         */
    char *scanptr = equalsignptr + 1;        /* +1 char from '='             */
    /* valptr - will "remember" the start of a value string and be updated  */
    /* to the beginning of the next value, if there is a list of values     */
    char *valptr = scanptr;                  /* +1 char from '=', like scanptr*/
    f_escape = false;
    for (; scanptr <= (src + strlen(src)); scanptr++)
    {
        chr = (char)*scanptr;
        /*
         * Unless escaped itself, '\' sets an escape for the next character.
         * Escape has to be monitored in and out of quotes.
         */
        if (chr == '\\' && !f_escape)
            f_escape = true;
        else
        {
            if ((isdelim(chr) || chr == '\0') && !f_escape)
            {
                /*
                 * Valid delimiter (or NULL termination) found,
                 * this is our end pointer for this value string.
                 */
                f_delim++;
                result[f_delim] = writeptr;
                writeptr += __trimcpy(writeptr, valptr, (scanptr - valptr));
                *writeptr = '\0';
                writeptr++;
                valptr = scanptr + 1;
            }
            f_escape = false;
        }
        srcindex++;
    }
    /* terminate pointer list */
    f_delim++;
    result[f_delim] = (char *)NULL;
//#ifdef _DEV
//printf("__getkeyval():\n");
//devlogheap(result);
//#endif
    return result;
}

/*****************************************************************************/
//
// PUBLIC FUNCTIONS
//
/*****************************************************************************/

/*
 * Public function to create keyval_t buffer from provided string
 */
keyval_t keyval_create(const char *keyvalstring)
{
    char    *workbuffer;
    keyval_t kv;
    if (!(workbuffer = __remove_comments(keyvalstring)))
    {
        logdev("__remove_comments() returned NULL!\n");
        return NULL;
    }
    kv = __getkeyval(workbuffer);
    __sanitize_values(kv);
    if (workbuffer)
        free(workbuffer);
    return kv;
}


/*
 * Return the number of values in kv (0 = none, 1 ... n)
 */
int keyval_nvalues(keyval_t kv)
{
    int n;
    if (!kv)
        return 0; /* ...or -1 ...? */
    for (n = 0; kv[n]; n++);
    return n - 1;
}

/*
 * Case insensitive check if the key in keyval_t is the same as argument 1
 */
int keyval_iskey(keyval_t kv, const char *keyname)
{
//    int cmplen = 0;
    if (!kv || !keyname)
        return (errno = EINVAL, false);
    // Determine shorter of the strings
//    cmplen = strlen(kv[0]) < strlen(keyname) ? strlen(kv[0]) : strlen(keyname);
    // Determine longer of the strings
//    cmplen = strlen(kv[0]) > strlen(keyname) ? strlen(kv[0]) : strlen(keyname);
//logdev("strncasecmp(\"%s\", \"%s\", %d) = %s", kv[0], keyname, cmplen, strncasecmp(kv[0], keyname, cmplen) ? "DIFFERENT" : "SAME");
//    if (strncasecmp(kv[0], keyname, cmplen))
    if (strcasecmp(kv[0], keyname))
        return (errno = 0, false);
    else
        return (errno = 0, true);
}

/*
 * Returns a malloc()'ed string containing delimited list of
 * values, contained in the provided keyval_t.
 * /
char *keyval_getlist(keyval_t kv)
{
    char   *result;
    char   *writeptr;
    int     strlength = 0;
    int     idx;
    // calculate needed buffer size
    for (idx = 1; kv[idx]; idx++)
        strlength += strlen(kv[idx]);
    strlength += idx - 1;   // one delimiter char between each value
    strlength++;            // for null termination
    // allocate result buffer
    if (!(result = malloc(strlength)))
        return NULL; // so unlikely that perhpas we should terminate ...
    writeptr = result;
    logdev("allocated %d/%d bytes\n", malloc_usable_size(result), strlength);
    // write the list
    for (idx = 1; kv[idx]; idx++)
        writeptr += sprintf(writeptr, "%s%c", kv[idx], *KEYVAL_LIST_DELIMITERS);
    // last delimiter is superfluous, change into null termination
    *(writeptr - 1) = '\0';
    return result;
}
*/
/*
 * Very simply shifts pointer list values one "down", thus overwriting the
 * key pointer, leaving the array with only value pointers.
 */
char **keyval2array(keyval_t kv)
{
    if (!kv)
        return (errno = EINVAL, NULL);
    int idx;
    for (idx = 1; kv[idx]; idx++)
        kv[idx - 1] = kv[idx];
    return (errno = 0, (char **)kv);
}

/*
 * Allocates a new buffer and compiles delimited
 * list string therein.
 */
static char *__2str(keyval_t kv, int fromindex)
{
    if (!kv)
        return (errno = EINVAL, NULL);
        
    // calculate necessary space
    int idx, len;
    for (idx = fromindex, len = 0; kv[idx]; idx++)
    {
        len += strlen(kv[idx]) + 1;
    }
    // reserve room for null termination
    len++;

    char *buffer = calloc(1, len);
    char *ptr = buffer;
    // compile list string
    for (idx = fromindex; kv[idx]; idx++)
    {
        if (idx - fromindex) // if not first item
            ptr += sprintf(ptr, "%c", *KEYVAL_LIST_DELIMITERS);
        ptr += sprintf(ptr, "%s", kv[idx]);
    }

    return (errno = 0, buffer);
}

char *keyval2valstr(keyval_t kv)
{
    return __2str(kv, 1);
}

char *keyval2str(keyval_t kv)
{
    char *str = __2str(kv, 0);
    // Even if the string contains nothing else than the key,
    // it still has enough room for the '=' below AND still
    // has a NULL termination. (see __2str() )
    *(str + strlen(kv[0])) = '=';
    return str;
}

/*
 * Removes pointers to empty strings. Only does so with values,
 * empty key will be left as-is.
 */
keyval_t keyval_remove_empty_values(keyval_t kv)
{
    int valueidx;
    int writeidx;
    if (!kv)
        return (errno = EINVAL, NULL);

    for (valueidx = 1, writeidx = 1; kv[valueidx]; valueidx++)
    {
        if(strlen(kv[valueidx]))
        {
            if (valueidx != writeidx)
            {
                kv[writeidx] = kv[valueidx];
                kv[valueidx] = NULL;
            }
            writeidx++;
        }
        else
            kv[valueidx] = NULL;
    }
    return (errno = 0, kv);
}

/******************************************************************************
 * Parse description for keyval_t
 *
 *      Uses util.c:bsptrinf() buffer printing.
 *      "printf() safe" - does not return NULL.
 *      "free() safe" - all returned strings CAN (and should) be free()'d.
 *      bsfree(&buffer) highly recommended.
 */
char *bsprint_keyval(char **buffer, keyval_t kv)
{
    if (!kv)
    {
        logdev("bsprint_keyval(): NULL pointer received. Returning...");
        bsprintf(buffer, "bsprint_keyval(null)\n");
        return (errno = EINVAL, *buffer);
    }
    bsprintf(buffer, "address          : 0x%.8X\n", (unsigned int)kv);
    bsprintf(buffer, "buffer size      : %d Bytes\n", malloc_usable_size((void *)kv));
    bsprintf(buffer, "number of values : %d\n", keyval_nvalues(kv));
    int index;
    for (index = 0; kv[index]; index++)
    {
        if (!index)
            bsprintf(buffer, "key          : \"%s\"\n", kv[index]);
        else
            bsprintf(buffer, "val[%d]       : \"%s\"\n", index, kv[index]);
    }
    bsprintf(buffer, "keyval_t heap:\n");
    bsprint_heap(buffer, kv);
    return (errno = 0, *buffer);
}

/* EOF keyval.c */
