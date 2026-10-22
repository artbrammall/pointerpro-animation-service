#define _POSIX_C_SOURCE 200809L

#include "connection.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define FIFO_NAME_SIZE 64

/*
 * Set by the SIGUSR2 handler once the server has finished creating this
 * client's FIFOs.
 */
static volatile sig_atomic_t received_sigusr2 = 0;

/**
 * @brief Record that the server responded to the connection request
 * @details
 * The client sends SIGUSR1 to the server when starting up. The server replies
 * with SIGUSR2 once FIFO_C2S_<pid> and FIFO_S2C_<pid> are ready. Signal
 * handlers should stay tiny, so this only updates a sig_atomic_t flag.
 *
 * @param signal received signal number, unused because only SIGUSR2 is handled
 */
static void handle_sigusr2(int signal) {
    (void)signal;
    received_sigusr2 = 1;
}

/**
 * @brief Connect the client process to the animate server
 * @details
 * Builds the FIFO names required by the assignment protocol, installs the
 * SIGUSR2 handler, sends SIGUSR1 to the server, waits briefly for the reply,
 * then opens both FIFOs. The server owns FIFO creation and must signal back
 * before the client attempts to open them.
 *
 * @param server_pid PID printed by animate_server on startup
 * @param client_pid PID of this client process
 * @param c2s_fd output location for the client to server FIFO descriptor
 * @param s2c_fd output location for the server to client FIFO descriptor
 * @return 1 on successful connection, otherwise 0
 */
int client_connect_to_server(pid_t server_pid, pid_t client_pid,
                             int *c2s_fd, int *s2c_fd) {
    char c2s_path[FIFO_NAME_SIZE];
    char s2c_path[FIFO_NAME_SIZE];
    struct sigaction action;
    int waited;

    /* FIFO names must match the exact format required by the brief. */
    snprintf(c2s_path, sizeof(c2s_path), "FIFO_C2S_%d", client_pid);
    snprintf(s2c_path, sizeof(s2c_path), "FIFO_S2C_%d", client_pid);

    received_sigusr2 = 0;

    /* Install the handler before notifying the server, or the reply can race. */
    action.sa_handler = handle_sigusr2;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if (sigaction(SIGUSR2, &action, NULL) == -1) {
        return 0;
    }

    /* Ask the server to create this client's FIFO pair. */
    if (kill(server_pid, SIGUSR1) == -1) {
        return 0;
    }

    /*
     * Wait for roughly one second in small sleeps. This keeps the client from
     * hanging forever if the server PID is wrong or the server never replies.
     */
    for (waited = 0; waited < 100; waited++) {
        struct timespec delay;

        if (received_sigusr2) {
            break;
        }

        delay.tv_sec = 0;
        delay.tv_nsec = 10000000L;  /* 10 ms */

        nanosleep(&delay, NULL);
    }

    if (!received_sigusr2) {
        fprintf(stderr, "Connection timed out\n");
        return 0;
    }

    /*
     * Open the write end first, then the read end. If the second open fails,
     * clean up the first descriptor so the client does not leak an fd.
     */
    *c2s_fd = open(c2s_path, O_WRONLY);
    if (*c2s_fd == -1) {
        return 0;
    }

    *s2c_fd = open(s2c_path, O_RDONLY);
    if (*s2c_fd == -1) {
        close(*c2s_fd);
        return 0;
    }

    return 1;
}