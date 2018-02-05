/*
 * database.h
 *
 *      LINE VALUES
 *
 *      Downstream Power: (-5 dBmV to 5 dBmV)
 *          You generally want between -12dB and +12dB.
 *          Most modems are rated from -15dB to +15dB.
 *          Anything less or more than +/-15dB and you may have quality issues.
 *
 *      Downstream SNR: (32 dB to 50 dB)
 *          This value is best to be kept over 30dB,
 *          but you may not have any problems with down to 25dB.
 *          Anything less than 25dB and you will probably have slow transfers,
 *          dropped connections, etc.
 *
 *      Upstream Power: (32 dBmV to 50 dBmV)
 *          The lower this number is, the better. If it is above 55dBmV,
 *          you may want to see if you can reconfigure your splitters.
 *          Anything above 57dBmV is not good and should be fixed ASAP.
 *          (This is getting pretty close to not being able to connect.)
 *
 *      Upstream SNR: THIS INFORMATION IS NOT AVAILABLE FROM EPC3825!
 *          Anything above 29dB is considered good. The higher this number is,
 *          the better. If this number is below 29dB, you have a minute amount
 *          of noise leaking in somewhere. If it's anything less than 25dB,
 *          you want to get it fixed as you may have a lot of packet loss or
 *          slow transfer rates.
 *
 */
#include <float.h>     // DBL_MAX
#include <time.h>

#ifndef __DATABASE_H__
#define __DATABASE_H__

/*
 * SQLite3 Busy Timeout
 *
 *	The busy handler (default callback handler is NULL and you need not to
 *  worry about it) sleeps for a specified amount of time when a table is
 *  locked. The handler will sleep multiple times until at least specified
 *  number of milliseconds of sleeping have been accumulated.
 *
 *  After at least "ms" milliseconds of sleeping, the handler returns 0
 *  which causes sqlite3_step() to return SQLITE_BUSY.
 *
 *  In simpler terms; Time to try before giving up (and reporting SQLITE_BUSY)
 *
 *	NOTE:	Raspberry Pi 1 Model B with worlds-crappiest-SD-card took 3 seconds
 *			for each insert. Note however, that minimum interval for datalogger
 *			is 5 seconds...
 */
#define DATABASE_SQLITE3_BUSY_TIMEOUT	4000
#define DATABASE_DOUBLE_NULL_VALUE      DBL_MAX

/*
 * public configuration values structure
 */
typedef struct {
    time_t timestamp;           /* measurement datetime in Unix timestamp   */
    double modemping_ms;        /* ping response in mS                      */
    double inetping_ms;         /* ping response in mS                      */
    double down_ch1_dbmv;
    double down_ch1_db;
    double down_ch2_dbmv;
    double down_ch2_db;
    double down_ch3_dbmv;
    double down_ch3_db;
    double down_ch4_dbmv;
    double down_ch4_db;
    double down_ch5_dbmv;
    double down_ch5_db;
    double down_ch6_dbmv;
    double down_ch6_db;
    double down_ch7_dbmv;
    double down_ch7_db;
    double down_ch8_dbmv;
    double down_ch8_db;
    double up_ch1_dbmv;
    double up_ch2_dbmv;
    double up_ch3_dbmv;
    double up_ch4_dbmv;
} databaserecord_t;

typedef struct
{
    int    n;       // Number of samples
    double min;     // Fastest insert
    double mean;    // Average
    double max;     // Slowest insert
    double stddev;  // Standard Deviation
} dbperf_t;

#endif /* __DATABASE_H__ */

/*
 * Function prototypes
 */
int     database_initialize(char *datafile);
int     database_insert(char *datafile, databaserecord_t *record);
void	database_logdev(databaserecord_t *record);
/*
 * Delete row(s) matching to defined timestamp value.
 * If value < 0, deletes all rows
 */
int     database_delete(int timestamp);

/*
 * EVENT_CMD_COLLECTTMPFS handler
 * Read all data older than argument (olderthan) from tmpfs datafile and insert
 * them into the actual datafile. (deletes the records that were moved).
 */
int     database_collecttmpfs(time_t olderthan);

/*
 * Test SQLite3 write performance
 */
dbperf_t *database_testwriteperf(int nsamples);

/*
 * SQL statements used internally
 */
#define SQL_CREATE_TABLE_DATA " \
CREATE TABLE data ( \
    Timestamp       INTEGER, \
    ModemPing       REAL, \
    InetPing        REAL, \
    dCh1dBbmV       REAL, \
    dCh1dB          REAL, \
    dCh2dBbmV       REAL, \
    dCh2dB          REAL, \
    dCh3dBbmV       REAL, \
    dCh3dB          REAL, \
    dCh4dBbmV       REAL, \
    dCh4dB          REAL, \
    dCh5dBbmV       REAL, \
    dCh5dB          REAL, \
    dCh6dBbmV       REAL, \
    dCh6dB          REAL, \
    dCh7dBbmV       REAL, \
    dCh7dB          REAL, \
    dCh8dBbmV       REAL, \
    dCh8dB          REAL, \
    uCh1dBmV        REAL, \
    uCh2dBmV        REAL, \
    uCh3dBmV        REAL, \
    uCh4dBmV        REAL \
); "
#define SQL_CREATE_TABLE_BOUNDS " \
CREATE TABLE bounds ( \
    Timestamp       INTEGER, \
    maxModemPing    REAL, \
    maxInetPing     REAL, \
    mindCh1dBbmV    REAL, \
    maxdCh1dBbmV    REAL, \
    mindCh1dB       REAL, \
    maxdCh1dB       REAL, \
    mindCh2dBbmV    REAL, \
    maxdCh2dBbmV    REAL, \
    mindCh2dB       REAL, \
    maxdCh2dB       REAL, \
    mindCh3dBbmV    REAL, \
    maxdCh3dBbmV    REAL, \
    mindCh3dB       REAL, \
    maxdCh3dB       REAL, \
    mindCh4dBbmV    REAL, \
    maxdCh4dBbmV    REAL, \
    mindCh4dB       REAL, \
    maxdCh4dB       REAL, \
    mindCh5dBbmV    REAL, \
    maxdCh5dBbmV    REAL, \
    mindCh5dB       REAL, \
    maxdCh5dB       REAL, \
    mindCh6dBbmV    REAL, \
    maxdCh6dBbmV    REAL, \
    mindCh6dB       REAL, \
    maxdCh6dB       REAL, \
    mindCh7dBbmV    REAL, \
    maxdCh7dBbmV    REAL, \
    mindCh7dB       REAL, \
    maxdCh7dB       REAL, \
    mindCh8dBbmV    REAL, \
    maxdCh8dBbmV    REAL, \
    mindCh8dB       REAL, \
    maxdCh8dB       REAL, \
    minuCh1dBmV     REAL, \
    maxuCh1dBmV     REAL, \
    minuCh2dBmV     REAL, \
    maxuCh2dBmV     REAL, \
    minuCh3dBmV     REAL, \
    maxuCh3dBmV     REAL, \
    minuCh4dBmV     REAL, \
    maxuCh4dBmV     REAL \
); "
#define NEW_SQL_CREATE_TABLE_BOUNDS " \
CREATE TABLE bounds ( \
    Timestamp       INTEGER, \
    maxModemPing    REAL, \
    maxInetPing     REAL, \
    minDownChdBbmV  REAL, \
    maxDownChdBbmV  REAL, \
    minDownChdB     REAL, \
    maxDownChdB     REAL, \
    minUpChdBmV     REAL, \
    maxUpChdBmV     REAL, \
); "

#define SQL_DELETE_BY_TIMESTAMP " \
DELETE FROM data WHERE Timestamp = @Timestamp"

#define SQL_DELETE_ALL " \
DELETE FROM data"

#define SQL_INSERT " \
INSERT INTO data ( \
                 Timestamp, \
                 ModemPing, \
                 InetPing, \
                 dCh1dBbmV, \
                 dCh1dB, \
                 dCh2dBbmV, \
                 dCh2dB, \
                 dCh3dBbmV, \
                 dCh3dB, \
                 dCh4dBbmV, \
                 dCh4dB, \
                 dCh5dBbmV, \
                 dCh5dB, \
                 dCh6dBbmV, \
                 dCh6dB, \
                 dCh7dBbmV, \
                 dCh7dB, \
                 dCh8dBbmV, \
                 dCh8dB, \
                 uCh1dBmV, \
                 uCh2dBmV, \
                 uCh3dBmV, \
                 uCh4dBmV \
                 ) \
VALUES           ( \
                 @Timestamp, \
                 @ModemPing, \
                 @InetPing, \
                 @dCh1dBbmV, \
                 @dCh1dB, \
                 @dCh2dBbmV, \
                 @dCh2dB, \
                 @dCh3dBbmV, \
                 @dCh3dB, \
                 @dCh4dBbmV, \
                 @dCh4dB, \
                 @dCh5dBbmV, \
                 @dCh5dB, \
                 @dCh6dBbmV, \
                 @dCh6dB, \
                 @dCh7dBbmV, \
                 @dCh7dB, \
                 @dCh8dBbmV, \
                 @dCh8dB, \
                 @uCh1dBmV, \
                 @uCh2dBmV, \
                 @uCh3dBmV, \
                 @uCh4dBmV \
                 )"

#define SQL_INSERT_BOUNDS " \
CREATE TABLE bounds ( \
                    Timestamp, \
                    maxModemPing, \
                    maxInetPing, \
                    mindCh1dBbmV, \
                    maxdCh1dBbmV, \
                    mindCh1dB, \
                    maxdCh1dB, \
                    mindCh2dBbmV, \
                    maxdCh2dBbmV, \
                    mindCh2dB, \
                    maxdCh2dB, \
                    mindCh3dBbmV, \
                    maxdCh3dBbmV, \
                    mindCh3dB, \
                    maxdCh3dB, \
                    mindCh4dBbmV, \
                    maxdCh4dBbmV, \
                    mindCh4dB, \
                    maxdCh4dB, \
                    mindCh5dBbmV, \
                    maxdCh5dBbmV, \
                    mindCh5dB, \
                    maxdCh5dB, \
                    mindCh6dBbmV, \
                    maxdCh6dBbmV, \
                    mindCh6dB, \
                    maxdCh6dB, \
                    mindCh7dBbmV, \
                    maxdCh7dBbmV, \
                    mindCh7dB, \
                    maxdCh7dB, \
                    mindCh8dBbmV, \
                    maxdCh8dBbmV, \
                    mindCh8dB, \
                    maxdCh8dB, \
                    minuCh1dBmV, \
                    maxuCh1dBmV, \
                    minuCh2dBmV, \
                    maxuCh2dBmV, \
                    minuCh3dBmV, \
                    maxuCh3dBmV, \
                    minuCh4dBmV, \
                    maxuCh4dBmV \
                    ) \
VALUES              ( \
                    );"
#define NEW_SQL_INSERT_BOUNDS " \
INSERT INTO bounds  ( \
                    Timestamp, \
                    maxModemPing, \
                    maxInetPing, \
                    minDownChdBbmV, \
                    maxDownChdBbmV, \
                    minDownChdB, \
                    maxDownChdB, \
                    minUpChdBmV, \
                    maxUpChdBmV \
                    ) \
VALUES              ( \
                    strftime('%s','now'), \
                    200, \
                    500, \
                    3.0, \
                    8.0, \
                    38.0, \
                    50.0, \
                    38.0, \
                    50.0 \
                    );"
/*
 * These custom bounds from my own experiences
 *
 *      Downstream Power: (-5 dBmV to 5 dBmV)
 *          You generally want between -12dB and +12dB.
 *          Most modems are rated from -15dB to +15dB.
 *          Anything less or more than +/-15dB and you may have quality issues.
 *      Personal Experience: anything less than 3.0 and problems seem to manifest, high values of 7.0+ not really seen and high power doesn't seem to coinside with network trouble
 *
 *      Downstream SNR: (32 dB to 50 dB)
 *          This value is best to be kept over 30dB,
 *          but you may not have any problems with down to 25dB.
 *          Anything less than 25dB and you will probably have slow transfers,
 *          dropped connections, etc.
 *      Persona Experience: Low values do not seem to coinside with trouble, but values of 47.0+ are usually problematic and modem always resets before it hits 50.0.
 *
 *      Upstream Power: (32 dBmV to 50 dBmV)
 *      Personal Experience: generally very uniformly between 40.0 - 41.0. No relation to problems found - become 0.0 when issues arise.
 */
 
/* EOF database.h */

