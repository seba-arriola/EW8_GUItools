#ifndef __EXEC_PROGRAM_H__
#define __EXEC_PROGRAM_H__

/**
 * Execute a program and wait until it terminates.
 * @param program the command to run the program.
 * return 0 if success, non-zero otherwise.
 */
int exec_program(char *const program);

/**
 * Execute a program and wait until it terminates.
 * @param argv the argument vector which is an array of pointers to strings
 * passed to the new program as its command-line arguments, the first should
 * contain the filename associated with the file being executed.
 * @return 0 if success, non-zero otherwise.
 */
int exec_programv(char *const argv[]);

#endif
