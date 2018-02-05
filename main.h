/*
 * main.h - 2016 Jani Tammi <janitammi@gmail.com>
 */

/*
 * EXIT_SUCCESS (0)
 * EXIT_FAILURE (1)
 * ...are defined in stdlib.h
 *
 * We will define a macro for cancelled action (not an error and not a success)
 * Will be used by cmd_* functions that can prompt confirmation from the user.
 * If user decides to cancel, it would be stupid to report an "error" - thus we
 * need to know when the action was cancelled, as opposed to encountering a real
 * error.
 */
#define EXIT_CANCELLED  2
/* EOF */
