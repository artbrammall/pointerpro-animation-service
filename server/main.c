#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>

#include "rpc_parse.h"
#include "handles.h"
#include "queue.h"
#include "threadpool.h"
#include "connection.h"
#include "client_state.h"
#include "auth.h"
#include "../common/util.h"

/* =========================================================================
 * Pending connection signal state
 * ========================================================================= */

/*
 * The SIGUSR1 handler writes connecting client PIDs into this pipe.
 * The main loop drains the pipe and performs FIFO creation outside the signal
 * handler.
 */
static int signal_pipe[2] = {-1, -1};

/* =========================================================================
 * Server argument parsing
 * ========================================================================= */

/**
 * @brief Parse and validate the server thread pool size
 * @details
 * Converts the command line argument into a positive integer. The value is used
 * to start the fixed worker pool that processes libanimate and ffmpeg work.
 *
 * @param text command line argument containing the requested thread count
 * @param out_size output location for the parsed thread pool size
 * @return 1 if the argument is valid, otherwise 0
 */
static int parse_threadpool_size(const char *text, int *out_size) {
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);

    /* Reject empty input, extra characters, and conversion errors. */
    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }

    /* The assignment requires a thread pool size of at least 1. */
    if (value < 1 || value > INT_MAX) {
        return 0;
    }

    *out_size = (int)value;
    return 1;
}

/**
 * @brief Sleep briefly between server loop ticks
 * @details
 * Keeps the server from busy-spinning while still responding much faster than
 * the client's approximate one second connection timeout.
 */
static void sleep_server_tick(void) {
    struct timespec delay;

    delay.tv_sec = 0;
    delay.tv_nsec = 10000000L;

    nanosleep(&delay, NULL);
}

/* =========================================================================
 * Signal handling
 * ========================================================================= */

/**
 * @brief Create the pipe used to queue client connection signals
 * @details
 * The signal handler writes client PIDs into this pipe. Both ends are made
 * non-blocking so the handler cannot freeze the server if the pipe is full.
 *
 * @return 0 on success, otherwise -1
 */
static int setup_signal_pipe(void) {
    int flags;

    if (pipe(signal_pipe) == -1) {
        return -1;
    }

    flags = fcntl(signal_pipe[0], F_GETFL);
    if (flags == -1) {
        return -1;
    }

    if (fcntl(signal_pipe[0], F_SETFL, flags | O_NONBLOCK) == -1) {
        return -1;
    }

    flags = fcntl(signal_pipe[1], F_GETFL);
    if (flags == -1) {
        return -1;
    }

    if (fcntl(signal_pipe[1], F_SETFL, flags | O_NONBLOCK) == -1) {
        return -1;
    }

    return 0;
}

/**
 * @brief Queue a client connection request
 * @details
 * The client sends SIGUSR1 to the server when it starts. The handler records
 * the sender PID by writing it into a pipe. FIFO creation is still done by the
 * main loop because signal handlers must stay minimal.
 *
 * @param signal received signal number
 * @param info signal metadata containing the sender PID
 * @param context unused signal context
 */
static void handle_sigusr1(int signal, siginfo_t *info, void *context) {
    int saved_errno = errno;

    (void)signal;
    (void)context;

    if (info != NULL && signal_pipe[1] != -1) {
        pid_t client_pid = info->si_pid;

        /*
         * write is async-signal-safe. Ignore failure here because the handler
         * cannot safely recover or print errors.
         */
        (void)write(signal_pipe[1], &client_pid, sizeof(client_pid));
    }

    errno = saved_errno;
}

/**
 * @brief Complete the connection handshake for one client
 * @details
 * Creates the FIFO pair, sends SIGUSR2 to wake the client, then creates and
 * registers the server-side ClientState used for future communication.
 *
 * @param client_pid PID of the connecting client
 */
static void accept_client_connection(pid_t client_pid) {
    char c2s_path[256];
    char s2c_path[256];

    snprintf(c2s_path, sizeof(c2s_path), "FIFO_C2S_%d", client_pid);
    snprintf(s2c_path, sizeof(s2c_path), "FIFO_S2C_%d", client_pid);

    /*
     * Create the FIFO pair first, then signal the client that it may open them.
     */
    if (server_create_client_fifos(client_pid)) {
        kill(client_pid, SIGUSR2);

        ClientState *client = create_client(client_pid, c2s_path, s2c_path);

        if (client != NULL) {
            if (add_client(client) == -1) {
                cleanup_client(client);
            }
        }
    }
}

/**
 * @brief Accept all client connections queued by the signal handler
 * @details
 * Drains the signal pipe until no complete PID remains. This prevents closely
 * spaced client connection requests from overwriting each other.
 */
static void accept_pending_connections(void) {
    while (1) {
        pid_t client_pid;
        ssize_t bytes = read(signal_pipe[0], &client_pid, sizeof(client_pid));

        if (bytes == sizeof(client_pid)) {
            accept_client_connection(client_pid);
        } else if (bytes == -1 && errno == EINTR) {
            continue;
        } else if (bytes == -1 &&
                   (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        } else {
            break;
        }
    }
}

/* =========================================================================
 * Client FIFO polling
 * ========================================================================= */

/**
 * @brief Poll all connected clients once for input
 * @details
 * Reads available commands from each client's C2S FIFO. Login and Disconnect
 * are handled directly by the server loop. Normal RPC commands are parsed and
 * pushed into the global request queue so worker threads can execute them.
 *
 * Replies for queued RPCs are stored and flushed elsewhere through the ordered
 * response system. This keeps per-client reply order even when workers finish
 * in a different order.
 */
static void poll_clients_once(void) {
    char buffer[1024];
    size_t count = get_client_count();

    for (size_t i = 0; i < count; i++) {
        ClientState *client = get_client_at_index(i);

        if (client == NULL) {
            continue;
        }

        ssize_t bytes = read(client->c2s_fd, buffer, sizeof(buffer) - 1);

        if (bytes > 0) {
            buffer[bytes] = '\0';
            strip_newline(buffer);

            if (strncmp(buffer, "Login ", 6) == 0) {
                /*
                 * Login is handled before the RPC queue because authorisation
                 * controls whether later animate commands are allowed.
                 */
                if (handle_login(client, buffer + 6)) {
                    i--;
                    count--;
                }

            } else if (strcmp(buffer, "Disconnect") == 0) {
                /*
                 * A graceful disconnect closes the client's FIFOs and removes
                 * them from the active registry.
                 */
                client_flush_responses(client);
                remove_client(client->pid);
                i--;
                count--;
            } else {
                RpcCommand command;

                if (!client->logged_in) {
                    dprintf(client->s2c_fd, "-1\n");
                } else if (!parse_rpc_command(buffer, &command)) {
                    dprintf(client->s2c_fd, "-1\n");
                } else {
                    uint64_t seq = client_next_request_seq(client);

                    /*
                     * Normal RPC work is sent to the shared queue. Worker
                     * threads can execute requests in parallel where possible.
                     */
                    if (request_queue_push(client, &command, seq) != 0) {
                        dprintf(client->s2c_fd, "-3\n");
                    }
                }
            }

        } else if (bytes == 0) {
            /*
             * EOF on the FIFO means the client side closed its write end.
             * Treat that the same way as a disconnect.
             */
            remove_client(client->pid);
            i--;
            count--;

        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            /*
             * Ignore normal non-blocking "nothing to read" cases. Other read
             * errors mean the connection is no longer useful.
             */
            remove_client(client->pid);
            i--;
            count--;
        }
    }
}

/* =========================================================================
 * Server entry point
 * ========================================================================= */

/**
 * @brief Start the animate server
 * @details
 * Validates the thread pool size, initialises the handle table, request queue,
 * and worker pool, then starts accepting signal based client connections.
 * The server prints its PID in the exact format expected by the assignment.
 *
 * @param argc number of command line arguments
 * @param argv command line argument values
 * @return 1 on startup failure. The server normally runs until externally
 *         terminated.
 */
int main(int argc, char **argv) {
    int threadpool_size;

    if (argc != 2) {
        fprintf(stderr, "Usage: ./animate_server <threadpool size>\n");
        return 1;
    }

    if (!parse_threadpool_size(argv[1], &threadpool_size)) {
        fprintf(stderr, "Invalid threadpool size\n");
        return 1;
    }

    if (handles_init() != HANDLE_OK) {
        fprintf(stderr, "Failed to initialise handles\n");
        return 1;
    }

    if (request_queue_init() != 0) {
        fprintf(stderr, "Failed to initialise request queue\n");
        return 1;
    }

    if (threadpool_start(threadpool_size) != 0) {
        fprintf(stderr, "Failed to start thread pool\n");
        return 1;
    }

    if (setup_signal_pipe() == -1) {
        perror("pipe");
        return 1;
    }

    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_sigaction = handle_sigusr1;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO;

    if (sigaction(SIGUSR1, &action, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

    printf("Server PID: %d.\n", getpid());
    fflush(stdout);

    while (1) {
        accept_pending_connections();

        /*
         * One loop tick handles available client commands, checks for clients
         * that died without Disconnect, then sleeps briefly before trying again.
         */
        poll_clients_once();
        check_client_liveness_once();
        sleep_server_tick();
    }
}