/*
 * ttyinput.c
 *
 *      Trying to create code that would gracefully accept one character input
 *      is generally diffult. This is not because of C library calls, but
 *      rather due to the behaviour of the terminal.
 *
 *      Default operation is to buffer user input until '\n' or EOF before
 *      delivering it to the program. Along with echoe, achieving clean
 *      looking one keypress input routine is all but impossible.
 *
 *      ...without modifying the terminal features.
 *
 *      This depends on your OS, if you are in a UNIX like environment the
 *      ICANON flag is enabled by default, so input is buffered until the next
 *      '\n' or EOF. By disabling the canonical mode you will get the
 *      characters immediately. This is also possible on other platforms, but
 *      there is no straight forward cross-platform solution.
 *
 */
#include <stdio.h>
#include <stdbool.h>        /* true, false                          */
#include <termios.h>        /* termios, TCSANOW, ECHO, ICANON       */
#include <unistd.h>         /* STDIN_FILENO                         */

/*
 * ttyprompt()
 *
 *      Display given prompt and except Y/N reply.
 *
 *  RETURN
 *      1       If answered 'Y'
 *      0       If answered 'N'
 */
int ttyprompt(const char *prompt)
{
    int chr;
    static struct termios oldt, newt;

    /*
     * Display prompt
     */
    fprintf(stderr, "%s", prompt);

    /*
     * tcgetattr() gets the parameters of the current terminal
     * STDIN_FILENO will tell tcgetattr() that it should write
     * the settings of stdin to oldt
     */
    tcgetattr(STDIN_FILENO, &oldt);
    /* Make a copy of the original settings */
    newt = oldt;

    /*
     * ICANON normally takes care that one line at a time will
     * be processed that means it will return if it sees a "\n"
     * or an EOF or an EOL.
     *
     * Turning off ECHO flag prevents the terminal from echoing
     * the input is immediately back.
     */
    newt.c_lflag &= ~(ICANON | ECHO);

    /*
     * Those new settings will be set to STDIN TCSANOW tells
     * tcsetattr() to change attributes immediately.
     */
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    /*
     * This is your part:
     * I choose 'e' to end input. Notice that EOF is also
     * turned off in the non-canonical mode
     */
    for(;;)
    {
        chr = getchar();
        if (chr == 'y' || chr == 'Y' || chr == 'n' || chr == 'N')
            break;
    }

    /*
     * Restore the old settings
     */
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

    fprintf(stderr, "%c\n", chr);
    if (chr == 'y' || chr == 'Y')
        return(true);
    return(false);
}

/* EOF */
