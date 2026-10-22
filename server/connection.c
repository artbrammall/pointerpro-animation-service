#include "connection.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#define FIFO_NAME_SIZE 64

/* =========================================================================
 * Server FIFO connection helpers
 * ========================================================================= */

/**
 * @brief Create the FIFO pair for a connecting client
 * @details
 * Builds the exact FIFO names required by the assignment protocol:
 * FIFO_C2S_<client_pid> and FIFO_S2C_<client_pid>. Any old FIFOs with the same
 * names are removed first, then new FIFOs are created for bidirectional
 * client/server communication.
 *
 * The FIFOs are briefly opened in read/write non-blocking mode so creation
 * completes cleanly without either side getting stuck waiting for the other
 * end. The actual long-term FIFO descriptors are opened later by the client
 * state setup code.
 *
 * @param client_pid PID of the client that requested a connection
 * @return 1 if both FIFOs were created successfully, otherwise 0
 */
int server_create_client_fifos(pid_t client_pid) {
    char c2s_path[FIFO_NAME_SIZE];
    char s2c_path[FIFO_NAME_SIZE];
    int c2s_fd;
    int s2c_fd;

    /* FIFO names must match the brief exactly or the client will miss them. */
    snprintf(c2s_path, sizeof(c2s_path), "FIFO_C2S_%d", client_pid);
    snprintf(s2c_path, sizeof(s2c_path), "FIFO_S2C_%d", client_pid);

    /* Remove stale FIFOs left behind by a previous crashed run. */
    unlink(c2s_path);
    unlink(s2c_path);

    if (mkfifo(c2s_path, 0666) == -1) {
        return 0;
    }

    if (mkfifo(s2c_path, 0666) == -1) {
        unlink(c2s_path);
        return 0;
    }

    /*
     * Open both ends temporarily to avoid FIFO open blocking edge cases during
     * the signal handshake. These descriptors are not used for real messages.
     */
    c2s_fd = open(c2s_path, O_RDWR | O_NONBLOCK);
    s2c_fd = open(s2c_path, O_RDWR | O_NONBLOCK);

    if (c2s_fd == -1 || s2c_fd == -1) {
        if (c2s_fd != -1) {
            close(c2s_fd);
        }
        if (s2c_fd != -1) {
            close(s2c_fd);
        }

        unlink(c2s_path);
        unlink(s2c_path);
        return 0;
    }

    close(c2s_fd);
    close(s2c_fd);

    return 1;
}