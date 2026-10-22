#include "util.h"

#include <string.h>

/* =========================================================================
 * Shared string helpers
 * ========================================================================= */

/**
 * @brief Remove one trailing newline from a string
 * @details
 * Protocol messages and stdin input often include a final '\n'. This helper
 * trims that newline when code needs to compare or print the message content
 * without the line ending.
 *
 * @param s string to update in place
 */
void strip_newline(char *s) {
    size_t len = strlen(s);

    if (len > 0 && s[len - 1] == '\n') {
        s[len - 1] = '\0';
    }
}