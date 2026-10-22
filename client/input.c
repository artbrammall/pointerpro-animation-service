#include "input.h"
#include "output.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define INPUT_SIZE 512
#define RESPONSE_SIZE 512

/* =========================================================================
 * Client input helpers
 * ========================================================================= */

/**
 * @brief Make an input line match the protocol newline format
 * @details
 * Removes any existing newline or carriage return characters from the end of
 * the line, then adds exactly one '\n'. The brief requires every client to
 * server message to end with exactly one newline.
 *
 * @param line input buffer read from stdin
 */
static void normalise_line(char *line) {
    size_t length = strlen(line);

    while (length > 0 &&
           (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        line[length - 1] = '\0';
        length--;
    }

    if (length + 1 < INPUT_SIZE) {
        line[length] = '\n';
        line[length + 1] = '\0';
    }
}

/**
 * @brief Check whether a line is a Login command
 * @details
 * Login is allowed before authorisation because it is the command that starts
 * the server side users.txt check.
 *
 * @param line normalised input line
 * @return non-zero if the line starts with "Login ", otherwise 0
 */
static int is_login_command(const char *line) {
    return strncmp(line, "Login ", 6) == 0;
}

/**
 * @brief Check whether a line is a Disconnect command
 * @details
 * Disconnect is allowed even before login so the client can close cleanly
 * instead of getting stuck behind the local login gate.
 *
 * @param line normalised input line
 * @return non-zero if the line is exactly "Disconnect\n", otherwise 0
 */
static int is_disconnect_command(const char *line) {
    return strcmp(line, "Disconnect\n") == 0;
}

/**
 * @brief Write a complete command line to the server FIFO
 * @details
 * Sends the already normalised command to FIFO_C2S_<pid>. The function treats
 * a partial write as failure because protocol messages should be sent whole.
 *
 * @param fd client to server FIFO descriptor
 * @param line command line to send
 * @return 1 if the full line was written, otherwise 0
 */
static int send_line(int fd, const char *line) {
    size_t length = strlen(line);
    ssize_t written = write(fd, line, length);

    return written == (ssize_t)length;
}

/**
 * @brief Read one response from the server FIFO
 * @details
 * Reads a server response into the supplied buffer and removes one trailing
 * newline before printing. The response text itself is still interpreted by
 * output.c according to the assignment protocol.
 *
 * @param fd server to client FIFO descriptor
 * @param buffer destination buffer for the response
 * @param size size of the destination buffer
 * @return 1 if a response was read, otherwise 0
 */
static int read_response(int fd, char *buffer, size_t size) {
    ssize_t bytes = read(fd, buffer, size - 1);

    if (bytes <= 0) {
        return 0;
    }

    buffer[bytes] = '\0';

    size_t length = strlen(buffer);
    if (length > 0 && buffer[length - 1] == '\n') {
        buffer[length - 1] = '\0';
    }

    return 1;
}

/* =========================================================================
 * Client input loop
 * ========================================================================= */

/**
 * @brief Run the main client command loop
 * @details
 * Reads commands from stdin, enforces the local "Not logged in" rule, forwards
 * allowed lines to the server, and prints each server response. Login responses
 * are handled specially because the client must print the welcome message using
 * the username and balance.
 *
 * @param c2s_fd client to server FIFO descriptor
 * @param s2c_fd server to client FIFO descriptor
 * @return 0 on normal client exit, otherwise 1
 */
int client_input_loop(int c2s_fd, int s2c_fd) {
    char line[INPUT_SIZE];
    char response[RESPONSE_SIZE];
    int logged_in = 0;

    while (fgets(line, sizeof(line), stdin) != NULL) {
        normalise_line(line);

        /*
         * Before login, only Login and Disconnect are allowed through.
         * Animate RPC commands must be rejected locally with this exact text.
         */
        if (!logged_in &&
            !is_login_command(line) &&
            !is_disconnect_command(line)) {
            printf("Not logged in\n");
            fflush(stdout);
            continue;
        }

        if (!send_line(c2s_fd, line)) {
            return 1;
        }

        if (is_login_command(line)) {
            char username[33];

            if (!read_response(s2c_fd, response, sizeof(response))) {
                return 1;
            }

            if (strncmp(response, "Reject ", 7) == 0) {
                printf("%s\n", response);
                fflush(stdout);
                return 0;
            } else {
                sscanf(line, "Login %32s", username);
                printf("Welcome %s. Your balance is %s\n", username, response);
                fflush(stdout);
                logged_in = 1;
            }

        } else if (!is_disconnect_command(line)) {
            if (!read_response(s2c_fd, response, sizeof(response))) {
                return 1;
            }

            print_server_response(response);
        }

        if (is_disconnect_command(line)) {
            break;
        }
    }

    return 0;
}