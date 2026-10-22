#include "input.h"

#include <sys/types.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "connection.h"

#define FIFO_NAME_SIZE 64

/* =========================================================================
 * Client argument parsing
 * ========================================================================= */

/**
 * @brief Parse a server PID from a command line argument
 * @details
 * Converts the provided text into a positive process ID. The client needs a
 * valid server PID so it can send SIGUSR1 during the connection handshake.
 *
 * @param text command line argument containing the server PID
 * @param out_pid output location for the parsed PID
 * @return 1 if the PID is valid, otherwise 0
 */
static int parse_pid(const char *text, pid_t *out_pid) {
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);

    /* Reject empty strings, bad characters, and conversion errors. */
    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }

    /* A usable PID must be positive and fit in the expected integer range. */
    if (value < 1 || value > INT_MAX) {
        return 0;
    }

    *out_pid = (pid_t)value;
    return 1;
}

/* =========================================================================
 * Client entry point
 * ========================================================================= */

/**
 * @brief Start the animate client
 * @details
 * Validates the server PID, connects to the server using the assignment signal
 * and FIFO protocol, then hands control to the client input loop. Once the
 * loop finishes, the FIFO descriptors are closed before the process exits.
 *
 * @param argc number of command line arguments
 * @param argv command line argument values
 * @return 0 on normal exit, otherwise non-zero on setup or communication error
 */
int main(int argc, char **argv) {
    pid_t server_pid;
    pid_t client_pid;
    int c2s_fd;
    int s2c_fd;
    char fifo_c2s[FIFO_NAME_SIZE];
    char fifo_s2c[FIFO_NAME_SIZE];

    if (argc != 2) {
        fprintf(stderr, "Usage: ./animate_client <server pid>\n");
        return 1;
    }

    if (!parse_pid(argv[1], &server_pid)) {
        fprintf(stderr, "Invalid server PID\n");
        return 1;
    }

    client_pid = getpid();

    /*
     * Build the FIFO names for this client PID. The connection helper also
     * uses this same naming scheme when opening the server-created FIFOs.
     */
    snprintf(fifo_c2s, sizeof(fifo_c2s), "FIFO_C2S_%d", client_pid);
    snprintf(fifo_s2c, sizeof(fifo_s2c), "FIFO_S2C_%d", client_pid);

    /*
     * Ask the server to create the FIFO pair and open both communication
     * channels once SIGUSR2 confirms they are ready.
     */
    if (!client_connect_to_server(server_pid, client_pid,
                                  &c2s_fd, &s2c_fd)) {
        return 1;
    }

    /* Process user commands until EOF, Disconnect, or a communication error. */
    int result = client_input_loop(c2s_fd, s2c_fd);

    close(c2s_fd);
    close(s2c_fd);

    return result;
}