/*
 * Copyright (c) 2000-2008, 2011, Juniper Networks, Inc.
 * All rights reserved.
 */

#ifndef __JNX_STREXTRA_H__
#define __JNX_STREXTRA_H__

/**
 * @file strextra.h
 * @brief 
 * String manipulation APIs.
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <common/aux_types.h>
#include <common/env.h>

#include <grpc/support/alloc.h>

#include "config.h"

/*
 * Use C linkage when using a C++ compiler
 */
#ifdef __cplusplus
extern "C" { 
    namespace junos {
#endif /* __cplusplus */

/**
 * @brief
 * Produces output into a dynamically-allocated character string buffer
 * according to a format specification string and an appropriate number
 * of arguments.
 *
 * @param[in] fmt
 *     Format string (see sprintf(3) for a description of the format)
 * @param[in] ...
 *     Arguments sufficient to satisfy the format string
 *
 * @return 
 *     A pointer to the resultant string.
 */
char *strdupf (const char *fmt, ...) PRINTFLIKE(1, 2);

/**
 * @brief
 * Appends a string to a base string, freeing the appended string,
 * if requested.
 *
 * @a If @a free_args is non-zero, @a appendstr is freed. Otherwise,
 * it's not.  If the memory space pointed by @a basestr is sufficient
 * for both @a basestr and @a appendstr, no new memory allocated and
 * @a basestr is returned.  Otherwise, a new memory is allocaged. If
 * that is successful, the address of new memory is returned and @a
 * baseptr is freed. Otherwise, NULL is returned and @a baseptr is not
 * freed.
 *
 * @param[in] basestr
 *     Base string to append to, it has to point to allocated memory
 *     if the current memory space is not sufficent for both @a basestr
 *     and @a appendstr.
 * @param[in] appendstr
 *     String to be appended to @a basestr
 * @param[in] free_args
 *     Indicator as to whether or not the arguments are allowed to be freed.
 *
 * @return 
 *     The combined string on success, NULL on failure.
 */   
char *strcombine (char *basestr, char *appendstr, int free_args);

#ifndef HAVE_STRNDUP
/**
 * @brief
 * Allocates sufficient memory for a copy of a string, up to the maximum
 * number of characters specified.
 *
 * @param[in] str
 *     String to duplicate
 * @param[in] count
 *     Maximum number of characters to copy
 *
 * @return 
 *     A duplicate string of up to @a count characters of @a str.
 */
char *strndup (const char *str, size_t count);
#endif /* HAVE_STRNDUP */

/**
 * @brief
 * Converts a string to lower case.
 *
 * @param[in] str
 *     String to convert to lower case.
 * @param[in] inplace
 *     If 0, allocate space for the lower case string.
 *     Otherwise, do the conversion in-place.
 * 
 * @return 
 *     The string in lower case form.  If @a inplace is 0, this
 *     string is dynamically-allocated and should be used as an
 *     argument to the function free(3) when no longer neeeded.
 *
 * @sa strupper()
 */
char *strlower (char *str, int inplace);

/**
 * @brief
 * Converts a strng to upper case.
 *
 * @param[in] str
 *     String to convert to upper case.
 * @param[in] inplace
 *     If 0, allocate space for the upper case string.
 *     Otherwise, do the conversion in-place.
 *
 * @return 
 *      The string in upper case form.  If @a inplace is 0, this
 *      string is dynamically-allocated and should be used as an
 *      argument to the function free(3) when no longer neeeded.
 *
 * @sa strlower()
 */
char *strupper (char *str, int inplace);

/**
 * @brief
 * Identical to strsep(3), but takes care of quoted segments.
 * 
 * Quoted segments of the string are skipped when searching for a delimiter.
 * A quoted segment is enclosed in a pair of one of the characters given in
 * the quotes string.
 *
 * @param[in,out] search_string
 *     Pointer to the string to search
 * @param[in] delimiter
 *     Characters to use as delimiters
 * @param[in] quotes
 *     Characters to use as 'quotes'
 *
 * @return
 *     The location of the next character after the delimiter;
 *     @c NULL if the end of @a search_string is reached;
 *     If @a *search_string is @c NULL, strsep_quoted returns @c NULL.   
 */
char *strsep_quoted (char **search_string, const char *delimiter,
		     const char *quotes);

/**
 * @brief
 * An escaped version of strsep(3).  
 *
 * This function locates the first occurrence
 * of any character in the delimiter string.  It allows for characters in
 * delim to be escaped in the target string.  For example, if @a delim is set
 * to "xyz", the characters x, y, and z would be ignored when parsing the
 * string "abcd...\x\y\z", but not when parsing the string "abcd...xyz".
 *
 * @param[in,out] stringp
 *     Pointer to string to search.  The location of the next
 *     character after the delimiter character (or @c NULL, if the
 *     end of the string was reached) will be stored in the
 *     location pointed to by this variable upon return.
 * @param[in] delim
 *     Delimiters.  A string containing the characters to search
 *     for in @a stringp.
 * 
 * @return 
 *     The original value of @a *stringp.
 *     @c NULL if @a *stringp is @c NULL.
 */
char *strsep_escaped (register char **stringp,
		      register const char *delim);

/**
 * @brief
 * Safe form of snprintf(3) that returns the number of characters written,
 * rather than the number of characters that would have been written.
 *
 * @param[out] out
 *     Pointer to the output buffer
 * @param[in]  outsize
 *     Size of the output buffer, in bytes
 * @param[in]  fmt
 *     Format string (see sprintf(3) for a description of the format)
 * @param[in] ...
 *     Arguments sufficient to satisfy the format string
 *
 * @return 
 *     The number of characters written to the output buffer.
 */
size_t snprintf_safe (char *out, size_t outsize, const char *fmt, ...)
    PRINTFLIKE(3, 4);

/**
 * @brief
 * Converts string buffer to printable form.
 *
 * The result will be placed in @a outbuf and returned.
 *
 * @param[out] outbuf
 *     Pointer to the output buffer
 * @param[out] outlen
 *     Size of the output buffer, in bytes
 * @param[in] buf
 *     Pointer to the string buffer
 * @param[in] len
 *     Number of characters in the string buffer
 *
 * @return 
 *     A pointer to the output buffer on success,
 *     @c NULL on error (@c errno will be set accordingly.)
 */
char *snstrprintable (char *outbuf, size_t outlen,
		      const char *buf, size_t len);

/**
 * @brief
 * Converts string buffer to printable form.
 *
 * @param[in] buf
 *     Pointer to the string buffer
 * @param[in] len
 *     Number of characters in the string buffer
 *
 * @return 
 *     A pointer to the converted string on success,
 *     @c NULL on error (@c errno will be set accordingly.)
 */
static inline char *
strprintable (const char *buf, size_t len)
{
    return snstrprintable(0, 0, buf, len);
}

/**
 * @brief
 * Given a string and a substring, finds the next character
 * after the substring in the string.
 *
 * Example:
 * 	<tt>stroffset("the fox ran", "fox ") =\> "ran"</tt>
 *
 * @param[in] big
 *     String to search
 * @param[in] little
 *     Substring to match
 *
 * @return 
 *     A pointer to the first character after the substring @a little as
 *     found in @a big, @c NULL otherwise
 */
static inline char *
stroffset (char *big, const char *little)
{
    char *t = strstr(big, little);
    
    return t ? t + strlen(little) : 0;
}

/**
 * @brief
 * Given two strings, return true if they are the same.
 * 
 * Tests the first characters for equality before calling strcmp.
 *
 * @param[in] red, blue
 *     The strings to be compared
 *
 * @return
 *     Nonzero (true) if the strings are the same. 
 */
static inline int
streq (const char *red, const char *blue)
{
    return *red == *blue && (*red == '\0' || strcmp(red + 1, blue + 1) == 0);
}

char *strdup2 (char *str1, char *str2);
char *strredup2 (char *str1, char *str2);
char *strip_trail (char *s);
char *strstrip_newline (char *str);
char *straddquoted (char *, size_t, const char *, const char *);
char *straddalwaysquoted (char *, size_t, const char *, const char *);

/*
 * strrnstr -- Find last occurrence of k in s. Length of s is len.
 *
 * This is embarassingly inefficient. Short of a Boyer-Moore style
 * preprocessing of k, I don't think there's a way to speed this up
 * that works for all strings.
 */
static inline char *
strrnstr (const char *s, const char *k, int len)
{
    int klen = strlen(k);
    register char *cp;
    register char c;

    if (len == 0 || klen > len)
	return NULL;

    cp = strstr(s, "");	 /* hack--get rid of const on s */
    cp += len - klen + 1;
    c = *k;

    do {
	if (--cp < s) return NULL;
	while (*cp != c && cp > s) cp--;
    } while (strncmp(cp, k, klen) != 0);

    return cp;
}

/*
 * strsfx --  Returns ptr to start of k in s, if k is a suffix of s.
 */
static inline char *
strsfx (char *s, const char *k)
{
    int slen = strlen(s);
    int klen = strlen(k);
    
    if (slen < klen)
	return NULL;

    s += slen - klen;

    while (*s) {
	if (*s++ != *k++)
	    return NULL;
    }
    
    return s - klen;
}

/*
 * Leading string compare. 
 *
 * Returns TRUE if 'compare' is a substring of 'leading'.  If allow_null 
 * is true -  returns TRUE if leading is NULL or an empty string.
 */
static inline int
strlcmp (const char *leading, const char *compare, int allow_null)
{
    if (compare == NULL) return 0;
    if (leading == NULL || *leading == 0) return allow_null;
    return *leading == *compare
		&& strncmp(leading, compare, strlen(leading)) == 0;
}


/*
 * memdup(): allocates sufficient memory for a copy of the
 * buffer buf, does the copy, and returns a pointer to it.  The pointer may
 * subsequently be used as an argument to the function free(3).
 */
static inline void *
memdup (const void *buf, size_t size)
{
    void *vp = gpr_malloc(size);
    if (vp) memcpy(vp, buf, size);
    return vp;
}

/*
 * strdup_sz: strlen + strdup in one
 * 
 * if sz is not NULL, the length for the string 
 * (not including the null character) is stored 
 * in sz
 */
static inline char* 
strdup_sz (char* s, size_t* out_sz) 
{
    size_t sz = strlen(s);
    char*  str = (char *) memdup(s, sz + 1);

    if (out_sz) *out_sz = sz;
    return str;
}

static inline char *
strnextws (char *input)
{
    if (input) {
	while (*input && !isspace((int) *input)) {
	    input++;
	}
    }
    return input;
}

/*
 * str_char_count: counts the number of chars in string.
 */
static inline int
str_char_count (const char *str, int ch)
{
    int count;
    
    for (count = 0; *str; str++) {
	if (*str == ch) count++;
    }
    return count;
}

/*
 * strtrimws: skip over leading whitespace
 */
static inline char *
strtrimws (char *input)
{
    while (isspace((int) *input))
	input += 1;
    return input;
}

/*
 * strtrimws: skip over leading whitespace
 */
static inline char *
strtrimwsc (char *input, unsigned *count)
{
    while (isspace((int) *input)) {
	if (*input == '\n') *count += 1;
	input += 1;
    }
    return input;
}

/*
 * strtrimtailws: skip over trailing whitespace
 */
static inline char *
strtrimtailws (char *input, char *start)
{
    while (start < input && isspace((int) *input))
	input -= 1;
    return input;
}

/*
 * RFC 1035 encode and decode
 */
boolean
string_encode_rfc1035_buffer (char *buf, size_t size, off_t *offsetp,
			      const char *str);

const char *
string_encode_rfc1035 (const char *str, const char *delim, size_t *lenp);

const char *
string_decode_rfc1035 (const char *encoded, size_t size, const char *delim);


/*
 * RFC 3548 Base32 encode and decode.
 */
static inline size_t
string_encode32_size (size_t src_size)
{
    return (src_size * 8) / 5 + (((src_size * 8) % 5) ? 1 : 0);
}

size_t
string_encode32 (u_int8_t *src, size_t src_size, char *dst, size_t dst_size);

size_t
string_decode32 (u_int8_t *src, size_t src_size, u_int8_t *dst,
		 size_t dst_size);

#ifndef HAVE_STRNSTR
static inline char *
strnstr (char *s1,  const char *s2, size_t n)
{
    char first = *s2++;
    size_t s2len;
    char *cp, *np;

    if (first == '\0')	      /* Empty string means immediate match */
	return s1;

    s2len = strlen(s2); /* Does not count first */
    for (cp = s1; *cp; cp = np + 1) {
	np = strchr(cp, first);
	if (np == NULL)
	    return NULL;
	if (s2len == 0)		/* s2 is only one character long */
	    return np;
	if (n - (np - s1) < s2len)
	    return NULL;
	if (strncmp(np + 1, s2, s2len) == 0)
	    return np;
    }

    return NULL;
}
#endif /* HAVE_STRNSTR */

#ifndef HAVE_STRLCPY
static inline size_t
strlcpy (char *dst, const char *src, size_t left)
{
    const char *save = src;

    if (left == 0)
	return strlen(src);

    while (--left != 0)
	if ((*dst++ = *src++) == '\0')
	    break;

    if (left == 0) {
	*dst = '\0';
	while (*src++)
	    continue;
    }

    return src - save - 1;
}
#endif /* HAVE_STRLCPY */

#ifdef __cplusplus
    }
}
#endif /* __cplusplus */

#endif /* __JNX_STREXTRA_H__ */
