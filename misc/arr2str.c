/* isdelim() DelimiterFunction for str2arr()
 * NOTE: first character in this list is used
 * as the delimiter character for parsing the
 * array back to list.
 */
#define CFG_LIST_DELIMITERS	",;|"
#define isdelim(c)		(int)strchr(CFG_LIST_DELIMITERS, (c))


/*
 * Internal data strucuture for key-value pair
 */
typedef struct {
	char *key;
	char *val;
} keyval_t;


/*
 * str2arr()
 *
 *	Returns a malloc'ed list of pointers to argument 1 (string pointer)
 *	using delimiter function.
 *
 *	Function makes only ONE malloc().
 *	Example of the memory arrangement:
 *
 *      | 0x0000 0000 |  prt to 0x0000 0010 |
 *      | 0x0000 0004 |  ptr to 0x0000 001b |
 *      | 0x0000 0008 |  ptr to 0x0000 0025 |
 *      | 0x0000 000c |  ptr to 0x0000 0000 | NULL POINTER, end of list
 *      | 0x0000 0010 |  "www.utu.fi\0"     |
 *      | 0x0000 001b |  "sonera.fi\0"      |
 *      | 0x0000 0025 |  "www.hs.fi\0"      | Final byte at 0x0000 002f
 *
 *	User doesn't need to worry about the internal organization.
 *	This method has been adopted so that the list can be free()'ed
 *	with single function call and there is no need to keep track of
 *	other pointers.
 *
 *  REAL LIFE EXAMPLE
 *
 *  char **arr = str2arr("www.utu.fi,sonera.fi,www.hs.fi");
 *  dump_mem(arr, malloc_usable_size(arr));
 *
 * 0876 6040  50 60 76 08 5B 60 76 08 65 60 76 08 00 00 00 00  P`v.[`v.e`v.....
 * 0876 6050  77 77 77 2E 75 74 75 2E 66 69 00 73 6F 6E 65 72  www.utu.fi.soner
 * 0876 6060  61 2E 66 69 00 77 77 77 2E 68 73 2E 66 69 00 00  a.fi.www.hs.fi..
 * 0876 6070  00 00 00 00                                      ....
 *
 * (address_t)[0] 0876 6050  -> "www.utu.fi\0"
 * (address_t)[1] 0876 605B  -> "sonera.fi\0"
 * (address_t)[2] 0876 6065  -> "www.hs.fi\0"
 *
 *
 *	NOTE:   Depending on the "dirtyness" of the source string
 *          (whitespaces), allocated memory space may be larger
 *          than optimal, but this is insignificant in modern
 *          machines, and merely an academic remark.
 *
 * RETURN
 *
 *      NULL    If no items (or NULL parameter)
 *      char**  Pointer to allocated list of pointers
 */
static char **str2arr(const char *src)
{
    int  nItem = 0;
    char *ptr, *item = 0;
    char **array;

    /* return if NULL pointer or pointer to a NULL is received */
    if (!src || !*src)
        return (char **)NULL;

    /* 1st PASS
     * storage calculation & malloc();
     */
    ptr = (char *)src;      /* we won't touch it, we promise...     */
    do
    {
        if (!item && !isdelim(*ptr) && !isspace(*ptr))
        {
            /* new item found                               */
            item = ptr;     /* point item to start          */
            nItem++;        /* increment item count         */
        }
        else if (item && (isdelim(*ptr) || isspace(*ptr)))
        {
            item = 0;
        }
        ptr++;
    } while (*ptr);

#ifdef __DEBUG /* devDebugging */
printf("str2arr(): nItem: %d, src: \"%s\"\n", nItem, src);
#endif
    array = malloc((nItem + 1) * sizeof(char *) + strlen(src) + 1);
    memset(array, 0, (nItem + 1) * sizeof(char *) + strlen(src) + 1);

    /* point to the string space start */
    ptr = (char *)(array + nItem + 1);
    /* copy string to buffer, into correct offset) */
    strcpy(ptr, src);

    /* 2nd PASS
     * store pointers and NULL terminate items
     */
    nItem = 0;
    item  = 0;
    do
    {
        if (!item && !isdelim(*ptr) && !isspace(*ptr))
        {
            /* new item found                               */
            array[nItem] = item = ptr;
            nItem++;        /* increment item count         */
        }
        else if (item && (isdelim(*ptr) || isspace(*ptr)))
        {
            *ptr = '\0';
            item = 0;
        }
        ptr++;
    } while (*ptr);

    return array;
}

/*
 * Creates a list string from the special array-buffer.
 * Accepts only array-buffers created by str2arr().
 * Delmiter character is the first in CFG_LIST_DELIMITERS
 */
static char *arr2str(char **array)
{
    int   buffersize;   /* ditto, size of the return buffer */
    char *liststring;   /* malloc()'ed return buffer */

#ifdef __DEBUG /* devDebugging */
    fprintf(stderr, "arr2str(): Dumping array (%d bytes):\n", malloc_usable_size(array));
    dump_mem(array, malloc_usable_size(array));
#endif

    /*
     * Create string buffer of equal size to the array-buffer
     */
    buffersize = malloc_usable_size(array);
    if (buffersize < 0)
    {
        logerr("malloc_usable_size() returned %d", buffersize);
        return(NULL);
    }
    liststring = malloc(buffersize);

    /*
     * Write entries to the string
     * NOTE: liststring WILL NOT run out because source buffer
     *       already contains all the content plus a pointer list.
     *       No need guard against buffer overflow.
     */
    int   index = 0;
    char  *ptr = liststring;
    while (array[index])
    {
#ifdef __DEBUG /* devDebugging */
    fprintf(stderr, "arr2str(): array[%d]: \"%s\"\n", index, array[index]);
#endif
        strcpy(ptr, array[index]);
        ptr += strlen(array[index]);
        index++;
        /* If there is a next, append delimiter */
        if (array[index])
            *ptr++ = *CFG_LIST_DELIMITERS;
    }

#ifdef __DEBUG /* devDebugging */
    fprintf(stderr, "arr2str(): Result: \"%s\"\n", liststring);
#endif
    return(liststring);
}

/*
 * get_next_keyval()
 *
 * PRIVATE - Read (FILE *) and read "key = value" pairs.
 *
 * RETURNS
 *	keyval_t *	on success / found "key = value" pair
 *      NULL		on EOF
 */
static keyval_t *get_next_keyval(FILE *cfp)
{
#pragma message "This implementation can run out of it's buffer - NEEDS TO BE FIXED!"
	/* static storage, so that these may be
	 * passed to caller without losing the values.
	 * NOT FOR LONG TERM STORAGE, caller must
	 * immediately process/copy these values, as
	 * they will be written over in the next call.
         */
	static keyval_t keyval;
	static char buffer[256];
	char *ptr, *key, *val, *eql;
	keyval.key = keyval.val = (char *)NULL;

	while(fgets(buffer, sizeof(buffer), cfp))
	{
		ptr = buffer;
		key = val = eql = (char *)0;
		/* locate key eql val pointers */
		while(*ptr && *ptr != '\n' && *ptr != '#')
		{
			if(!key && !eql && !val && !isspace(*ptr))
				key = ptr;
			else if(key && !eql && !val && *ptr == '=')
				eql = ptr;
			else if(key && eql && !val && !isspace(*ptr))
				val = ptr;
			ptr++;
		}
		/* if successful find, populate struct and return */
		if(key && eql && val)
		{
			keyval.key = key;
			keyval.val = val;
			while(!isspace(*key) && *key != '=')
				key++;
			*key = '\0';
			while(!isspace(*val))
				val++;
			*val = '\0';
			return &keyval;
		}
		/* ELSE, we proceed reading the next line... */
	}

	return (keyval_t *)NULL;
}
