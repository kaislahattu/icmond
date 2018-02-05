/*
 * binary.h - 2002, 2016 Jani Tammi <janitammi@gmail.com>
 *
 *  2016.09.05  Fixed dump_mem() to use unsigned int for addresses.
 *              Hid the "unused variable" complaint for copyright.
 *
 *    Hex and binary utilities
 */
/* #pragma once */
#ifndef __BINARY_H__
#define __BINARY_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>			/* memset()  */
#include <ctype.h>			/* isprint() */
#include <malloc.h>         /* malloc()  */

/* Essentially, disable the compile time pragma message... */
#ifdef __BINARY_H__SHOW_PRAGMA_MESSAGE
#ifndef __FILEINFO__
#define __LINE_NO__(A,B)   A"("#B") : "
#define __LINE_NO2__(A,B) __LINE_NO__(A,B)
#define __FILEINFO__ __LINE_NO2__(__FILE__,__LINE__)
#endif /* __FILEINFO__ */

#ifndef __BINARY_H__COPYRIGHT_DISPLAYED
#define __BINARY_H__COPYRIGHT_DISPLAYED
#pragma message( __FILEINFO__ "(c) Jani Tammi 2002 All Rights Reserved" )
#endif /* __BINARY_H__COPYRIGHT_DISPLAYED */
#endif /* __BINARY_H__SHOW_PRAGMA_MESSAGE */


#ifdef  __cplusplus
extern "C" {
#endif

/*
 * int2char()
 *
 * Description
 *
 *  Gives printable char representing the hex value of the lowest byte of the int.
 *
 * Arguments
 *
 *	1) integer to be written in hex.
 *
 * Return value
 *
 *	char having character value from '0' to '9' or 'a' to 'f'
 *
 * Example
 *
 *  int2char(0xffffabcd) = 'd'
 */
char int2char(const int i);

/*
 * int2HexStr()
 *
 * Description
 *
 *  Creates a 9 byte string (NULL termination) .
 *
 * Arguments
 *
 *	1) integer to be written into hex string.
 *
 * Return value
 *
 *	malloc()'ed string buffer containing the string
 *
 * Example
 *
 *  int2HexStr(0xffffabcd) = "ffffabcd"
 */
char *int2HexStr(const int i);

/*
 * int2BinStr()
 *
 * Description
 *
 *  Writes into string s (that must have atleast 32 bytes space) binay
 *  representation of integer x
 *
 * Arguments
 *
 *	1) pointer to a buffer having atleast 32 bytes of free space.
 *  2) integer which is to be written in binary presentation string.
 *
 * Return value
 *
 *  For convenience, the function returns the argument s, so it may
 *  easily be nested into other function calls.
 */
char *int2BinStr(char *s, const int x);

/*
 * getBits()
 *
 * Description
 *
 *  Fetches n bits from position p within integer x.
 *
 * Arguments
 *
 *	1) integer from which the bits are to be extracted.
 *	2) integer, how many'eth bit from the right signified, zero indexed, start.
 *  3) integer, how many bits.
 *
 * Return value
 *
 *	Right aligned integer consisting of the specified bits
 *
 * Example
 *
 *  getBits(0xffffabcd, 11, 8) = 0xbc
 */
unsigned int getBits(const unsigned int x, const int p, const int n);

/*
 * dump_mem()
 *
 * Description
 *
 *  Ancient function I did to write hex data to stdout.
 *  This code could enter IOCCC...
 *
 * Arguments
 *
 *	1) void pointer to a memory address from where to dump.
 *  2) how many bytes to dump.
 *
 * Return value
 *
 *  None
 */
void dump_mem(const void *src, const int len);

#ifdef  __cplusplus
}
#endif

#endif /* __BINARY_H__ */
/* EOF */
