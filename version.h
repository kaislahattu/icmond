/*
 * version.h
 */
#ifndef __VERSION_H__
#define __VERSION_H__

#define X(x) #x
#define TOSTR(x) X(x)

#define BUILD_MAJOR    0
#define BUILD_MINOR    7

#define GNUC_VERSION            TOSTR(__GNUC__) "." \
                                TOSTR(__GNUC_MINOR__) "." \
                                TOSTR(__GNUC_PATCHLEVEL__)

//#pragma message "TODO : dynamic compile time date and version numbering"
#define DAEMON_VERSION          TOSTR(BUILD_MAJOR)"."TOSTR(BUILD_MINOR)
#define DAEMON_BUILD            "Build date: "__DATE__" "__TIME__", gcc: "GNUC_VERSION

/*
 * This will exist in version.c,
 * which is rewritten by Makefile
 */
extern const char *daemon_build;

#endif /* __VERSION_H__ */
