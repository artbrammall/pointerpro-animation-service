#define _POSIX_C_SOURCE 200809L

#include "auth.h"
#include "../common/util.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* =========================================================================
 * User validation helpers
 * ========================================================================= */

/**
 * @brief Check whether a username matches the assignment format
 * @details
 * Usernames must be alphanumeric and at most 32 characters long. This is used
 * before reading users.txt so invalid Login input is rejected cleanly.
 *
 * @param username username string to validate
 * @return 1 if the username is valid, otherwise 0
 */
static int valid_username(const char *username) {
    size_t len = strlen(username);

    if (len == 0 || len > 32) {
        return 0;
    }

    for (size_t i = 0; i < len; i++) {
        if (!isalnum((unsigned char)username[i])) {
            return 0;
        }
    }

    return 1;
}

/* =========================================================================
 * users.txt authentication
 * ========================================================================= */

/**
 * @brief Authenticate a username against users.txt
 * @details
 * Opens users.txt from the server's working directory and searches for an
 * exact case sensitive username match. A user is only accepted when they exist
 * and their balance is greater than zero, as required by the login protocol.
 *
 * @param username username supplied by the client
 * @param balance_out output location for the accepted user's balance
 * @return AUTH_OK, AUTH_BALANCE, or AUTH_UNAUTHORISED
 */
int authenticate_user(const char *username, int *balance_out) {
    FILE *file;
    char line[256];

    if (!valid_username(username)) {
        return AUTH_UNAUTHORISED;
    }

    file = fopen("users.txt", "r");
    if (file == NULL) {
        return AUTH_UNAUTHORISED;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char file_username[33];
        int balance;
        char extra;

        /*
         * Leading whitespace is ignored. The username and balance are enough
         * for authentication, so malformed trailing text is not used.
         */
        if (sscanf(line, " %32s %d %c", file_username, &balance, &extra) >= 2) {
            if (strcmp(username, file_username) == 0) {
                fclose(file);

                if (balance <= 0) {
                    return AUTH_BALANCE;
                }

                *balance_out = balance;
                return AUTH_OK;
            }
        }
    }

    fclose(file);
    return AUTH_UNAUTHORISED;
}

/**
 * @brief Check whether a valid username exists in users.txt
 * @details
 * Used by sharing logic when the server needs to know whether a named user is
 * real, without also requiring that user to have a positive balance.
 *
 * @param username username to search for
 * @return 1 if the username exists in users.txt, otherwise 0
 */
int user_exists(const char *username) {
    if (!valid_username(username)) {
        return 0;
    }

    FILE *file = fopen("users.txt", "r");

    if (file == NULL) {
        return 0;
    }

    char line[256];

    while (fgets(line, sizeof(line), file) != NULL) {
        char file_username[33];
        int balance;

        if (sscanf(line, " %32s %d", file_username, &balance) == 2) {
            if (strcmp(file_username, username) == 0) {
                fclose(file);
                return 1;
            }
        }
    }

    fclose(file);
    return 0;
}

/* =========================================================================
 * Login protocol handling
 * ========================================================================= */

/**
 * @brief Handle a Login command for one connected client
 * @details
 * Removes the protocol newline, authenticates the username, updates the client
 * state on success, and writes the exact server response required by the
 * assignment. Rejected clients are disconnected after a short delay.
 *
 * @param client connected client attempting to log in
 * @param username username argument from the Login command
 * @return 0 if login succeeds, otherwise 1 after rejection handling
 */
int handle_login(ClientState *client, char *username) {
    int balance = 0;
    int result;

    strip_newline(username);

    result = authenticate_user(username, &balance);

    if (result == AUTH_OK) {
        client->logged_in = 1;

        /* Save the accepted username for later ownership and sharing checks. */
        strncpy(client->username, username, sizeof(client->username) - 1);
        client->username[sizeof(client->username) - 1] = '\0';

        dprintf(client->s2c_fd, "%d\n", balance);
        return 0;
    }

    if (result == AUTH_BALANCE) {
        dprintf(client->s2c_fd, "Reject BALANCE\n");
        sleep(1);
        remove_client(client->pid);
        return 1;
    }

    dprintf(client->s2c_fd, "Reject UNAUTHORISED\n");
    sleep(1);
    remove_client(client->pid);
    return 1;
}