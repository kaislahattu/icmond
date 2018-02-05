/*
 * capability.c - 2016 Jani Tammi <janitammi@gmail.com>
 *
 *      This code is very specific to icmond. If time allows, some level
 *      generality will be implemented.
 *
 *      Icmond changes UID/GID and fork()'s processes and each of those
 *      actions affect capabilities (generally dropping Effective and
 *      Inherit -flags drop).
 *
 *      Primary purpose of this module is to offer a function to restore
 *      capabilities to the intended order:
 *
 *      CAP_NET_RAW=epi
 *
 */
#include <stdlib.h>
#include <sys/prctl.h>                  // PR_CAPBSET_READ

#include "capability.h"
#include "logwrite.h"


/*
 *  capability_set() - reset capability flags after fork()
 *
 *      This function is intended for unprivileged use and cannot
 *      reintroduce capabilities that the process no longer has.
 *
 *      NOTE: This function is NOT intended for pre-UID/GID change.
 *            icmond does that just once during the starup, and that
 *            code will call prctl(PR_SET_KEEPCAPS, 1L, 0, 0); that
 *            one time before the user change.
 *            (main.c:daemonize() -function)
 */
void capability_set()
{
    //
    // Check necessary capabilities (IEEE 1003.1e style)
    // CAP_NET_RAW required by socket(), for ICMP echo
    //
    if (!prctl(PR_CAPBSET_READ, CAP_NET_RAW, 0, 0, 0))
    {
        // Because icmond can be invoked only as a root, this means that
        // the program code has failed to maintain capabilities and exit()
        // is fully justified recourse.
        logerr("Raw net socket capabilities missing!");
        exit(EXIT_FAILURE);
    }
    else
    {
        //
        // Drop all but required capabilities and raise the required to effective status.
        // (UID change sets effective status OFF even when the Permissible is retained!)
        //
        cap_t       capabilities;
        cap_value_t cap_list[2];
        int         cap_list_ncaps;
        // All initial flag values are "cleared"
        if (!(capabilities = cap_init()))
        {
            logerr("cap_init() failed");
            exit(EXIT_FAILURE);
        }
        // Setup flags that we need
        cap_list[0] = CAP_NET_RAW;      // for socket() (ICMP echo packets, aka. ping)
        //cap_list[1] = CAP_SETFCAP;
        cap_list_ncaps = 1;             // number of flags in the list, see below
        if (
            cap_set_flag(capabilities, CAP_PERMITTED,   cap_list_ncaps, cap_list, CAP_SET) == -1 ||
            cap_set_flag(capabilities, CAP_EFFECTIVE,   cap_list_ncaps, cap_list, CAP_SET) == -1 ||
            cap_set_flag(capabilities, CAP_INHERITABLE, cap_list_ncaps, cap_list, CAP_SET) == -1
            )
        {
            logerr("cap_set_flag() failure");
            exit(EXIT_FAILURE);
        }
        if (cap_set_proc(capabilities) == -1)
        {
            logerr("cap_set_proc() failure");
            exit(EXIT_FAILURE);
        }
        // free capabilities buffer
        cap_free(capabilities);
    }
}

/*
char *bsprint_capability(char **buffer)
{
    cap_t process_capabilities;
    if (!(process_capabilities = cap_get_proc()))
        logerr("cap_get_proc()");
    bsprintf(buffer, "Capabilities %s", cap_to_text(process_capabilities, NULL));
    cap_free(process_capabilities);
}
*/

/* EOF capability.c */