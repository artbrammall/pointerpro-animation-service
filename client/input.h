#ifndef CLIENT_INPUT_H
#define CLIENT_INPUT_H

/* =========================================================================
 * Client input interface
 * ========================================================================= */

/**
 * @brief Run the main client command loop
 * @details
 * Reads commands from stdin, applies the client side login gate, forwards
 * allowed protocol messages to the server FIFO, then reads and prints server
 * responses in the format required by the assignment.
 *
 * @param c2s_fd client to server FIFO descriptor
 * @param s2c_fd server to client FIFO descriptor
 * @return 0 on normal client exit, otherwise non-zero on communication failure
 */
int client_input_loop(int c2s_fd, int s2c_fd);

#endif