/*
 * tmpfsdb.h - 2016 Jani Tammi <janitammi@gmail.com>
 *
 *  tmpfs DB, a solution to work around substandard SD cards that take
 *  too much time to write data.
 *
 *  Usage:
 *      -ramdisk            forced tmpfs
 *      -ramdisk=false      no tmpfs
 *      -ramdisk=true       forced tmpfs
 *      -ramdisk=auto       tested and decided by program
 */
#ifndef __TMPFSDB_H__
#define __TMPFSDB_H__

#include <time.h>

#include "config.h"

/*
 * If the defined values below are exceeded, tmpfs will be created
 */
#define TMPFSDB_NUMBER_OF_TEST_INSERTS          5
#define TMPFSDB_INSERT_MAX_MEAN                 200
#define TMPFSDB_INSERT_MAX_MAX                  600

#define TMPFSDB_SOURCENAME                      "icmond.tmpfs"                  // Visible in "df" for example

/*
 * Tests SQLite3 write performance and sets up tmpfs database
 * if performance results are poor. In addition, will create
 * and event that periodically moves accumulated data from the
 * tmpfs to actual databasefile.
 *
 * RETURN
 *      1       tmpfs was needed and created
 *      0       OK, no tmpfs created
 *     -1       An error occured. errno set.
 */
int     tmpfs_mount(const char *mountpoint, int mbytes);
int     tmpfs_umount(const char *mountpoint);


#endif /* __TMPFSDB_H__ */

/* EOF tmpfsdb.h */
