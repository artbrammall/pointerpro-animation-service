#include "auth_state.h"

#include <string.h>

/* =========================================================================
 * Client authorisation state
 * ========================================================================= */

/*
 * This file is client side only.
 * The server still does the real users.txt authorisation check.
 * These values only help the client know whether it should allow animate RPCs
 * or print "Not logged in" before a successful Login.
 */

static int logged_in = 0;
static char username[33] = "";

/**
 * @brief Check whether the client is currently logged in
 * @details
 * Returns the local login flag used by the client input loop.
 * Before this is set, animate RPC commands should not be sent to the server.
 *
 * @return non-zero if logged in, otherwise 0
 */
int is_logged_in(void) {
    return logged_in;
}

/**
 * @brief Update the local login flag
 * @details
 * Called after the server accepts or clears authorisation.
 * This does not authenticate the user by itself. It only records the result
 * already decided by the server protocol.
 *
 * @param value non-zero for logged in, 0 for logged out
 */
void set_logged_in(int value) {
    logged_in = value;
}

/**
 * @brief Get the currently stored username
 * @details
 * Returns the username saved after a login attempt. This can be used for
 * client side messages and state checks.
 *
 * @return pointer to the stored username string
 */
const char *get_username(void) {
    return username;
}

/**
 * @brief Store the active username
 * @details
 * Copies at most 32 characters because the assignment limits usernames to
 * 32 alphanumeric characters. The final byte is always set to '\0' so the
 * stored value remains a valid C string.
 *
 * @param new_username username to store for the current client session
 */
void set_username(const char *new_username) {
    strncpy(username, new_username, 32);
    username[32] = '\0';
}