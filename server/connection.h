#ifndef SERVER_CONNECTION_H
#define SERVER_CONNECTION_H

#include <sys/types.h>

/* =========================================================================
 * Server connection interface
 * ========================================================================= */

/**
 * @brief Create the FIFO pair for a connecting client
 * @details
 * Creates FIFO_C2S_<client_pid> and FIFO_S2C_<client_pid> for the assignment
 * connection protocol. The server calls this after receiving SIGUSR1 from a
 * client and before sending SIGUSR2 back.
 *
 * @param client_pid PID of the client requesting a connection
 * @return 1 if both FIFOs were created successfully, otherwise 0
 */
int server_create_client_fifos(pid_t client_pid);

#endif