#ifndef UTIL_H
#define UTIL_H

/* =========================================================================
 * Shared utility interface
 * ========================================================================= */

/**
 * @brief Remove one trailing newline from a string
 * @details
 * Updates the supplied string in place. This is useful after reading protocol
 * lines from stdin or a FIFO when the code needs to compare the command text
 * without the final '\n'.
 *
 * @param s string to update in place
 */
void strip_newline(char *s);

#endif