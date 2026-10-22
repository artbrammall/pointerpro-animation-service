#ifndef OUTPUT_H
#define OUTPUT_H

/* =========================================================================
 * Client output interface
 * ========================================================================= */

/**
 * @brief Print the user facing version of a server response
 * @details
 * Converts protocol replies such as "-1", "-2", "0", and "0 <value>" into
 * the exact stdout messages required by the assignment. This keeps response
 * formatting in one place instead of scattering protocol text through the
 * client input loop.
 *
 * @param response raw response string received from the server FIFO
 */
void print_server_response(const char *response);

#endif