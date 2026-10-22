#ifndef CLIENT_CONNECTION_H
#define CLIENT_CONNECTION_H

#include <sys/types.h>

/* =========================================================================
 * Client connection interface
 * ========================================================================= */

/**
 * @brief Connect this client process to the animate server
 * @details
 * Performs the client side of the assignment connection protocol. The client
 * sends SIGUSR1 to the server, waits for SIGUSR2, then opens the FIFO pair:
 * FIFO_C2S_<client_pid> for writing and FIFO_S2C_<client_pid> for reading.
 *
 * @param server_pid PID of the running animate_server process
 * @param client_pid PID of this animate_client process
 * @param c2s_fd output location for the client to server FIFO descriptor
 * @param s2c_fd output location for the server to client FIFO descriptor
 * @return 1 if the connection succeeds, otherwise 0
 */
int client_connect_to_server(pid_t server_pid, pid_t client_pid,
                             int *c2s_fd, int *s2c_fd);

#endif