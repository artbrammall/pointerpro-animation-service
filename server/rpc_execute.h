#ifndef RPC_EXECUTE_H
#define RPC_EXECUTE_H

#include "client_state.h"
#include "rpc_parse.h"

/* =========================================================================
 * RPC execution interface
 * ========================================================================= */

/**
 * @brief Execute one parsed RPC command
 * @details
 * Runs the server side action for a parsed client request. This includes
 * calling libanimate where needed, validating handles and access, handling
 * sharing/barrier/generate commands, and writing the protocol response text
 * into the supplied response buffer.
 *
 * @param client client that sent the request
 * @param command parsed RPC command to execute
 * @param response destination buffer for the server response without newline
 * @param response_size size of the response buffer
 * @return 0 on normal response formatting, otherwise a negative internal code
 */
int execute_rpc(ClientState *client,
                const RpcCommand *command,
                char *response,
                size_t response_size);

#endif