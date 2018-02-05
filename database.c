/*
 * database.c - 2016 Jani Tammi <janitammi@gmail.com>
 */
#include <stdio.h>
#include <unistd.h>             // access()
#include <time.h>               // time_t
#include <math.h>
#include <stdlib.h>             // exit()
#include <errno.h>              // errno
#include <sqlite3.h>

#include "database.h"
#include "config.h"
#include "logwrite.h"
#include "util.h"

/*
 * Event command handler for EVENT_CMD_COLLECTTMPFS
 *
 *      Function will access the database file in tmpfs and read
 *      all accumulated data, insert it into the actual database
 *      file and finally, when successful, deletes the records that
 *      were moved.
 *
 */
int database_collecttmpfs(time_t olderthan)
{
    if (!cfg.execute.tmpfs)
    {
        logerr("tmpfs not in use! This should not have been called!");
        return EXIT_FAILURE;
    }
    logerr("NOT IMPLEMENTED!");
    return EXIT_FAILURE;
}

/*
 * test SQLite3 write performance
 *
 *      Test is made against "standard" datafile (cfg.database.filename).
 *      Function returns dbwriteperf_t pointer which is allocated from heap.
 *      Caller is responsible for releasing it when no longer needed.
 */
dbperf_t *database_testwriteperf(int nsamples)
{
    databaserecord_t dbrec;
    static dbperf_t  dbperf;
    // Other values in structure are unimportant
    dbrec.timestamp = 0;       // Delete operation will match this value
    // setup dbperf
    dbperf.n        = 0;
    dbperf.min      = DBL_MAX;
    dbperf.mean     = 0.0L;
    dbperf.max      = 0.0L;
    dbperf.stddev   = 0.0L;
    double tnow;                    // time it took now
    // for Welford's method
    double M2       = 0.0L;
    double delta;
    
    xtmr_t *t = xtmr();             // Timer
    int rc;                         // return value
    do
    {
        xtmrlap(t);
        if ((rc = database_insert(cfg.database.filename, &dbrec)))
        {
            logerr("\ndatabase_insert() failed!");
            return NULL;
        }
        else
        {
            tnow            = xtmrlap(t);
            dbperf.min      = tnow < dbperf.min ? tnow : dbperf.min;
            dbperf.max      = tnow > dbperf.max ? tnow : dbperf.max;
            // Welford's method for standard deviation
            dbperf.n++;
            delta           = tnow - dbperf.mean;
            dbperf.mean    += delta / dbperf.n;
            M2             += delta * (tnow - dbperf.mean);
            dbperf.stddev   = sqrt(M2 / (dbperf.n - 1));
        }
    } while (--nsamples);
    free(t);

    // Remove test rows
    database_delete(dbrec.timestamp);
    return &dbperf;
}


/*
 * Create database file
 *
 *	Caller must quarantee that the database file DOES NOT
 *      EXIST prior to entering this function.
 *
 *      ALSO, this function is intended to be called with the
 *      effective user privileges set to that of the user which
 *      will be used in daemon mode.
 */
int database_initialize(char *filename)
{
    int      rc;   // return code from the sqlite3_* functions
    sqlite3 *db;
    char    *errMsg = 0;

    /*
     * Open & crete database
     */
    if ((rc = sqlite3_open_v2(
                             filename,
                             &db,
                             SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE,
                             NULL
                             )) != SQLITE_OK)
    {
        logerr("Can't open database (\"%s\"): %s\n", filename, sqlite3_errmsg(db));
        return rc;
    }

    /*
     * Create data table
     */
    if ((rc = sqlite3_exec(
                          db,
                          SQL_CREATE_TABLE_DATA,
                          (void *)0,
                          0,
                          &errMsg
                          )) != SQLITE_OK)
    {
        logerr("SQL error: %s\n", errMsg);
        sqlite3_free(errMsg);
        return rc;
    }

    /*
     * Create bounds table
     */
    if ((rc = sqlite3_exec(
                          db,
                          SQL_CREATE_TABLE_BOUNDS,
                          (void *)0,
                          0,
                          &errMsg)) != SQLITE_OK)
    {
        logerr("SQL error: %s\n", errMsg);
        sqlite3_free(errMsg);
        return rc;
    }

    sqlite3_close(db);
    // SQLite3 functions persistently set errno values even without errors.
    // They are then automatically picked up by my logwrite routines.
    // This disturbs my little happy world, and thus I simply zero errno now.
    errno = 0;
    return EXIT_SUCCESS;
}

int database_delete(int timestamp)
{
    int           rc;
    sqlite3      *db;
    sqlite3_stmt *stmt;
    char         *sqlstr;

    if ((rc = sqlite3_open(cfg.database.filename, &db)) != SQLITE_OK)
    {
        logerr("Can't open database: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return rc;
    }

    if ((rc = sqlite3_busy_timeout(db, DATABASE_SQLITE3_BUSY_TIMEOUT)) != SQLITE_OK)
    {
        logerr("Unable to set timeout: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return rc;
    }

    if (timestamp < 0)
        sqlstr = SQL_DELETE_ALL;
    else
        sqlstr = SQL_DELETE_BY_TIMESTAMP;

    if ((rc = sqlite3_prepare_v2(
                                db,            // Database handle
                                sqlstr,        // SQL statement, UTF-8 encoded
                                -1,            // Maximum length of zSql in bytes. (-1 = read until null termination)
                                &stmt,         // OUT: Statement handle
                                NULL           // OUT: Pointer to unused portion of zSql
                                )) != SQLITE_OK)
    {
        logerr("Unable to prepare DELETE SQL: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return rc;
    }

    if (timestamp >= 0)
    {
        sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, "@Timestamp"), timestamp);
    }

    // Execute statement
	if ((rc = sqlite3_step(stmt)) != SQLITE_DONE)
    {
		logerr("Delete statement dod not return with SQLITE_DONE: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return rc;
	}

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return EXIT_SUCCESS;
}

/*
 * Insert record
 *
 * 1. bind variables to statement
 * 2. open cfg->db_filename
 * 3. execute statement
 * 4. close database
 *
 * RETURN
 *      SQLITE_OK       Success
 *      *               Return code received from failed sqlite3_ -function
 */
int database_insert(char *filename, databaserecord_t *rec)
{
    int           rc;   // return code from the sqlite3_* functions
    sqlite3      *db;
    sqlite3_stmt *stmt;
//    xtmr_t *t = xtmr();

    /*
     * "Whether or not an error occurs when it is opened, resources associated with
     *  the database connection handle should be released."
     */
    if ((rc = sqlite3_open(filename, &db)) != SQLITE_OK)
    {
        logerr("Can't open database \"%s\": %s", filename, sqlite3_errmsg(db));
        sqlite3_close(db);
        return rc;
    }
//    logdev("sqlite3_open() : %5.2f ms", xtmrlap(t));

    /*
     * Set busy timeout
     *
     * sqlite3_busy_timeout() sets a busy handler that sleeps for a specified
     * amount of time when a table is locked. The handler will sleep multiple
     * times until at least "ms" milliseconds of sleeping have accumulated.
     *
     * After at least "ms" milliseconds of sleeping, the handler returns 0
     * which causes sqlite3_step() to return SQLITE_BUSY.
     */
    if ((rc = sqlite3_busy_timeout(db, DATABASE_SQLITE3_BUSY_TIMEOUT)) != SQLITE_OK)
    {
        logerr("Unable to set timeout: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return rc;
    }
//    logdev("sqlite3_busy_timeout() : %5.2f ms", xtmrlap(t));

    // Something in this generates errno(2) "No such file or directory"
    if ((rc = sqlite3_prepare_v2(
                                db,            // Database handle
                                SQL_INSERT,    // SQL statement, UTF-8 encoded
                                -1,            // Maximum length of zSql in bytes. (-1 = read until null termination)
                                &stmt,         // OUT: Statement handle
                                NULL           // OUT: Pointer to unused portion of zSql
                                )) != SQLITE_OK)
    {
        logerr("Unable to prepare INSERT SQL: %s", sqlite3_errmsg(db));
        logerr("Statement: %s", SQL_INSERT);
        sqlite3_close(db);
        return rc;
    }
    // clear errno
//    errno = 0;
//    logdev("sqlite3_prepare_v2() : %5.2f ms", xtmrlap(t));

#define BINDDOUBLE(s, v) \
    ({ \
    if ((v) == DATABASE_DOUBLE_NULL_VALUE) \
        sqlite3_bind_null(stmt, sqlite3_bind_parameter_index(stmt, (s))); \
    else \
        sqlite3_bind_double(stmt, sqlite3_bind_parameter_index(stmt, (s)), (v)); \
    }) 

    // I don't expect trouble with these...
    sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, "@Timestamp"), rec->timestamp);
    BINDDOUBLE("@ModemPing", rec->modemping_ms);
    BINDDOUBLE("@InetPing",  rec->inetping_ms);
    BINDDOUBLE("@dCh1dBbmV", rec->down_ch1_dbmv);
    BINDDOUBLE("@dCh1dB",    rec->down_ch1_db);
    BINDDOUBLE("@dCh2dBbmV", rec->down_ch2_dbmv);
    BINDDOUBLE("@dCh2dB",    rec->down_ch2_db);
    BINDDOUBLE("@dCh3dBbmV", rec->down_ch3_dbmv);
    BINDDOUBLE("@dCh3dB",    rec->down_ch3_db);
    BINDDOUBLE("@dCh4dBbmV", rec->down_ch4_dbmv);
    BINDDOUBLE("@dCh4dB",    rec->down_ch4_db);
    BINDDOUBLE("@dCh5dBbmV", rec->down_ch5_dbmv);
    BINDDOUBLE("@dCh5dB",    rec->down_ch5_db);
    BINDDOUBLE("@dCh6dBbmV", rec->down_ch6_dbmv);
    BINDDOUBLE("@dCh6dB",    rec->down_ch6_db);
    BINDDOUBLE("@dCh7dBbmV", rec->down_ch7_dbmv);
    BINDDOUBLE("@dCh7dB",    rec->down_ch7_db);
    BINDDOUBLE("@dCh8dBbmV", rec->down_ch8_dbmv);
    BINDDOUBLE("@dCh8dB",    rec->down_ch8_db);
    BINDDOUBLE("@uCh1dBmV",  rec->up_ch1_dbmv);
    BINDDOUBLE("@uCh2dBmV",  rec->up_ch2_dbmv);
    BINDDOUBLE("@uCh3dBmV",  rec->up_ch3_dbmv);
    BINDDOUBLE("@uCh4dBmV",  rec->up_ch4_dbmv);
//    logdev("sqlite3_bind_*() : %5.2f ms", xtmrlap(t));

    // Execute statement
    // This also generates errno(2) "No such file or directory"
	if ((rc = sqlite3_step(stmt)) != SQLITE_DONE)
    {
		logerr("Insert statement did not return with SQLITE_DONE: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return rc;
	}
    // Clear errno
//    errno = 0;
//    logdev("sqlite3_step() : %5.2f ms", xtmrlap(t));

    /*
     * Destroy a prepared statement object,
     * close database connection and
     * return with appropriate code
     */
    sqlite3_finalize(stmt);
    sqlite3_close(db);
//    logdev("sqlite3_finalize() and sqlite3_close() : %5.2f ms", xtmrlap(t));
//    free(t);

    return EXIT_SUCCESS;
}

void database_logdev(databaserecord_t *rec)
{
    if (!rec)
        logdev("Received NULL pointer!");

#define LOGDEV(n, v) \
    ({ \
    if ((v) == DATABASE_DOUBLE_NULL_VALUE) \
        logdev("%-30s : NULL\n", (n)); \
    else \
        logdev("%-30s : %4.1f\n", (n), (v)); \
    }) 

    logdev("databaserecord_t.timestamp     : %d\n",    (int)rec->timestamp);
    LOGDEV("databaserecord_t.modemping_ms",  rec->modemping_ms);
    LOGDEV("databaserecord_t.inetping_ms",   rec->inetping_ms);
    LOGDEV("databaserecord_t.down_ch1_dbmv", rec->down_ch1_dbmv);
    LOGDEV("databaserecord_t.down_ch1_db",   rec->down_ch1_db);
    LOGDEV("databaserecord_t.down_ch2_dbmv", rec->down_ch2_dbmv);
    LOGDEV("databaserecord_t.down_ch2_db",   rec->down_ch2_db);
    LOGDEV("databaserecord_t.down_ch3_dbmv", rec->down_ch3_dbmv);
    LOGDEV("databaserecord_t.down_ch3_db",   rec->down_ch3_db);
    LOGDEV("databaserecord_t.down_ch4_dbmv", rec->down_ch4_dbmv);
    LOGDEV("databaserecord_t.down_ch4_db",   rec->down_ch4_db);
    LOGDEV("databaserecord_t.down_ch5_dbmv", rec->down_ch5_dbmv);
    LOGDEV("databaserecord_t.down_ch5_db",   rec->down_ch5_db);
    LOGDEV("databaserecord_t.down_ch6_dbmv", rec->down_ch6_dbmv);
    LOGDEV("databaserecord_t.down_ch6_db",   rec->down_ch6_db);
    LOGDEV("databaserecord_t.down_ch7_dbmv", rec->down_ch7_dbmv);
    LOGDEV("databaserecord_t.down_ch7_db",   rec->down_ch7_db);
    LOGDEV("databaserecord_t.down_ch8_dbmv", rec->down_ch8_dbmv);
    LOGDEV("databaserecord_t.down_ch8_db",   rec->down_ch8_db);
    LOGDEV("databaserecord_t.up_ch1_dbmv",   rec->up_ch1_dbmv);
    LOGDEV("databaserecord_t.up_ch2_dbmv",   rec->up_ch2_dbmv);
    LOGDEV("databaserecord_t.up_ch3_dbmv",   rec->up_ch3_dbmv);
    LOGDEV("databaserecord_t.up_ch4_dbmv",   rec->up_ch4_dbmv);

}

/* EOF */

