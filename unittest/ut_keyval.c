/******************************************************************************
 * DEVELOPMENT AND TESTING CODE
 ******************************************************************************/
#include <stdio.h>
#include <string.h>

#include "../keyval.h"
#include "../util.h"

char **tststr = (char *[])
{
    "=",
    "=,",
    "=,,",
    "=,pop,",
    "pip=,,,pop",
    "=niks,,,naks",
    "=naks",
    "=poks,,,,",
    NULL
};
char *tstres[9][7] = 
{
    { "",    "",     NULL,  NULL, NULL,   NULL, NULL }, // "="
    { "",    "",     "",    NULL, NULL,   NULL, NULL }, // "=,"
    { "",    "",     "",    "",   NULL,   NULL, NULL }, // "=,,"
    { "",    "",     "pop", "",   NULL,   NULL, NULL }, // "=,pop,"
    { "pip", "",     "",    "",   "pop",  NULL, NULL }, // "pip=,,,pop"
    { "",    "niks", "",    "",   "naks", NULL, NULL }, // "=niks,,,naks"
    { "",    "naks", NULL,  NULL, NULL,   NULL, NULL }, // "=naks"
    { "",    "poks", "",    "",   "",     "",   NULL }, // "=poks,,,,"
    { NULL,  NULL,   NULL,  NULL, NULL,   NULL, NULL }
};

int resultOK(keyval_t kv, char **result)
{
    int i;
    // +1 because keyval_nvalues() does not count key
    if (keyval_nvalues(kv) + 1 != arrlen(result))
        return 0;
    for (i = 0; result[i]; i++)
    {
        if (strcmp(kv[i], result[i]))
            return 0;
    }
    return 1;
}
// TODO: bsprintf() parsing messages...
    /*
    "=,,",
    "# Comment line for your annoyance",
    "smb1= \\ \\\\\\\\tunkki\\\\srv # Windows share, prefixed by leading space (escaped). ",
    "That  =   my \\\"life\\;business\\\"; my problem; my responsibility    ",
    "goldkey = val1, val2\\=false, val3 \\= mursu marsu norsu, val4 # note the syntax!",
    "prompt = Please type in: \"key \\= val \\# important!\"",
    "Invalid\\=KeyVal String",
    "wish list = 32\\\" TV, \"8\\\" tablet\", \\#1 prize",
    "1473593429=3.5;44.5;0.0;0.0;0.0;0.0;0.0;0.0;0.0;0.0;0.0;0.0;0.0;0.0;0.0;0.0;41.2;0.0;0.0;0.0",
    */

int main(int argc, char *argv[])
{
    int       index;
    char *    description = NULL;
    keyval_t  kv;

    for (index = 0; tststr[index]; index++)
    {
        printf("====================================\n");
        printf("keyval_create(\"%s\"):\n", tststr[index]);
        kv = keyval_create(tststr[index]);
//        keyval_remove_empty_values(kv);
        printf("COMPARE %s\n", resultOK(kv, tstres[index]) ? "OK" : "NOT OK!");
        bsprint_keyval(&description, kv);
        printf("%s", description);
//        printf("%s\n", bsprint_keyval(&description, kv));
        printf("====================================\n");
        bsfree(&description);
    }

    return 0;
}

/* ut_keyval.c */
