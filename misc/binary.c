/*
 * binary.c - 2002, 2016 Jani Tammi <janitammi@gmail.com>
 *
 *  2016.09.05  Fixed dump_mem() to use unsigned int for addresses.
 *              Hid the "unused variable" complaint for copyright.
 *  2016.09.09  Created .c for the .h, to avoid multiple defines.
 *
 *    Hex and binary utilities
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>         /* memset()  */
#include <ctype.h>          /* isprint() */
#include <malloc.h>         /* malloc()  */

#include "binary.h"

/*
 * Leave a copyright string into the compiled binary
 */
#ifdef __GNUC__
#define VARIABLE_IS_NOT_USED __attribute__ ((unused))
#else
#define VARIABLE_IS_NOT_USED
#endif /* __GNUC__ */
#define CREATED	(__FILE__ " by Jani Tammi (c) 2002 (compiled " __DATE__ " " __TIME__ ")")
static const char * VARIABLE_IS_NOT_USED copyright = CREATED;


char int2char(const int i)
{
	static char a[2] = {0, 0};
	sprintf(a, "%x", (int)((unsigned char)i));
	return *a;
}

char *int2HexStr(const int i)
{
	char *s;
	s = (char *)malloc(9); /* 8 bytes + null termination */

	*s			= int2char((i >> 28) & 0xf);
	*(s + 1)	= int2char((i >> 24) & 0xf);
	*(s + 2)	= int2char((i >> 20) & 0xf);
	*(s + 3)	= int2char((i >> 16) & 0xf);
	*(s + 4)	= int2char((i >> 12) & 0xf);
	*(s + 5)	= int2char((i >>  8) & 0xf);
	*(s + 6)	= int2char((i >>  4) & 0xf);
	*(s + 7)	= int2char( i        & 0xf);
	*(s + 8)    = (char)0;

	return s;
}

char *int2BinStr(char *s, const int x)
{
	int c;
	for(c = 0; c < 32; c++)
        s[c] = (x & (1 << (31 - c))) ? '1' : '0';
	return s;
}

unsigned int getBits(const unsigned int x, const int p, const int n)
{
	return (x >> (p + 1 - n)) & ~(~0 << n);
};

void dump_mem(const void *src, const int len)
{
/*       10        20        30        40        50        60        70        80
         |         |         |         |         |         |         |         |
0000 0000  68 65 6c 6c 6f 20 62 69   67 20 77 6f 72 6c 64 21   hello.bi g.world!
 * ("0F99 1023  ", total 11 characters)
 *  4   upper part of address
 *  1   space
 *  4   lower part of address
 *  2   two spaces ("  ")
 *
 * ("68 65 6c 6c 6f 20 62 69" = "hello bi", total 23 characters)
 *  2   Byte 0 in hex
 *  1   space
 *  2   Byte 1 in hex
 *  1   space
 *  2   Byte 2 in hex
 *  1   space
 *  2   Byte 3 in hex
 *  1   space
 *  2   Byte 4 in hex
 *  1   space
 *  2   Byte 5 in hex
 *  1   space
 *  2   Byte 6 in hex
 *  1   space
 *  2   Byte 7 in hex
 *
 *  3   spaces ("   ", total 3 characters)
 *
 * ("67 20 77 6f 72 6c 64 21" = "g world!", total 23 characters)
 *  2   Byte 0 in hex
 *  1   space
 *  2   Byte 1 in hex
 *  1   space
 *  2   Byte 2 in hex
 *  1   space
 *  2   Byte 3 in hex
 *  1   space
 *  2   Byte 4 in hex
 *  1   space
 *  2   Byte 5 in hex
 *  1   space
 *  2   Byte 6 in hex
 *  1   space
 *  2   Byte 7 in hex
 *
 * So far total: 11 + 23 + 3 + 23 = 60 characters (20 left)
 *  3   spaces ("   ", total of 3 characters)
 *
 * ("hello.bi g.world!", total of 17 characters)
 *  8   lower stringyfied content ("hello.bi")
 *  1   space (" ")
 *  8   upper stringyfied content ("g.world!")
 *
 * Grand total of 80 characters - just the width of standard console.
 */
#define LINE_WIDTH	80 + 1  // +1 for null termination
#define HEX_START   11      // offset where hex data 1 begins
#define STR_START   60      // offset where hex data 2 begins

    /*
     * i        stepping calculator, to process 16 bytes per
     *          displayable line.
     * ptr      traversing source and incrementing.
     * end      pre-calculated end pointer for the memory space.
     *          Used to compare to ptr, detect the end.
     * line     Buffer where output is written, one line at the time.
     */
	unsigned int     i;
	char            *ptr, *end;
	char             line[LINE_WIDTH];

    /* Return if null pointer was given. Kernel space for sure...
     */
	if(!(ptr = (char *)src))
		return;

	memset(&line, ' ', LINE_WIDTH);     /* fill with space ...          */
	line[LINE_WIDTH] = '\0';            /* ...and set null termination  */
	end = ptr + len;                    /* calculate end pointer        */

	/*
	 * "Rewind" ptr into closest 16byte aligned addr (less than src)
	 */
// printf("src: 0x%.8x, src mod 16: 0x%.8x\n", (unsigned int)src, (unsigned int)src % 16);
	ptr = (char *)((unsigned int)src - (unsigned int)src % 16);
// printf("src: 0x%.8x, aligned ptr: 0x%.8x\n", (unsigned int)src, (unsigned int)ptr);

	do {
        /* print address in format: "0000 0000  " */
        sprintf(
               (char *)&line,
               "%.4x %.4x  ",
               ((unsigned int)ptr >> 16) & 0xffff,
               (unsigned int)ptr & 0xffff
               );
/*        sprintf((char *)&line, "%.8X: ", ptr); // address part */
        /* 16 bytes per display line */
        for (i = 0; i < 16; i++)
        {
            /* If ptr + i is in given source range */
			if (
                (unsigned int)ptr + i >= (unsigned int)src &&
                (unsigned int)ptr + i <  (unsigned int)end
               )
            {
                /* hex entry */
				sprintf(
                       (char *)((unsigned int)&line + HEX_START + i * 3),
                       "%.2X ",
                       (unsigned char)*(ptr + i)
                       );
                /* char entry */
				sprintf(
                       (char *)((unsigned int)&line + STR_START + i),
                       "%c ",
                       isprint((int)*(ptr + i)) ? (unsigned char)*(ptr + i) : '.'
                       );
			}
            else
            /* not in displayable range */
            {
				sprintf((char *)((unsigned int)&line + HEX_START + i * 3), "   ");
				sprintf((char *)((unsigned int)&line + STR_START + i), "  ");
			}
		}
		line[STR_START - 1] = ' '; /*  clear sprintf's NULL termination */
		ptr += 16;
		fprintf(stderr, "%s\n", (char *)&line);
	} while((unsigned int)ptr < (unsigned int)end);

	return;
}