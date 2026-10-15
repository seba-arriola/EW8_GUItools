/*
 * strfuncs.c
 *
 *  string functions
 */
#ifndef STRFUNCS_H
#define STRFUNCS_H

#include <string.h>

/**
 * Free the argument vector with last entry being a NULL terminator.
 * @param argv the argument vector;
 */
void freeargv(char *argv[]);

/**
 * Copy string src to buffer dst of size n.  At most n-1 non-null chars
 * will be copied.  Always null terminates (unless n == 0) the buffer.
 * This should replace strncpy which does not always null terminate.
 */
char *strcpyn(char *dst, const char *src, size_t n);

/**
 * strendswith:
 * Tests if the  specified string ends with the specified suffix.
 * @str the string
 * @suffix the suffix
 * @return non-zero  if the  specified string ends with the specified suffix;
 * zero otherwise
 */
int strendswith(char *const str, char *const suffix);

/*
 * The strlenn() function computes the length of the string str,
 * but stops when the length reaches the maximum of n.
 */
size_t strlenn(const char *str, size_t n);

/**
 * strreplace:
 * @str: a string to replace characters in
 * @strsize: the size of the str array
 * @oldChar: the character to be replaced
 * @newChar: the character to replace
 *
 * Modifies all old characters from a string with the specified
 * new character.
 *
 * This function doesn't allocate or reallocate any memory;
 * it modifies @str in place. Therefore, it cannot be used on
 * statically allocated strings.
 *
 * The pointer to @str is returned to allow the nesting of functions.
 *
 * Returns: @str
 */
char *strreplace(char *str, size_t strsize, char oldChar, char newChar);

/**
 * strreplace_string:
 * @orig: a string to replace characters in
 * @rep:  substring to be replaced
 * @with: new substring
 *
 * Replace substring in the string
 *
 * !!!!!!!!!You must free the result if result is non-NULL.!!!!!!!!!!!!!!!!
 *
 * The pointer to @str is returned to allow the nesting of functions.
 *
 * Returns: new allocated modified string
 */
char *strreplace_string(char *orig, char *rep, char *with);

/**
 * strstartswith:
 * Tests if the  specified string starts with the specified prefix.
 * @str the string
 * @prefix the suffix
 * @return non-zero  if the  specified string starts with the specified prefix;
 * zero otherwise
 */
int strstartswith(char *const str, char *const prefix);

/**
 * strstrip:
 * @str: a string to remove the leading and trailing whitespace from
 *
 * Removes leading and trailing whitespace from a string, by moving the rest
 * of the characters forward.
 *
 * This function doesn't allocate or reallocate any memory;
 * it modifies @str in place. Therefore, it cannot be used on
 * statically allocated strings.
 *
 * The pointer to @str is returned to allow the nesting of functions.
 *
 * Returns: @str
 */
char *strstrip(char *const str);

/**
 * strstripLeading:
 * @str: a string to remove the leading whitespace from
 *
 * Removes leading whitespace from a string, by moving the rest
 * of the characters forward.
 *
 * This function doesn't allocate or reallocate any memory;
 * it modifies @str in place. Therefore, it cannot be used on
 * statically allocated strings.
 *
 * The pointer to @str is returned to allow the nesting of functions.
 *
 * Returns: @str
 */
char *strstripLeading(char *const str);

/**
 * strstripTrailing:
 * @str: a string to remove the trailing whitespace from
 *
 * Removes trailing whitespace from a string.
 *
 * This function doesn't allocate or reallocate any memory;
 * it modifies @str in place. Therefore, it cannot be used
 * on statically allocated strings.
 *
 * The pointer to @str is returned to allow the nesting of functions.
 *
 * Returns: @str
 */
char *strstripTrailing(char *const str);

/**
 * Convert string to argument vector with last entry being a NULL terminator.
 * The memory obtained is done dynamically using calloc and hence it can be
 * freed using freeargv().
 * @param str the string.
 * @param delim the deliimter character, typically the space character.
 * @return the argument vector.
 */
char **str2argv(char *const str, const char delim);

#endif
