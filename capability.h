/*
 * capability.h - 2016 Jani Tammi <janitammi@gmail.com>
 */
#include <sys/capability.h>
#include "logwrite.h"

#ifndef __CAPABILITY_H__
#define __CAPABILITY_H__

/*
 *
 */
void capability_set();

#ifdef _DEBUG
#define \
    capability_logdev() \
    ({ \
    cap_t process_capabilities; \
    if (!(process_capabilities = cap_get_proc())) \
        logerr("cap_get_proc()"); \
    logdev("Capabilities %s", cap_to_text(process_capabilities, NULL)); \
    cap_free(process_capabilities); \
    })
#else
#define capability_logdev() { }
#endif /* __DEBUG */

#endif /* __CAPABILITY_H__ */

/* EOF capability.h */
