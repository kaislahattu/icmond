/*
 * power.c - 2016 Jani Tammi <janitammi@gmail.com>
 */
#include <stdio.h>
#include <stdbool.h>        // true, false
#include <stdlib.h>         // EXIT_SUCCESS, EXIT_FAILURE

#include "power.h"
#include "config.h"
#include "logwrite.h"

int	power_on()
{
    logdev("NOT IMPLEMENTED!");
    return EXIT_SUCCESS;
}

int	power_off()
{
    logdev("NOT IMPLEMENTED!");
    return EXIT_SUCCESS;
}

int power_state()
{
    logdev("NOT IMPLEMENTED!");
    return true;
}

/* EOF */
