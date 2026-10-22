#ifndef QUEUE_H
#define QUEUE_H

#include "client_state.h"
#include "rpc_parse.h"

/* =========================================================================
 * Request queue data
 * ========================================================================= */

/**
 * @brief One RPC request waiting for a worker thread
 * @details
 * Stores the client that sent the request, the parsed RPC command, and the
 * per-client sequence number used to preserve response order. Requests are
 * linked together in the global queue.
 */
typedef struct Request {
    ClientState *client;
    RpcCommand command;
    uint64_t seq;
    struct Request *next;
} Request;

/* =========================================================================
 * Request queue interface
 * ========================================================================= */

/**
 * @brief Initialise the global request queue
 * @details
 * Resets queue state before the server starts accepting client RPC work.
 *
 * @return 0 on success
 */
int request_queue_init(void);

/**
 * @brief Destroy queued request nodes
 * @details
 * Frees any requests still waiting in the queue. This is mainly useful during
 * shutdown or test cleanup.
 */
void request_queue_destroy(void);

/**
 * @brief Add a request to the global queue
 * @details
 * Called by the server loop after parsing a client RPC. The request is later
 * popped by one of the fixed worker threads.
 *
 * @param client client that sent the request
 * @param command parsed RPC command to execute
 * @param seq per-client request sequence number
 * @return 0 on success, otherwise -1
 */
int request_queue_push(ClientState *client,
                       const RpcCommand *command,
                       uint64_t seq);

/**
 * @brief Remove and return the next queued request
 * @details
 * Called by worker threads. If the queue is empty, the worker blocks until a
 * request is available instead of busy-waiting.
 *
 * @return next request to process
 */
Request *request_queue_pop(void);

#endif