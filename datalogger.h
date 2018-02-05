/*
 * datalogger.h
 *
 *      Worker process routine.
 *
 */
#include <time.h>       // time_t

#ifndef __DATALOGGER_H__
#define __DATALOGGER_H__

/*
 * Datalogger() return codes
 *
 *      Only 8-bit value is returned from child process. This space is divided
 *      into "codes" and "flags". Flags signify some notable condition in the
 *      execution that still did not prevent the datalogger() from writing its
 *      data into the database. Code values are produces in situations where
 *      datalogger() cannot proceed and must abandon the attempt to collect
 *      data.
 *
 *      signed char return code 0b 0000 0000
 *
 *      Lowest 2-bits are reserved for terminal exit codes. (0 + 3 codes)
 *      These are conditions that terminate datalogger prematurely.
 *
 *      Higher bits are a flag values (6 values) and are reserved for
 *      non-terminal conditions.
 */
#define DATALOGGER_SUCCESS                  0           // execution fully successful
#define DATALOGGER_EXIT_FAILURE             1           // Overlap with EXIT_FAILURE (1) "General error"
#define DATALOGGER_SQLITE3_ERROR            2           // any kind of SQLite3 INSERT failure
#define DATALOGGER_RESERVED                 3

// These affect stored data, but process will still complete
#define DATALOGGER_FLAG_ICMPINET_TIMEOUT    (1 << 2)    // inet ping was killed
#define DATALOGGER_FLAG_ICMPMODEM_TIMEOUT   (1 << 3)    // modem ping was killed
#define DATALOGGER_FLAG_RESERVED            (1 << 4)    //
// Scrubber flags CANNOT exist at the same time -> collapse into 0 == no err, 1, 2 and 3 (2-bits)
#define DATALOGGER_FLAG_SCRUBBER_TIMEOUT    (1 << 5)    // scrubber was killed
#define DATALOGGER_FLAG_SCRUBBER_FAILURE    (1 << 6)    // normal exit, but non-zero exit code
#define DATALOGGER_FLAG_SCRUBBER_DATAERROR  (1 << 7)    // Scrubber data was incomplete/malformed

// Need to reallocate... propably to 
#define DATALOGGER_EXITCODE(c)              ((c) & 0x03)
#define DATALOGGER_ICMPINETTOUT(c)          ((c) & DATALOGGER_FLAG_ICMPINET_TIMEOUT)
#define DATALOGGER_ICMPMODEMTOUT(c)         ((c) & DATALOGGER_FLAG_ICMPMODEM_TIMEOUT)
#define DATALOGGER_SCUBBER_TIMEOUT(c)       ((c) & DATALOGGER_FLAG_SCRUBBER_TIMEOUT)
#define DATALOGGER_SCRUBBER_FAILURE(c)      ((c) & DATALOGGER_FLAG_SCRUBBER_FAILURE)
#define DATALOGGER_SCRUBBER_DATAERROR(c)    ((c) & DATALOGGER_FLAG_SCRUBBER_DATAERROR)

typedef struct
{
    unsigned int code                   : 2;
    unsigned int f_icmpinet_timeout     : 1;
    unsigned int f_icmpmodem_timeout    : 1;
    unsigned int f_reserved             : 1;
    unsigned int f_scrubber_timeout     : 1;
    unsigned int f_scrubber_failure     : 1;
    unsigned int f_scrubber_dataerror   : 1;
} datalogger_exitvalue;

#endif /* __DATALOGGER_H__ */

/*
 * Function prototypes
 *
 *  datalogger(time_t)
 *
 *      The "worker" routine which will send the ICMP Echo Request packets and
 *      execute external script that will retrieve DOCSIS modem line dB values.
 *
 *      time_t is the Unix timestamp (since epoch) and it is inserted into the
 *      database to mark the date and time when the data record was collected.
 *
 *      Return value is a 8-bit byte value that is a combination of a code and
 *      four possible flags. Please see above for explanations and defines.
 *      (return value uses only the least significant byte from the 32-bit int)
 *
 *  datalgger_errorstring(int)
 *
 *      UNIMPLEMENTED
 *
 *      This function will allocate a string buffer and parse explanations
 *      corresponding to the datalogger() return value.
 *
 *      Caller is responsible for free()'ing up the buffer when no longer
 *      needed.
 */
int   datalogger(time_t);
char *datalogger_errorstring(int);

/* EOF datalogger.h */


