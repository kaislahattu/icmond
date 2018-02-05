/*
 * user.c - temporary and permanent privilege drops
 *
 * icmond daemon is started up with some user account which may be even root.
 * Before the daemon settles down to basic execution (waking up periodically)
 * it will assume indicated user ID (default: "daemon") and  permanently drop
 * other privileges.
 *
 * During the start up process, there is need to determine file access for
 * the daemon user, prior to actually dropping the privileges.
 *
 * This implementation contains the necessary functions to execute temporary
 * privilege drops and restorations as well as permanent privilege drop.
 *
 * https://www.safaribooksonline.com/library/view/secure-programming-cookbook/0596003943/ch01s03.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>     // memset()
#include <limits.h>     // LOGIN_NAME_MAX
#include <pwd.h>        // getpwnam()
#include <grp.h>        // setgroups()
#include <errno.h>
#include <sys/param.h>
#include <sys/types.h>

#include "user.h"


/*
 * Public functions within this module will always invoke
 * __get_user_pwd() function to retrieve specified user data
 * before proceeding.
 *
 * This may seem wasteful, but it offers:
 * - cleaner syntax in public functions
 * - flexibility beyond earlier implementation where
 *   storing retrieved information effectively prevented
 *   checking for other users (although not needed by icmond).
 * - better malloc()/free() control.
 */
typedef struct
{
    struct passwd pwd;
    char          *strings;
    size_t        strings_size;
} userpwd_t;
static userpwd_t *userpwd = NULL;

/*
 * __get_user_pwd()  - internal use only
 *
 *
 *
 *
 */
static userpwd_t *__get_user_pwd(const char *uname)
{
    struct passwd pwd;
    struct passwd *pwdptr;
    char          *strings;
    size_t        strings_size;
    int           errorcode;

    /*
     * Free malloc()'ed memory
     */
    if (userpwd)
    {
        free(userpwd->strings);
        free(userpwd);
        userpwd = NULL;
    }

    /*
     * Size and create strings buffer
     */
    strings_size = sysconf(_SC_GETPW_R_SIZE_MAX);
    /* If value was indeterminate */
    if (strings_size == -1)
        strings_size = 16384;        /* Should be more than enough */
    strings = malloc(strings_size);
    if (strings == NULL)
    {
        logerr("malloc() failure for strings buffer!");
        exit(EXIT_FAILURE);
    }

    /*
     * Verify that the user exists
     */
    errorcode = getpwnam_r(uname, &pwd, strings, strings_size, &pwdptr);
    if (pwdptr == NULL)
    {
        if (errorcode == 0)
        {
            return(NULL);
        }
        else
        {
            errno = errorcode;
            logerr("getpwdnam_r(\"%s\") failure!", uname);
            exit(EXIT_FAILURE);
        }
    }

    /*
     * Only now allocate the space for the structure.
     * This way the pointer remains NULL, if any errors occure
     * and we avoid any potential confusion as to if the user
     * password data was retrieved or not.
     */
    if (!(userpwd = malloc(sizeof(userpwd_t))))
    {
        logerr("malloc() failure for userpwd!");
        exit(EXIT_FAILURE);
    }
    /*
     * compiler should recognize similar structures and write
     * the necessary shallow-copy operation here...
     * Shallow-copy is allowed since the pointers in there all
     * point to the strings -buffer we already ourselves created.
     */
    userpwd->pwd          = pwd;
/* ALTERNATIVE (if the above does not work)
    memcpy(&(daemonpwd->pwd), &pwd, sizeof(passwd));
*/
    userpwd->strings      = strings;
    userpwd->strings_size = strings_size;

    return(userpwd);
}

/*
 * Set effective user and group ID's
 *
 *
 *
 *
 */
int user_set_eugid(const char *username)
{
    userpwd_t *userpwd;

    if (!(userpwd = __get_user_pwd(username)))
    {
        logerr("User (\"%s\") does not exist!");
        return(EXIT_FAILURE);
    }
/*
    logdev	(
		"Changing effective user as \"%s\", UID: %d GID: %d\n",
		userpwd->pwd.pw_name,
		userpwd->pwd.pw_uid,
		userpwd->pwd.pw_gid
		);
*/
    /*
     * Set effective GID
     *
     * Funny, if you first change effective UID, it will prevent you
     * from setting effective GID (EPERM - lack of privileges)
     */
    if (setregid(-1, userpwd->pwd.pw_gid))
    {
        logerr("Unable to set effective GID!");
        return(EXIT_FAILURE);
    }

    /*
     * Set effective UID
     */
    if (setreuid(-1, userpwd->pwd.pw_uid))
    {
        logerr("Unable to set effective UID!");
        /* restore EGID just in case */
        setregid(-1, getgid());
        return(EXIT_FAILURE);
    }

    return(EXIT_SUCCESS);
}

/*
 * Get effective username
 */
char *user_get_ename()
{
    static char  eusername[LOGIN_NAME_MAX];  /* LOGIN_NAME_MAX defined in limits.h */

    return(cuserid(eusername));
}

uid_t user_get_uid(const char *uname)
{
    userpwd_t *usr = __get_user_pwd(uname);
    return usr->pwd.pw_uid;
}

uid_t user_get_gid(const char *uname)
{
    userpwd_t *usr = __get_user_pwd(uname);
    return usr->pwd.pw_gid;
}

/*
 * Simply sets EUID as RUID and EGID and RGID
 *
 *
 *
 *
 */
int user_restore_eugid()
{
    int savederrno = 0;
    if (setreuid(-1, getuid()))
    {
        savederrno = errno;
        logmsg(LOG_ERR, "Unable to restore effective UID with real UID!");
        return (errno = savederrno, EXIT_FAILURE);
    }

    if (setregid(-1, getgid()))
    {
        savederrno = errno;
        logmsg(LOG_ERR, "Unable to restore effective GID with real GID!");
        return (errno = savederrno, EXIT_FAILURE);
    }

    return (errno = 0, EXIT_SUCCESS);
}

/*
 * Report real and effective UID and GID's
 * Caller is provided with char * to parsed report.
 * Used exclusively for debugging / informational
 * purposes.
 *
 */
char *user_idreport()
{
    /*
     * Static, because strings need to stay intact after exiting this function
     */
    static char  msgstring[1024];
    static char  eusername[LOGIN_NAME_MAX];  /* LOGIN_NAME_MAX defined in limits.h */

    /*
     * Try getting effective user name.
     * Calling without buffer is no good. The below sprintf()
     * will use same static storage to format it's frmt string
     * which will overwrite our effective username.
     */
    /* cuserid(eusername)); */
    /*
     * cuserid() does not seem to feture any facilities for errors?
     */
    sprintf(
           msgstring,
           "Effective \"%s\" (UID: %d GID: %d) "
           "Real \"%s\" (UID: %d GID: %d)",
           cuserid(eusername),
           geteuid(),
           getegid(),
           getlogin(),
           getuid(),
           getgid()
           );
    return(msgstring);
}

/* This should be used only for development purposes...
 */
void user_show_proc_ersugid()
{
    static FILE *fp = NULL;
    static int myPid = 0;
    static char file[1024];
    char line[1024];
    if (!myPid)
    {
        myPid = getpid();
        sprintf(file, "/proc/%d/task/%d/status", myPid, myPid);
    }

    if (!fp)
    {
        if ((fp = fopen(file, "r")) == NULL)
        {
            perror("fopen() failed!");
            return;
        }
    }
    /* Rewind */
    printf("\tReal\tEffect\tSaved\tFS\n");
    fseek(fp, 0, SEEK_SET);
    while (fgets(line, 1024, fp))
    {
        if (strncmp(line, "Uid:", 4) == 0 ||
            strncmp(line, "Gid:", 4) == 0)
        {
            printf("%s",line);
        }
    }
}

/*
 * drop privileges permanently
 *
 * https://www.safaribooksonline.com/library/view/secure-programming-cookbook/0596003943/ch01s03.html
 *
 * setuid(2) man page:
 *   "The setuid() function checks the effective user ID of the caller
 *   and if it is the superuser, all process-related user ID's are set
 *   to the argument uid.
 *   After this has occurred, it is impossible for the program
 *   to regain root privileges."
 * => This is the call to drop privileges (set EUID, RUID, SUID).
 */
int user_changeto(const char *username)
{
    userpwd_t *userpwd;

    /*
     * We can do this ONLY if this process has been
     * executed as root. If not, issue warning and
     * return without doing anything. (see else -part)
     */
    if (getuid() == 0 || geteuid() == 0)
    {

        /*
         * Retrieve record for the specified username
         */
        if (!(userpwd = __get_user_pwd(username)))
        {
            logerr("User (\"%s\") does not exist!");
            return(EXIT_FAILURE);
        }

        /* If root privileges are to be dropped, be sure to pare down the ancillary
         * groups for the process before doing anything else because the
         * setgroups() system call requires root privileges. Drop ancillary groups
         * regardless of whether privileges are being dropped temporarily or
         * permanently.
         */
        setgroups(1, &(userpwd->pwd.pw_gid)); /* of type pw_gid */

        /*
         * Remember to set the groups PRIOR to changing the UID.
         */
        if (setregid(userpwd->pwd.pw_gid, userpwd->pwd.pw_gid))
        {
            logerr("Unable to set group \"%s\"!", userpwd->pwd.pw_gid);
            return(EXIT_FAILURE);
        }

        /*
         * Finally, let setuid() blanket-change all; EUID, RUID, SUID (+FSUID)
         */
        if (setuid(userpwd->pwd.pw_uid))
        {
            logerr("Unable to set user \"%s\"!", DAEMON_RUN_AS_USER);
            return(EXIT_FAILURE);
        }

        /*
         * Verify that the changes were successful
         */
        /*
         *  ...yeah, I WOULD, if I could implement the __get_uids()...
         */

    }
    else /* not run as root */
    {
        /*
         * This is allowed operation and thus we return with success status
         */
        logmsg(LOG_WARNING, "Not running as root! Cannot change into user \"%s\"!", DAEMON_RUN_AS_USER);
        return(EXIT_SUCCESS);
    }

    return(EXIT_SUCCESS);
}

/* EOF privilege.c */
