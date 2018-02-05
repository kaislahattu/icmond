/*
 * daemon.h - 2016 Jani Tammi <janitammi@gmail.com>
 */

 #ifndef __DAEMON_H__
 #define __DAEMON_H__

/*
 * Entry to daemon's main loop
 */
void daemon_main();

/*
 * Functions to enter and exit suspended operation mode
 * Set and clear .state.suspended_by_schedule value, as
 * no other source than scheduled events will use this
 * API.
 */
int daemon_suspend();
int daemon_resume();

/*
 * Routine to monitor memory and CPU consumption (and others)
 */
int daemon_watchdog();
int daemon_importtmpfs();
int daemon_importtmpfstimeout();

#endif /* __DAEMON_H__ */

/* EOF daemon.h */
