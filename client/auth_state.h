#ifndef AUTH_STATE_H
#define AUTH_STATE_H

/* =========================================================================
 * Client authorisation state interface
 * ========================================================================= */

/**
 * @brief Check whether the client has completed login
 * @details
 * Used by the client input loop to decide whether animate RPC commands should
 * be sent to the server or rejected locally with "Not logged in".
 *
 * @return non-zero if logged in, otherwise 0
 */
int is_logged_in(void);

/**
 * @brief Set the client's local login flag
 * @details
 * This records the result of the server's authorisation response. It does not
 * perform the actual users.txt check itself.
 *
 * @param value non-zero for logged in, 0 for logged out
 */
void set_logged_in(int value);

/**
 * @brief Get the stored username for the current client
 * @details
 * The username is stored after a login attempt so client code can reuse it for
 * local state and messages.
 *
 * @return pointer to the stored username string
 */
const char *get_username(void);

/**
 * @brief Store the username for the current client session
 * @details
 * The implementation truncates this to the assignment username limit, so other
 * files can call this without managing the fixed buffer directly.
 *
 * @param username username to store locally
 */
void set_username(const char *username);

#endif