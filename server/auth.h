#ifndef AUTH_H
#define AUTH_H

#include "client_state.h"

/* =========================================================================
 * Authentication result codes
 * ========================================================================= */

/*
 * These values describe the result of checking a username against users.txt.
 * They are used internally by the server before writing the protocol response
 * back to the client.
 */
#define AUTH_OK 0
#define AUTH_UNAUTHORISED 1
#define AUTH_BALANCE 2
#define AUTH_ERROR 3

/* =========================================================================
 * Server authentication interface
 * ========================================================================= */

/**
 * @brief Authenticate a username using users.txt
 * @details
 * Checks whether the username exists and has a positive balance. The server
 * uses this during the Login protocol before allowing animate RPC commands.
 *
 * @param username username supplied by the client
 * @param balance_out output location for the user's balance on success
 * @return AUTH_OK, AUTH_UNAUTHORISED, AUTH_BALANCE, or AUTH_ERROR
 */
int authenticate_user(const char *username, int *balance_out);

/**
 * @brief Check whether a username exists in users.txt
 * @details
 * Used by canvas sharing code to validate the target username without logging
 * that user in or checking whether their balance is positive.
 *
 * @param username username to search for
 * @return non-zero if the user exists, otherwise 0
 */
int user_exists(const char *username);

/**
 * @brief Handle a Login command for a connected client
 * @details
 * Runs server side authentication, updates the client state on success, and
 * writes the exact Login response expected by the client protocol.
 *
 * @param client connected client attempting to log in
 * @param username username argument from the Login command
 * @return 0 on successful login, otherwise non-zero after rejection handling
 */
int handle_login(ClientState *client, char *username);

#endif