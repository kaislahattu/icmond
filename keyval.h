/*
 * keyval.h - 2016 Jani Tammi <janitammi@gmail.com>
 *
 *      Convenience datastructure. Basically a string array, but specifically
 *      designed for configuration values "key = value1, value2".
 *
 *      kv[0]   Contains the "key"
 *      kv[1]   Value 1
 *      kv[2]   Value 2
 *      ...
 *      kv[n]   NULL, list termination
 *
 *      All the strings and the pointer list is compiled in one allocated
 *      memory buffer, making it easy to dispose with single free() call.
 *
 *      Downside is that it is not easily modifiable, but this is not
 *      considered an issue due to the intended usage.
 *
 *  USAGE
 *
 *      Line from a configuration file or similar source is given to the
 *      keyval_create() function, which trims the content and removes comments.
 *      This datastructure is intended for convenient read actions and thus
 *      if editable structure is needed, something else needs to be chosen.
 */
#ifndef __KEYVAL_H__
#define __KEYVAL_H__

#define KEYVAL_LIST_DELIMITERS	",;"
#define KEYVAL_DELIMITER        "="

typedef char **keyval_t;


/*
 * Create keyval_t.
 *
 *      Buffer returned by this function can be free()'d normally.
 *      Honors the KEYVAL_LIST_DELIMITERS and KEYVAL_DELIMITER.
 *      Trims extranous whitespace from values (from beginning and end only).
 *      Considers "#" a comment starter and discards anything after #.
 */
keyval_t keyval_create(const char *keyvalstring);

/*
 * Check if fiven value is in given keyval_t
 */
int      keyval_iskey(keyval_t kv, const char *keyname);

/*
 * Return the number of values in kv (NOTE: key is not a value).
 */
int      keyval_nvalues(keyval_t kv);

/*
 * Return the content of the keyval_t as delimited list string
 */
//char *   keyval_getlist(keyval_t kv);

/*
 * "Remove" key from keyval_t, making it "regular" string value array
 */
char **  keyval2array(keyval_t kv);

/*
 * Create a buffer for full key-value set;
 * "key=val1,val2"
 */
char *   keyval2str(keyval_t kv);

/*
 * Create a buffer for values;
 * "val1,val2"
 */
char *   keyval2valstr(keyval_t kv);

/*
 * Removes pointers to empty strings.
 * Only does so with values, empty key will be left as-is.
 * Returns the kv (or NULL if NULL argument was given)
 */
keyval_t keyval_remove_empty_values(keyval_t kv);

/*
 * Describe keyval_t
 *
 *      Development and testing function. Parses information about the
 *      provided keyval_t (arg2) into the provided string pointer (arg1)
 *      Returns pointer to the buffer, but that's convenience for using
 *      this function as *printf() argument:
 *
 *      fprintf(stderr, "%s", bsprint_keyval(kv));
 */
char *   bsprint_keyval(char **buffer, keyval_t kv);


#endif /* __KEYVAL_H__ */
/* EOF keyval.h */
