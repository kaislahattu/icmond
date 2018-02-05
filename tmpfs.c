/*
 * tmpfs.c - 2016 Jani Tammi <janitammi@gmail.com>
 */
#include <stdio.h>
#include <stdint.h>         // uint64_t
#include <unistd.h>		    // access(), euidaccess()
#include <stdlib.h>         // calloc(), realpath()
#include <stdbool.h>        // true, false
#include <ctype.h>          // isspace()
#include <string.h>         // memset()
#include <math.h>
#include <malloc.h>
#include <errno.h>
#include <sys/mount.h>

#include "tmpfs.h"
#include "logwrite.h"
#include "util.h"
#include "user.h"
//#include "database.h"

#define _PROC_PATH_MOUNTINFO	"/proc/self/mountinfo"
#define _PROC_LINEWIDTH         2048

/*
 * Check if "path" is already mounted
 *
 *      Requires /proc filesystem AND canocalized path.
 *      Assumes sequential file (since kernel 2.6.x something).
 *
 *  RETURN
 *          1   errno = 0   path listed as a mountpoint
 *          0   errno = 0   parth not listed as a mountpoint
 *         -1   EINVAL      provided argument was NULL or pointer to NULL
 *         -1   errno != 0  fopen() error
 */
static int ismountpoint(const char *path)
{
    FILE *fp;
    int rc = 0;        // return code

    if (!path || !*path)
        return (errno = EINVAL, -1);

    if (!(fp = fopen(_PROC_PATH_MOUNTINFO, "r")))
        return -1;  // errno set by fopen()

    ssize_t  chars_in_line;
    size_t   buffer_size = 0;   // mandatory value for getline()
    char    *line = NULL;       // getline() allocated buffer
    char    *sptr;              // parsing pointer "start"
    char    *eptr;              // parsing pointer "end"
    while ((chars_in_line = getline(&line, &buffer_size, fp)) > 0)
    {
        // Skip four values from the beginning of the line
        int skip_count = 4;
        sptr = line;
        for (; skip_count; skip_count--)
        {
            while (!isspace(*sptr))
                sptr++;
            while (isspace(*sptr))
                sptr++;
        }
        // Null terminate this value
        eptr = sptr;
        while (!isspace(*eptr))
            eptr++;
        *eptr = '\0';

        // specified path listed as mount point?
        if (!strcmp(path, sptr))
        {
            rc = 1;
            break;
        }
    }
    free(line);
    fclose(fp);
    return (errno = 0, rc);
}


/*
 * unmount tmpfs
 *
 *
 * RETURN
 */
int tmpfs_umount(const char *path)
{
#pragma message "TO BE IMPLEMENTED"
    logerr("NOT YET IMPLEMENTED!");
    return EXIT_FAILURE;
}

/*
 *
 * RETURN
 *      EXIT_SUCCES     tmpfs mounted successfully
 *      EXIT_FAILURE    an error occured. errno is set.
 *
 * ERRNO from this function
 *      EINVAL          mountpoint was NULL or pointed to empty string,
 *                      mbytes value < 1 or > 256
 *      EBUSY           Specified mountpoint has something already on it
 */
int tmpfs_mount(const char *mountpoint, int mbytes)
{
    char data[64];

    if (!mountpoint || !*mountpoint || mbytes < 1 || mbytes > 256)
        return (errno = EINVAL, EXIT_FAILURE);

    // Create mountpoint directory
    if (mkdir_recursive(mountpoint))   // util.c
    {
        int savederrno = errno;
        logerr("Failed to create mountpoint directory \"%s\"", mountpoint);
        return (errno = savederrno, -1);
    }

    // Check if already mounted
    int rc;
    if ((rc = ismountpoint(mountpoint) < 0))
    {
        return EXIT_FAILURE;  // errno as set by mountpoint()
    }
    else if (rc > 0)
    {
        logdev("Mountpoint \"%s\" has something already mounted on it!", mountpoint);
        return (errno = EBUSY, EXIT_FAILURE);
    }
    else
    {
        // parse data string
        sprintf(
               data,
               "mode=0775,size=%dM,uid=%d,gid=%d",
               mbytes,
               user_get_uid(DAEMON_RUN_AS_USER),
               user_get_gid(DAEMON_RUN_AS_USER)
               );
        /*
         * OK - "path" is not mountpoint (nothing mounted on it, yet)
         * sudo mount -t tmpfs -o size=4M icmond.tmpfs /tmp/icmond.tmpfs
         * const char* opts = "mode=0700,uid=65534";   // 65534 is the uid of nobody
         */
        if (mount(
                 TMPFSDB_SOURCENAME,            // const char *source, Displayed in df
                 mountpoint,                    // const char *target, mountpoint
                 "tmpfs",                       // const char *filesystemtype,
                 0,                             // unsigned long mountflags,
                 data                           // const void *data
                 ))
        {
            int savederrno = errno;
            logdev(
                  "mount(\"%s\", \"%s\", \"tmpfs\", 0, \"%s\") failed!",
                  TMPFSDB_SOURCENAME, mountpoint, data
                  );
            return (errno = savederrno, EXIT_FAILURE);
        }
    }
    return (errno = 0, EXIT_SUCCESS);
}


/* EOF tmpfs.c */