/*
 * power.h - 2016 Jani Tammi <janitammi@gmail.com>
 *
 *      This module will, some day, provide the means to control WiFi/USB/other
 *      power plug. I don't actually have one yet, so this is as of yet
 *      completely open.
 */
#ifndef __POWER_H__
#define __POWER_H__

/*
 * ON/OFF functions
 *
 *  RETURN
 *      EXIT_SUCCESS | EXIT_FAILURE
 */
int	power_on();
int	power_off();

/*
 * Get power state.
 *
 *  RETURN
 *      true        if power is on
 *      false       if power is off
 */
int power_state();

#endif /* __POWER_H__ */
/* EOF */
