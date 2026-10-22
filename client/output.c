#include "output.h"

#include <stdio.h>
#include <string.h>

/* =========================================================================
 * Server response output helpers
 * ========================================================================= */

/**
 * @brief Print the user facing message for a server response
 * @details
 * The server sends compact protocol codes such as "-1", "-2", or "0 <value>".
 * This helper converts those codes into the exact text required by the brief.
 * Generate has a few special success-shaped failure replies, so those are
 * checked before the general "0 <value>" success case.
 *
 * @param response raw response string read from FIFO_S2C_<client_pid>
 */
void print_server_response(const char *response) {
    if (strcmp(response, "-1") == 0) {
        printf("RPC Failed\n");
    } else if (strcmp(response, "-2") == 0) {
        printf("Value error\n");
    } else if (strcmp(response, "-3") == 0) {
        printf("Internal error\n");
    } else if (strcmp(response, "0 -1") == 0) {
        printf("Data write failed\n");
    } else if (strcmp(response, "0 0 -1") == 0) {
        printf("Movie write failed\n");
    } else if (strcmp(response, "0 0 0") == 0) {
        printf("Success\n");
    } else if (strcmp(response, "0") == 0) {
        printf("Success\n");
    } else if (strncmp(response, "0 ", 2) == 0) {
        printf("Success %s\n", response + 2);
    } else {
        /*
         * Keep unknown responses visible. This is useful for Login rejection
         * messages and avoids hiding unexpected protocol output while testing.
         */
        printf("%s\n", response);
    }

    fflush(stdout);
}