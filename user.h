/*
 * user.h - 2016 Jani Tammi <janitammi@gmail.com>
 *
 *      icmond specific functions for access and privilege drop related needs.
 *      See also capability.c for additional access and privilege related stuff.
 */
#include "config.h"
#include "logwrite.h"


#ifndef __USER_H__
#define __USER_H__

/*
 * user_changeto()
 *
 *      Permanently drop privileges and assume the specified user rights.
 *      Obviously cannot be executed unless root.
 */
int     user_changeto(const char *);
/*
 * user_set_eugid()
 *
 *      Set effective user (and group) ID as the specified user.
 *      Requires root privileges.
 */
int     user_set_eugid(const char *);
/*
 * user_get_ename()
 *
 *      Return the user name of effective user ID.
 */
char *  user_get_ename();
/*
 * user_get_uid()
 *
 *      Return the UID by given username.
 */
uid_t   user_get_uid(const char *);
/*
 * user_get_gid()
 *
 *      Return the GID by given username.
 */
uid_t   user_get_gid(const char *);
/*
 * user_restore_eugid()
 *
 *      Restores effective user ID (and group ID) to real ID's.
 *      Real ID needs to be root in order to work.
 */
int	    user_restore_eugid();
/*
 * user_idreport()
 *
 *      Displays basic ID values (except saved or FS ID's).
 *      Can be used to log this information to syslog,
 *      unlike before user_proc_ersugid().
 */
char *  user_idreport();
/*
 * user_show_proc_ersugid()
 *
 *      Uses printf() to show two lines from /proc containing
 *      real, effective, saved and filesystem ID's (group and user).
 *      SHOULD NOT BE USED UNLESS AS DEVELOPMENT TOOL!
 */
void    user_show_proc_ersugid();

#endif /* __USER_H__ */

/* EOF user.h */
